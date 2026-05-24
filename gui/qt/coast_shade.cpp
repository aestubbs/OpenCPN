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

#include <cmath>
#include <cstring>

#include <QSGGeometry>
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
    const float width = mat->widthPx * dpr;
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
                                    float max_alpha) {
  // Count quad-triangles (6 verts per edge) across all usable contours.
  int total = 0;
  for (const QList<QPointF>& c : contours)
    if (c.size() >= 3) total += c.size() * 6;
  if (total == 0) return nullptr;

  auto* geo = new QSGGeometry(shadeAttributeSet(), total);
  geo->setDrawingMode(QSGGeometry::DrawTriangles);
  auto* v = static_cast<ShadeVertex*>(geo->vertexData());
  int k = 0;

  for (const QList<QPointF>& c : contours) {
    const int n = c.size();
    if (n < 3) continue;

    // Inward normal sign from winding: left normal (-dy, dx) points to the
    // interior for a positive-signed-area (CCW) ring, else the right normal.
    const double sgn = signedArea2(c) >= 0.0 ? 1.0 : -1.0;

    // Per-vertex unit inward bisector normals.
    QList<QPointF> inward;
    inward.reserve(n);
    const auto edgeNormal = [&](const QPointF& a, const QPointF& b, bool& ok) {
      const double dx = b.x() - a.x(), dy = b.y() - a.y();
      const double len = std::hypot(dx, dy);
      ok = len > 0.0;
      // left normal (-dy,dx) scaled by winding sign -> inward.
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
      v[k].cx = static_cast<float>(c[i].x());
      v[k].cy = static_cast<float>(c[i].y());
      v[k].nx = static_cast<float>(inward[i].x());
      v[k].ny = static_cast<float>(inward[i].y());
      v[k].t = t;
      ++k;
    };
    for (int i = 0; i < n; ++i) {
      const int j = (i + 1) % n;
      // quad: (i,0)(i,1)(j,0)(j,1) -> tris (i0,j0,j1)(i0,j1,i1)
      put(i, 0.0f); put(j, 0.0f); put(j, 1.0f);
      put(i, 0.0f); put(j, 1.0f); put(i, 1.0f);
    }
  }

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
