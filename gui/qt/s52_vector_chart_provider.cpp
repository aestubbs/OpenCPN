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
 * Implement s52_vector_chart_provider.h.
 */

#include "s52_vector_chart_provider.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QMatrix4x4>
#include <QPainter>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGNode>
#include <QSGTransformNode>

#include "viewport.h"

namespace ocpn::qtui {

namespace {
// Render a text label to an RGBA image using a SYSTEM font (the default
// application font). This is the Qt-native replacement for the chart's
// proprietary TexFont/DepthFont engine; font selection becomes
// configurable later. Rendered at 2x for crispness on hi-DPI.
QImage renderLabelImage(const s52sg::Label& lab) {
  QFont font;  // default system font
  font.setPointSizeF(lab.pointSize);
  const qreal dpr = 2.0;
  QFontMetrics fm(font);
  QRect br = fm.boundingRect(lab.text);
  const int w = (br.width() + 4);
  const int h = (br.height() + 4);
  QImage img(static_cast<int>(w * dpr), static_cast<int>(h * dpr),
             QImage::Format_RGBA8888_Premultiplied);
  img.setDevicePixelRatio(dpr);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  p.setFont(font);
  p.setPen(lab.color.isValid() ? lab.color : QColor(0, 0, 0));
  p.drawText(QRectF(0, 0, w, h), Qt::AlignCenter, lab.text);
  p.end();
  return img;
}
// World convention: x = lon, y = -lat (see viewport.h). Buffer vertices
// are QPointF(lon, lat).
QSGGeometry::Point2D worldPoint(const QPointF& v) {
  QSGGeometry::Point2D p;
  p.set(static_cast<float>(v.x()), static_cast<float>(-v.y()));
  return p;
}

// Expand a fill primitive into an independent triangle list. The Qt RHI
// backends (Metal/Vulkan/D3D) do not all support triangle fans -- Metal
// in particular rejects them ("Primitive topology 0x6 not supported") --
// and GLU tessellation emits fans and strips freely. Converting to a
// DrawTriangles list here keeps the s52plib emit GPU-agnostic and works
// on every backend.
QList<QSGGeometry::Point2D> expandToTriangles(const s52sg::Prim& prim) {
  const QList<QPointF>& v = prim.verts;
  QList<QSGGeometry::Point2D> out;
  if (v.size() < 3) return out;

  switch (prim.type) {
    case s52sg::PrimType::TriangleFan:
      // (v0, vi, vi+1) for i in 1..n-2
      out.reserve((v.size() - 2) * 3);
      for (qsizetype i = 1; i + 1 < v.size(); ++i) {
        out.append(worldPoint(v[0]));
        out.append(worldPoint(v[i]));
        out.append(worldPoint(v[i + 1]));
      }
      break;
    case s52sg::PrimType::TriangleStrip:
      // (vi, vi+1, vi+2) with winding alternation
      out.reserve((v.size() - 2) * 3);
      for (qsizetype i = 0; i + 2 < v.size(); ++i) {
        if (i & 1) {
          out.append(worldPoint(v[i + 1]));
          out.append(worldPoint(v[i]));
          out.append(worldPoint(v[i + 2]));
        } else {
          out.append(worldPoint(v[i]));
          out.append(worldPoint(v[i + 1]));
          out.append(worldPoint(v[i + 2]));
        }
      }
      break;
    case s52sg::PrimType::Triangles:
    default:
      out.reserve(v.size());
      for (const QPointF& vert : v) out.append(worldPoint(vert));
      break;
  }
  return out;
}
}  // namespace

S52VectorChartProvider::S52VectorChartProvider(QString id,
                                               s52sg::Buffer buffer,
                                               double north, double south,
                                               double west, double east,
                                               const Viewport* viewport,
                                               QObject* parent)
    : ChartProvider(parent),
      m_id(std::move(id)),
      m_buffer(std::move(buffer)),
      m_north(north),
      m_south(south),
      m_west(west),
      m_east(east),
      m_viewport(viewport) {
  // Billboarded point items (symbols/text) must re-apply their counter-
  // scale when the viewport zooms. Watch the viewport and ask the
  // wrapping ChartLayer to re-run renderChart (which only updates the
  // billboard transforms -- geometry + textures are built once).
  if (m_viewport) {
    connect(m_viewport, &Viewport::changed, this, &ChartProvider::changed);
  }
}

void S52VectorChartProvider::updateBillboards(const Viewport& viewport) {
  const double s = viewport.scale();
  if (s <= 0.0) return;
  for (const Billboard& b : m_billboards) {
    if (!b.xform) continue;
    QMatrix4x4 m;
    // Placed under the World-anchored root (transform M = ...*scale(s)).
    // translate to the world anchor, then scale(1/s) so M*this leaves the
    // content at screen-pixel size regardless of zoom.
    m.translate(static_cast<float>(b.worldPos.x()),
                static_cast<float>(b.worldPos.y()));
    m.scale(static_cast<float>(1.0 / s), static_cast<float>(1.0 / s));
    b.xform->setMatrix(m);
  }
}

QSGNode* S52VectorChartProvider::renderChart(QSGNode* old_subtree,
                                             const Viewport& viewport,
                                             QQuickWindow* window) {
  // Static fills/lines + the billboard point items are built once. On
  // later calls (viewport pan/zoom) we only re-apply the billboard
  // counter-scale; the World-anchored root transform handles everything
  // else, so no geometry or textures are rebuilt.
  if (old_subtree && m_built) {
    updateBillboards(viewport);
    return old_subtree;
  }

  auto* root = new QSGNode();

  for (const s52sg::Prim& prim : m_buffer.prims) {
    if (prim.verts.isEmpty()) continue;

    // Line features keep their strip topology (supported everywhere);
    // fills expand to an independent triangle list (fans aren't portable).
    if (prim.type == s52sg::PrimType::LineStrip) {
      auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),
                                  static_cast<int>(prim.verts.size()));
      geo->setDrawingMode(QSGGeometry::DrawLineStrip);
      // The Qt RHI backends (Metal/Vulkan/D3D) only support line width 1;
      // wider S-52 pens need quad geometry (a later refinement). Clamp to
      // avoid the per-frame "line widths other than 1" warning.
      geo->setLineWidth(1.0f);
      QSGGeometry::Point2D* v = geo->vertexDataAsPoint2D();
      for (qsizetype i = 0; i < prim.verts.size(); ++i)
        v[i] = worldPoint(prim.verts[i]);

      auto* mat = new QSGFlatColorMaterial();
      mat->setColor(prim.color);
      auto* node = new QSGGeometryNode();
      node->setGeometry(geo);
      node->setFlag(QSGNode::OwnsGeometry);
      node->setMaterial(mat);
      node->setFlag(QSGNode::OwnsMaterial);
      root->appendChildNode(node);
      continue;
    }

    QList<QSGGeometry::Point2D> tris = expandToTriangles(prim);
    if (tris.isEmpty()) continue;

    auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),
                                static_cast<int>(tris.size()));
    geo->setDrawingMode(QSGGeometry::DrawTriangles);
    QSGGeometry::Point2D* v = geo->vertexDataAsPoint2D();
    for (qsizetype i = 0; i < tris.size(); ++i) v[i] = tris[i];

    auto* mat = new QSGFlatColorMaterial();
    mat->setColor(prim.color);

    auto* node = new QSGGeometryNode();
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    root->appendChildNode(node);
  }

  // Billboarded point items: one QSGTransformNode (placed at the world
  // anchor, counter-scaled per viewport) wrapping a textured quad. Built
  // once with their textures; updateBillboards only touches the transform.
  m_billboards.clear();
  auto addBillboard = [&](const QImage& image, QPointF worldPos,
                          QPointF pivotPx) {
    if (image.isNull() || !window) return;
    QSGTexture* tex = window->createTextureFromImage(
        image, QQuickWindow::TextureHasAlphaChannel);
    if (!tex) return;
    const qreal dpr = image.devicePixelRatio() > 0 ? image.devicePixelRatio()
                                                    : 1.0;
    const qreal w = image.width() / dpr;
    const qreal h = image.height() / dpr;
    auto* img = window->createImageNode();
    img->setTexture(tex);
    img->setOwnsTexture(true);
    img->setRect(QRectF(-pivotPx.x(), -pivotPx.y(), w, h));
    img->setFiltering(QSGTexture::Linear);
    auto* xform = new QSGTransformNode();
    xform->appendChildNode(img);
    root->appendChildNode(xform);
    m_billboards.append({xform, worldPos});
  };

  // Text labels (soundings, names) -- centred on the anchor for now.
  for (const s52sg::Label& lab : m_buffer.labels) {
    QImage img = renderLabelImage(lab);
    const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
    addBillboard(img, QPointF(lab.pos.x(), -lab.pos.y()),
                 QPointF(img.width() / dpr / 2.0, img.height() / dpr / 2.0));
  }

  // Point symbols (buoys/beacons) -- pivot is the symbol's hot-spot.
  for (const s52sg::Symbol& sym : m_buffer.symbols) {
    addBillboard(sym.image, QPointF(sym.pos.x(), -sym.pos.y()), sym.pivot);
  }

  updateBillboards(viewport);
  m_built = true;
  return root;
}

}  // namespace ocpn::qtui
