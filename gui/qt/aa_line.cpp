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
 * Implement aa_line.h.
 */

#include "aa_line.h"

#include <cmath>
#include <cstring>

#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QVector2D>
#include <QVector4D>

namespace ocpn::qtui {

namespace {

// One emitted quad vertex: centreline position + per-segment perpendicular
// normal + which edge (+/-1) + cumulative arc length + per-segment tangent +
// square-cap direction (-1 start / +1 end). Matches the aaline.vert inputs and
// the attribute set below; struct field order must match the attribute order.
struct AaVertex {
  float cx, cy;    // location 0: center (treated as position)
  float nx, ny;    // location 1: normal
  float side;      // location 2
  float arclen;    // location 3
  float tx, ty;    // location 4: tangent
  float cap;       // location 5
};

const QSGGeometry::AttributeSet& aaAttributeSet() {
  static const QSGGeometry::Attribute attrs[] = {
      QSGGeometry::Attribute::create(0, 2, QSGGeometry::FloatType, true),
      QSGGeometry::Attribute::create(1, 2, QSGGeometry::FloatType, false),
      QSGGeometry::Attribute::create(2, 1, QSGGeometry::FloatType, false),
      QSGGeometry::Attribute::create(3, 1, QSGGeometry::FloatType, false),
      QSGGeometry::Attribute::create(4, 2, QSGGeometry::FloatType, false),
      QSGGeometry::Attribute::create(5, 1, QSGGeometry::FloatType, false),
  };
  static const QSGGeometry::AttributeSet set = {6, sizeof(AaVertex), attrs};
  return set;
}

// std140 UBO layout shared with aaline.{vert,frag}. The pencil float at offset
// 112 rounds the block up to 128 (std140 16-byte multiple).
constexpr int kUboSize = 128;

}  // namespace

// ---- shader ---------------------------------------------------------------

class AaLineShader : public QSGMaterialShader {
public:
  AaLineShader() {
    setShaderFileName(VertexStage, QStringLiteral(":/shaders/aaline.vert.qsb"));
    setShaderFileName(FragmentStage,
                      QStringLiteral(":/shaders/aaline.frag.qsb"));
  }

  bool updateUniformData(RenderState& state, QSGMaterial* newMaterial,
                         QSGMaterial* /*oldMaterial*/) override {
    QByteArray* buf = state.uniformData();
    if (buf->size() < kUboSize) buf->resize(kUboSize);
    char* p = buf->data();

    const QMatrix4x4 m = state.combinedMatrix();
    std::memcpy(p + 0, m.constData(), 64);

    const auto* mat = static_cast<AaLineMaterial*>(newMaterial);
    const float col[4] = {static_cast<float>(mat->color.redF()),
                          static_cast<float>(mat->color.greenF()),
                          static_cast<float>(mat->color.blueF()),
                          static_cast<float>(mat->color.alphaF())};
    std::memcpy(p + 64, col, 16);

    const QRect vp = state.viewportRect();
    const float vpx[2] = {static_cast<float>(vp.width()),
                          static_cast<float>(vp.height())};
    std::memcpy(p + 80, vpx, 8);

    const float opacity = state.opacity();
    std::memcpy(p + 88, &opacity, 4);

    const float dpr = state.devicePixelRatio();
    const float halfWidth = mat->widthPx * dpr * 0.5f;
    const float feather = 1.0f * dpr;
    const float dashOn = mat->dashOnPx * dpr;
    const float dashOff = mat->dashOffPx * dpr;
    std::memcpy(p + 92, &halfWidth, 4);
    std::memcpy(p + 96, &feather, 4);
    std::memcpy(p + 100, &dashOn, 4);
    std::memcpy(p + 104, &dashOff, 4);

    // Device px per world unit, derived from the MVP + viewport (no coupling
    // to the viewport object): map a unit world-x step to NDC, scale to px.
    const QVector4D o = m.map(QVector4D(0, 0, 0, 1));
    const QVector4D x = m.map(QVector4D(1, 0, 0, 1));
    const QVector2D on(o.x() / o.w(), o.y() / o.w());
    const QVector2D xn(x.x() / x.w(), x.y() / x.w());
    const float pxPerWorld =
        static_cast<float>((xn - on).length() * 0.5 * vp.width());
    std::memcpy(p + 108, &pxPerWorld, 4);

    const float pencil = mat->pencil;
    std::memcpy(p + 112, &pencil, 4);

    return true;
  }
};

// ---- material -------------------------------------------------------------

QSGMaterialType* AaLineMaterial::type() const {
  static QSGMaterialType t;
  return &t;
}

QSGMaterialShader* AaLineMaterial::createShader(
    QSGRendererInterface::RenderMode) const {
  return new AaLineShader;
}

int AaLineMaterial::compare(const QSGMaterial* other) const {
  const auto* o = static_cast<const AaLineMaterial*>(other);
  if (color.rgba() != o->color.rgba())
    return color.rgba() < o->color.rgba() ? -1 : 1;
  if (widthPx != o->widthPx) return widthPx < o->widthPx ? -1 : 1;
  if (dashOnPx != o->dashOnPx) return dashOnPx < o->dashOnPx ? -1 : 1;
  if (dashOffPx != o->dashOffPx) return dashOffPx < o->dashOffPx ? -1 : 1;
  if (pencil != o->pencil) return pencil < o->pencil ? -1 : 1;
  return 0;
}

// ---- builder --------------------------------------------------------------

QSGGeometryNode* makeAaLineNode(const QList<QPointF>& world_pts,
                                const QColor& color, float width_px,
                                bool closed, float dash_on_px,
                                float dash_off_px, float pencil) {
  const int n = static_cast<int>(world_pts.size());
  if (n < 2) return nullptr;

  // Each segment is an INDEPENDENT quad using its own segment normal (not an
  // averaged bisector). The old bisector normalised the averaged normal and
  // extruded by exactly halfWidth, so an acute turn pinched the line to
  // halfWidth*sin(half-angle) -- thin/invisible at sharp angles. Per-segment
  // normals draw each segment at full width; the shader's square cap (extend
  // halfWidth along the tangent at both ends) makes consecutive segments
  // overlap at the joints so there is no gap or pinch. Square caps now; round
  // joins/caps can come later.
  const int count = closed ? n + 1 : n;  // repeat first point to close
  QList<QPointF> pts;
  pts.reserve(count);
  for (int i = 0; i < n; ++i) pts.append(world_pts[i]);
  if (closed) pts.append(world_pts[0]);

  // Cumulative arc length per point (world units) for the dash phase.
  QList<float> arc;
  arc.reserve(count);
  float acc = 0.0f;
  for (int i = 0; i < count; ++i) {
    if (i > 0)
      acc += static_cast<float>(std::hypot(pts[i].x() - pts[i - 1].x(),
                                           pts[i].y() - pts[i - 1].y()));
    arc.append(acc);
  }

  // Two triangles per segment from the 4 corner vertices (DrawTriangles --
  // robust on every RHI backend, unlike triangle strips/fans).
  const int segs = count - 1;
  auto* geo = new QSGGeometry(aaAttributeSet(), segs * 6);
  geo->setDrawingMode(QSGGeometry::DrawTriangles);
  auto* v = static_cast<AaVertex*>(geo->vertexData());

  int k = 0;
  const auto put = [&](const QPointF& c, const QPointF& nrm, const QPointF& tan,
                       float side, float cap, float al) {
    v[k].cx = static_cast<float>(c.x());
    v[k].cy = static_cast<float>(c.y());
    v[k].nx = static_cast<float>(nrm.x());
    v[k].ny = static_cast<float>(nrm.y());
    v[k].side = side;
    v[k].arclen = al;
    v[k].tx = static_cast<float>(tan.x());
    v[k].ty = static_cast<float>(tan.y());
    v[k].cap = cap;
    ++k;
  };
  for (int i = 0; i < segs; ++i) {
    const double dx = pts[i + 1].x() - pts[i].x();
    const double dy = pts[i + 1].y() - pts[i].y();
    const double len = std::hypot(dx, dy);
    const QPointF t = len > 0.0 ? QPointF(dx / len, dy / len) : QPointF(0, 0);
    const QPointF nrm(-t.y(), t.x());  // left perpendicular
    // a (start) extends back (cap -1), b (end) extends forward (cap +1).
    // tris: (a+,b+,b-) (a+,b-,a-)
    put(pts[i], nrm, t, +1.0f, -1.0f, arc[i]);
    put(pts[i + 1], nrm, t, +1.0f, +1.0f, arc[i + 1]);
    put(pts[i + 1], nrm, t, -1.0f, +1.0f, arc[i + 1]);
    put(pts[i], nrm, t, +1.0f, -1.0f, arc[i]);
    put(pts[i + 1], nrm, t, -1.0f, +1.0f, arc[i + 1]);
    put(pts[i], nrm, t, -1.0f, -1.0f, arc[i]);
  }

  auto* mat = new AaLineMaterial();
  mat->color = color;
  mat->widthPx = width_px;
  mat->dashOnPx = dash_on_px;
  mat->dashOffPx = dash_off_px;
  mat->pencil = pencil;

  auto* node = new QSGGeometryNode();
  node->setGeometry(geo);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(mat);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

}  // namespace ocpn::qtui
