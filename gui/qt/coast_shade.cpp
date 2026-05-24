/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * Implement coast_shade.h.
 */

#include "coast_shade.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <QSGGeometry>
#include <QVector2D>
#include <QVector4D>
#include <QSGGeometryNode>

namespace ocpn::qtui {

namespace {
// One emitted vertex: coastline position + unit inward (landward) direction +
// the ramp coordinate (0 at coast, 1 inland). Matches coastshade.vert.
struct ShadeVertex {
  float cx, cy;  // location 0
  float nx, ny;  // location 1 (inward)
  float t;       // location 2
};

const QSGGeometry::AttributeSet& shadeAttributeSet() {
  static const QSGGeometry::Attribute attrs[] = {
      QSGGeometry::Attribute::create(0, 2, QSGGeometry::FloatType, true),
      QSGGeometry::Attribute::create(1, 2, QSGGeometry::FloatType, false),
      QSGGeometry::Attribute::create(2, 1, QSGGeometry::FloatType, false),
  };
  static const QSGGeometry::AttributeSet set = {3, sizeof(ShadeVertex), attrs};
  return set;
}

constexpr int kUboSize = 112;  // std140, matches coastshade.{vert,frag}

// Zoom-adaptive shade width: the full band is fine zoomed in but obscures
// detail zoomed out, so ramp it from 1px (world/regional) up to the material
// width (harbour detail), keyed on logical pixels-per-degree.
constexpr double kLoScale = 40.0;   // <= this px/deg -> 1px
constexpr double kHiScale = 800.0;  // >= this px/deg -> full width
float smoothstep(double a, double b, double x) {
  if (b <= a) return x >= b ? 1.0f : 0.0f;
  double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
  return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

// Twice the signed area of a closed contour; sign gives the winding.
double signedArea2(const QList<QPointF>& c) {
  double a = 0.0;
  const int n = c.size();
  for (int i = 0; i < n; ++i) {
    const QPointF& p = c[i];
    const QPointF& q = c[(i + 1) % n];
    a += p.x() * q.y() - q.x() * p.y();
  }
  return a;
}
}  // namespace

// ---- shader ---------------------------------------------------------------

class CoastShadeShader : public QSGMaterialShader {
public:
  CoastShadeShader() {
    setShaderFileName(VertexStage,
                      QStringLiteral(":/shaders/coastshade.vert.qsb"));
    setShaderFileName(FragmentStage,
                      QStringLiteral(":/shaders/coastshade.frag.qsb"));
  }

  bool updateUniformData(RenderState& state, QSGMaterial* newMaterial,
                         QSGMaterial*) override {
    QByteArray* buf = state.uniformData();
    if (buf->size() < kUboSize) buf->resize(kUboSize);
    char* p = buf->data();

    const QMatrix4x4 m = state.combinedMatrix();
    std::memcpy(p + 0, m.constData(), 64);

    const auto* mat = static_cast<CoastShadeMaterial*>(newMaterial);
    const float col[4] = {static_cast<float>(mat->color.redF()),
                          static_cast<float>(mat->color.greenF()),
                          static_cast<float>(mat->color.blueF()), 1.0f};
    std::memcpy(p + 64, col, 16);

    const QRect vp = state.viewportRect();
    const float vpx[2] = {static_cast<float>(vp.width()),
                          static_cast<float>(vp.height())};
    std::memcpy(p + 80, vpx, 8);

    const float opacity = state.opacity();
    std::memcpy(p + 88, &opacity, 4);

    const float dpr = state.devicePixelRatio();

    // Derive logical pixels-per-degree from the MVP + viewport, and ramp the
    // shade width 1px -> material width with zoom so it doesn't swamp detail
    // when zoomed out.
    const QVector4D o = m.map(QVector4D(0, 0, 0, 1));
    const QVector4D x = m.map(QVector4D(1, 0, 0, 1));
    const QVector2D on(o.x() / o.w(), o.y() / o.w());
    const QVector2D xn(x.x() / x.w(), x.y() / x.w());
    const double pxPerWorldDev = (xn - on).length() * 0.5 * vp.width();
    const double pxPerWorldLog = dpr > 0.0 ? pxPerWorldDev / dpr : pxPerWorldDev;
    const float effLogical =
        1.0f + (mat->widthPx - 1.0f) *
                   smoothstep(kLoScale, kHiScale, pxPerWorldLog);
    const float width = effLogical * dpr;
    std::memcpy(p + 92, &width, 4);
    std::memcpy(p + 96, &mat->maxAlpha, 4);
    return true;
  }
};

QSGMaterialType* CoastShadeMaterial::type() const {
  static QSGMaterialType t;
  return &t;
}

QSGMaterialShader* CoastShadeMaterial::createShader(
    QSGRendererInterface::RenderMode) const {
  return new CoastShadeShader;
}

int CoastShadeMaterial::compare(const QSGMaterial* other) const {
  const auto* o = static_cast<const CoastShadeMaterial*>(other);
  if (color.rgba() != o->color.rgba())
    return color.rgba() < o->color.rgba() ? -1 : 1;
  if (widthPx != o->widthPx) return widthPx < o->widthPx ? -1 : 1;
  if (maxAlpha != o->maxAlpha) return maxAlpha < o->maxAlpha ? -1 : 1;
  return 0;
}

// ---- builder --------------------------------------------------------------

QSGGeometryNode* makeCoastShadeNode(const QList<QList<QPointF>>& contours,
                                    const QColor& color, float width_px,
                                    float max_alpha,
                                    const ShadeEdgeFilter& keep) {
  std::vector<ShadeVertex> verts;  // accumulate; clip edges are skipped
  for (const QList<QPointF>& c : contours) {
    const int n = c.size();
    if (n < 3) continue;

    // Inward normal sign from winding: left normal (-dy, dx) points to the
    // interior for a positive-signed-area (CCW) ring, else the right normal.
    // Computed from the FULL ring so normals stay correct where we skip
    // segments.
    const double sgn = signedArea2(c) >= 0.0 ? 1.0 : -1.0;
    QList<QPointF> inward;
    inward.reserve(n);
    const auto edgeNormal = [&](const QPointF& a, const QPointF& b, bool& ok) {
      const double dx = b.x() - a.x(), dy = b.y() - a.y();
      const double len = std::hypot(dx, dy);
      ok = len > 0.0;
      return ok ? QPointF(sgn * -dy / len, sgn * dx / len) : QPointF(0, 0);
    };
    for (int i = 0; i < n; ++i) {
      bool okIn = false, okOut = false;
      const QPointF nIn = edgeNormal(c[(i - 1 + n) % n], c[i], okIn);
      const QPointF nOut = edgeNormal(c[i], c[(i + 1) % n], okOut);
      QPointF nm = (okIn && okOut) ? (nIn + nOut) : (okOut ? nOut : nIn);
      const double l = std::hypot(nm.x(), nm.y());
      inward.append(l > 1e-9 ? nm / l : QPointF(0, 0));
    }

    const auto put = [&](int i, float t) {
      ShadeVertex sv;
      sv.cx = static_cast<float>(c[i].x());
      sv.cy = static_cast<float>(c[i].y());
      sv.nx = static_cast<float>(inward[i].x());
      sv.ny = static_cast<float>(inward[i].y());
      sv.t = t;
      verts.push_back(sv);
    };
    for (int i = 0; i < n; ++i) {
      const int j = (i + 1) % n;
      if (keep && !keep(c[i], c[j])) continue;  // skip clip edges
      // quad: (i,0)(i,1)(j,0)(j,1) -> tris (i0,j0,j1)(i0,j1,i1)
      put(i, 0.0f); put(j, 0.0f); put(j, 1.0f);
      put(i, 0.0f); put(j, 1.0f); put(i, 1.0f);
    }
  }
  if (verts.empty()) return nullptr;

  auto* geo = new QSGGeometry(shadeAttributeSet(), static_cast<int>(verts.size()));
  geo->setDrawingMode(QSGGeometry::DrawTriangles);
  std::memcpy(geo->vertexData(), verts.data(),
              verts.size() * sizeof(ShadeVertex));

  auto* mat = new CoastShadeMaterial();
  mat->color = color;
  mat->widthPx = width_px;
  mat->maxAlpha = max_alpha;

  auto* node = new QSGGeometryNode();
  node->setGeometry(geo);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(mat);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

}  // namespace ocpn::qtui
