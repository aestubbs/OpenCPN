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
 * Implement sg_builder.h.
 */

#include "sg_builder.h"

#include <cmath>

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGNode>

#include "sg_helpers.h"
#include "sg_texture_cache.h"
#include "tesselator.h"  // libtess2, for drawPolygon fills

namespace ocpn::qtui {

namespace {
inline QSGGeometry::Point2D pt(const QPointF& p) {
  QSGGeometry::Point2D v;
  v.set(static_cast<float>(p.x()), static_cast<float>(p.y()));
  return v;
}

// Unit perpendicular normal of segment a->b (rotate the direction +90deg).
inline QPointF segNormal(const QPointF& a, const QPointF& b, bool& ok) {
  const double dx = b.x() - a.x(), dy = b.y() - a.y();
  const double len = std::hypot(dx, dy);
  ok = len > 0.0;
  return ok ? QPointF(-dy / len, dx / len) : QPointF(0, 0);
}
}  // namespace

SgBuilder::SgBuilder(QSGNode* parent, QQuickWindow* window)
    : m_parent(parent), m_window(window) {}

void SgBuilder::setPen(const QColor& color, float width) {
  m_pen_color = color;
  m_pen_width = width > 0.0f ? width : 1.0f;
  m_has_pen = color.isValid();
}
void SgBuilder::noPen() { m_has_pen = false; }
void SgBuilder::setBrush(const QColor& color) {
  m_brush_color = color;
  m_has_brush = color.isValid();
}
void SgBuilder::noBrush() { m_has_brush = false; }

TextureCacheNode* SgBuilder::textureRoot() {
  if (!m_tex_root && m_parent && m_window) {
    m_tex_root = new TextureCacheNode(m_window);
    m_parent->appendChildNode(m_tex_root);
  }
  return m_tex_root;
}

// A polyline as filled quads: each segment becomes a rectangle `width` wide
// centred on the segment. This gives a true thick line on every RHI backend
// (the GL line primitive caps width at 1). Joints are simple overlaps -- good
// enough for routes/tracks; mitred joins are a later refinement.
void SgBuilder::appendThickPolyline(const QList<QPointF>& pts, const QColor& color,
                               float width, bool closed) {
  if (!m_parent || pts.size() < 2) return;

  // Thin lines: a plain line strip/loop rasterises cleanly (MSAA edges) and
  // is cheaper than quads.
  if (width <= 1.0f) {
    // DrawLineLoop closes back to the first vertex itself.
    auto* node = sg::makeFlatColorNode(
        color, closed ? QSGGeometry::DrawLineLoop : QSGGeometry::DrawLineStrip,
        static_cast<int>(pts.size()), width);
    QSGGeometry::Point2D* v = node->geometry()->vertexDataAsPoint2D();
    for (qsizetype i = 0; i < pts.size(); ++i) v[i] = pt(pts[i]);
    m_parent->appendChildNode(node);
    return;
  }

  const double h = width / 2.0;
  const qsizetype segs = closed ? pts.size() : pts.size() - 1;
  QList<QSGGeometry::Point2D> tris;
  tris.reserve(segs * 6);
  for (qsizetype i = 0; i < segs; ++i) {
    const QPointF& a = pts[i];
    const QPointF& b = pts[(i + 1) % pts.size()];
    bool ok = false;
    const QPointF nrm = segNormal(a, b, ok);
    if (!ok) continue;
    const QPointF off = nrm * h;
    const QPointF a0 = a + off, a1 = a - off, b0 = b + off, b1 = b - off;
    tris << pt(a0) << pt(b0) << pt(b1);  // tri 1
    tris << pt(a0) << pt(b1) << pt(a1);  // tri 2
  }
  if (tris.isEmpty()) return;
  auto* node = sg::makeFlatColorNode(color, QSGGeometry::DrawTriangles,
                                     static_cast<int>(tris.size()));
  QSGGeometry::Point2D* v = node->geometry()->vertexDataAsPoint2D();
  for (qsizetype i = 0; i < tris.size(); ++i) v[i] = tris[i];
  m_parent->appendChildNode(node);
}

void SgBuilder::drawLine(const QPointF& a, const QPointF& b) {
  if (!m_has_pen) return;
  appendThickPolyline({a, b}, m_pen_color, m_pen_width, /*closed=*/false);
}

void SgBuilder::drawPolyline(const QList<QPointF>& pts) {
  if (!m_has_pen) return;
  appendThickPolyline(pts, m_pen_color, m_pen_width, /*closed=*/false);
}

void SgBuilder::drawPolygon(const QList<QPointF>& pts) {
  if (!m_parent || pts.size() < 3) return;

  // Fill: tessellate with libtess2 (NONZERO winding) into a triangle list so
  // concave / self-intersecting outlines fill correctly.
  if (m_has_brush) {
    TESStesselator* tess = tessNewTess(nullptr);
    QList<float> contour;
    contour.reserve(pts.size() * 2);
    for (const QPointF& p : pts) {
      contour.append(static_cast<float>(p.x()));
      contour.append(static_cast<float>(p.y()));
    }
    tessAddContour(tess, 2, contour.constData(), sizeof(float) * 2,
                   static_cast<int>(pts.size()));
    if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 2,
                      nullptr)) {
      const float* verts = tessGetVertices(tess);
      const TESSindex* elems = tessGetElements(tess);
      const int ne = tessGetElementCount(tess);
      QList<QSGGeometry::Point2D> tris;
      tris.reserve(ne * 3);
      for (int i = 0; i < ne; ++i) {
        bool degenerate = false;
        QSGGeometry::Point2D tri[3];
        for (int j = 0; j < 3; ++j) {
          const TESSindex idx = elems[i * 3 + j];
          if (idx == TESS_UNDEF) { degenerate = true; break; }
          tri[j].set(verts[idx * 2], verts[idx * 2 + 1]);
        }
        if (degenerate) continue;
        tris << tri[0] << tri[1] << tri[2];
      }
      if (!tris.isEmpty()) {
        auto* node = sg::makeFlatColorNode(m_brush_color,
                                           QSGGeometry::DrawTriangles,
                                           static_cast<int>(tris.size()));
        QSGGeometry::Point2D* v = node->geometry()->vertexDataAsPoint2D();
        for (qsizetype i = 0; i < tris.size(); ++i) v[i] = tris[i];
        m_parent->appendChildNode(node);
      }
    }
    tessDeleteTess(tess);
  }

  // Outline: closed thick polyline in the pen.
  if (m_has_pen)
    appendThickPolyline(pts, m_pen_color, m_pen_width, /*closed=*/true);
}

void SgBuilder::drawRect(const QRectF& rect) {
  if (m_has_brush && m_parent) {
    auto* node = sg::makeFlatColorNode(m_brush_color, QSGGeometry::DrawTriangles, 6);
    QSGGeometry::Point2D* v = node->geometry()->vertexDataAsPoint2D();
    const float xl = static_cast<float>(rect.left());
    const float xr = static_cast<float>(rect.right());
    const float yt = static_cast<float>(rect.top());
    const float yb = static_cast<float>(rect.bottom());
    v[0].set(xl, yt); v[1].set(xr, yt); v[2].set(xr, yb);
    v[3].set(xl, yt); v[4].set(xr, yb); v[5].set(xl, yb);
    m_parent->appendChildNode(node);
  }
  if (m_has_pen)
    appendThickPolyline({rect.topLeft(), rect.topRight(), rect.bottomRight(),
                         rect.bottomLeft()},
                        m_pen_color, m_pen_width, /*closed=*/true);
}

void SgBuilder::drawCircle(const QPointF& center, float radius, int segments) {
  if (radius <= 0.0f || !m_parent) return;
  int n = segments;
  if (n <= 0) n = std::max(12, static_cast<int>(radius * 0.5f));  // LOD by size
  n = std::min(n, 256);

  QList<QPointF> ring;
  ring.reserve(n);
  for (int i = 0; i < n; ++i) {
    const double a = (2.0 * M_PI * i) / n;
    ring.append(QPointF(center.x() + radius * std::cos(a),
                        center.y() + radius * std::sin(a)));
  }

  // Fill: a triangle list (center, ring[i], ring[i+1]) -- expanded rather
  // than a fan, which Metal rejects.
  if (m_has_brush) {
    QList<QSGGeometry::Point2D> tris;
    tris.reserve(n * 3);
    for (int i = 0; i < n; ++i) {
      tris << pt(center) << pt(ring[i]) << pt(ring[(i + 1) % n]);
    }
    auto* node = sg::makeFlatColorNode(m_brush_color, QSGGeometry::DrawTriangles,
                                       static_cast<int>(tris.size()));
    QSGGeometry::Point2D* v = node->geometry()->vertexDataAsPoint2D();
    for (qsizetype i = 0; i < tris.size(); ++i) v[i] = tris[i];
    m_parent->appendChildNode(node);
  }
  if (m_has_pen)
    appendThickPolyline(ring, m_pen_color, m_pen_width, /*closed=*/true);
}

void SgBuilder::drawImage(const QRectF& dest, const QImage& image) {
  TextureCacheNode* root = textureRoot();
  if (!root || image.isNull()) return;
  QSGTexture* tex = root->texture(image);
  if (!tex) return;
  QSGImageNode* node = m_window->createImageNode();
  if (!node) return;
  node->setTexture(tex);
  node->setOwnsTexture(false);  // TextureCacheNode owns it
  node->setFiltering(QSGTexture::Linear);
  node->setRect(dest);
  m_parent->appendChildNode(node);
}

QImage SgBuilder::renderText(const QString& text, const QColor& color,
                             float point_size) {
  if (text.isEmpty()) return QImage();
  QFont font;  // default system font (the Qt-native replacement for TexFont)
  if (point_size > 0.0f) font.setPointSizeF(point_size);
  const qreal dpr = 2.0;  // render at 2x for crispness on hi-DPI
  QFontMetrics fm(font);
  const QRect br = fm.boundingRect(text);
  const int w = br.width() + 4;
  const int h = br.height() + 4;
  QImage img(static_cast<int>(w * dpr), static_cast<int>(h * dpr),
             QImage::Format_RGBA8888_Premultiplied);
  img.setDevicePixelRatio(dpr);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  p.setFont(font);
  p.setPen(color.isValid() ? color : QColor(0, 0, 0));
  p.drawText(QRectF(0, 0, w, h), Qt::AlignCenter, text);
  p.end();
  return img;
}

void SgBuilder::drawText(const QString& text, const QPointF& top_left,
                    const QColor& color, float point_size) {
  if (text.isEmpty() || !m_window || !m_parent) return;
  const QImage img =
      renderText(text, color.isValid() ? color : m_pen_color, point_size);
  if (img.isNull()) return;
  const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
  // Destination rect sized from the rendered logical extent, at top_left.
  drawImage(QRectF(top_left, QSizeF(img.width() / dpr, img.height() / dpr)),
            img);
}

}  // namespace ocpn::qtui
