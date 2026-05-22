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

#include <cmath>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QMatrix4x4>
#include <QPainter>
#include <QHash>
#include <QSet>
#include <QQuickWindow>
#include <QScreen>
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

void S52VectorChartProvider::rebuildLines(double scale) {
  if (scale <= 0.0) return;
  // S-52 line widths are in units of ~0.32 mm (the nominal pen unit).
  // Convert width -> physical mm -> logical pixels (via m_screen_ppmm) ->
  // world-space half-width (/scale). Screen-fixed physical thickness on
  // any monitor. Each segment becomes two triangles offset by the segment
  // normal; per-segment quads avoid miter-joint math (slight gaps at sharp
  // bends are not noticeable at chart line widths).
  constexpr double kS52PenWidthMM = 0.32;
  for (const LineGeom& lg : m_lines) {
    const QList<QPointF>& p = lg.worldPts;
    if (p.size() < 2 || !lg.node) {
      if (lg.node && lg.node->geometry())
        lg.node->geometry()->allocate(0);
      continue;
    }
    const double widthLogicalPx = lg.widthPx * kS52PenWidthMM * m_screen_ppmm;
    const double halfW = (widthLogicalPx / scale) / 2.0;

    QList<QSGGeometry::Point2D> tris;
    tris.reserve((p.size() - 1) * 6);
    for (qsizetype i = 0; i + 1 < p.size(); ++i) {
      const QPointF a = p[i], b = p[i + 1];
      double dx = b.x() - a.x(), dy = b.y() - a.y();
      const double len = std::hypot(dx, dy);
      if (len <= 0.0) continue;
      // Perpendicular unit normal * half-width.
      const double nx = -dy / len * halfW;
      const double ny = dx / len * halfW;
      QSGGeometry::Point2D a0, a1, b0, b1;
      a0.set(a.x() + nx, a.y() + ny);
      a1.set(a.x() - nx, a.y() - ny);
      b0.set(b.x() + nx, b.y() + ny);
      b1.set(b.x() - nx, b.y() - ny);
      tris.append(a0); tris.append(a1); tris.append(b0);  // tri 1
      tris.append(b0); tris.append(a1); tris.append(b1);  // tri 2
    }

    QSGGeometry* geo = lg.node->geometry();
    geo->allocate(static_cast<int>(tris.size()));
    QSGGeometry::Point2D* v = geo->vertexDataAsPoint2D();
    for (qsizetype i = 0; i < tris.size(); ++i) v[i] = tris[i];
    lg.node->markDirty(QSGNode::DirtyGeometry);
  }
}

void S52VectorChartProvider::updateBillboards(const Viewport& viewport) {
  const double s = viewport.scale();  // pixels per degree
  if (s <= 0.0) return;

  // Line quad widths depend only on scale, not pan -- rebuild on zoom.
  if (s != m_last_line_scale) {
    rebuildLines(s);
    m_last_line_scale = s;
  }

  // Current chart scale as a 1:N denominator, for SCAMIN decluttering.
  // N = ground-metres-per-pixel / screen-metres-per-pixel:
  //   ground m/px = (metres per degree of lat) / (pixels per degree)
  //   screen m/px = 1 / (m_screen_ppmm * 1000)
  // m_screen_ppmm is the real display density (set from QScreen at build).
  constexpr double kMetresPerDegLat = 111320.0;
  const double chart_scale_n =
      (kMetresPerDegLat / s) * m_screen_ppmm * 1000.0;

  // Sounding density declutter: at most one sounding per ~kCellPx screen
  // cell, keeping the SHALLOWEST (safety). Soundings inherit a single
  // per-feature SCAMIN, so without this they'd all vanish together. The
  // cell is screen pixels = worldPos * s (the constant canvas-centre
  // offset doesn't change which cell neighbours fall in). Non-soundings
  // (symbols, names) are sparse -- shown subject only to SCAMIN.
  constexpr double kCellPx = 46.0;
  const auto cellKey = [&](const QPointF& wp) -> qint64 {
    const long cx = std::lround(wp.x() * s / kCellPx);
    const long cy = std::lround(wp.y() * s / kCellPx);
    return (static_cast<qint64>(cx) << 32) ^ static_cast<quint32>(cy);
  };

  // Pass 1: for each cell, find the shallowest visible sounding.
  QHash<qint64, int> cellShallowest;  // cell -> billboard index
  for (int i = 0; i < m_billboards.size(); ++i) {
    const Billboard& b = m_billboards[i];
    if (!b.isSounding || chart_scale_n > b.scamin) continue;
    const qint64 key = cellKey(b.worldPos);
    auto it = cellShallowest.find(key);
    if (it == cellShallowest.end() ||
        b.depth < m_billboards[it.value()].depth)
      cellShallowest[key] = i;
  }

  // Pass 2: place/hide each billboard.
  for (int i = 0; i < m_billboards.size(); ++i) {
    const Billboard& b = m_billboards[i];
    if (!b.xform) continue;
    bool hidden = chart_scale_n > b.scamin;  // SCAMIN hard floor
    if (!hidden && b.isSounding)
      hidden = (cellShallowest.value(cellKey(b.worldPos), -1) != i);

    QMatrix4x4 m;
    if (hidden) {
      m.scale(0.0f);  // collapse the quad to nothing
    } else {
      // Placed under the World-anchored root (transform M = ...*scale(s)).
      // translate to the world anchor, then scale(1/s) so M*this leaves the
      // content at screen-pixel size regardless of zoom.
      m.translate(static_cast<float>(b.worldPos.x()),
                  static_cast<float>(b.worldPos.y()));
      m.scale(static_cast<float>(1.0 / s), static_cast<float>(1.0 / s));
    }
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
  m_lines.clear();
  m_last_line_scale = -1.0;

  for (const s52sg::Prim& prim : m_buffer.prims) {
    if (prim.verts.isEmpty()) continue;

    // Line features: screen-fixed-width quad geometry (Qt RHI caps real
    // line width at 1). Create the node now with its colour; the quad
    // vertices are (re)built from the polyline by rebuildLines() once the
    // scale is known, and again whenever the scale changes.
    if (prim.type == s52sg::PrimType::LineStrip) {
      auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
      geo->setDrawingMode(QSGGeometry::DrawTriangles);
      auto* mat = new QSGFlatColorMaterial();
      mat->setColor(prim.color);
      auto* node = new QSGGeometryNode();
      node->setGeometry(geo);
      node->setFlag(QSGNode::OwnsGeometry);
      node->setMaterial(mat);
      node->setFlag(QSGNode::OwnsMaterial);
      root->appendChildNode(node);

      LineGeom lg;
      lg.node = node;
      lg.widthPx = prim.width;
      lg.worldPts.reserve(prim.verts.size());
      for (const QPointF& p : prim.verts)
        lg.worldPts.append(QPointF(p.x(), -p.y()));  // world (x=lon, y=-lat)
      m_lines.append(lg);
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
  // Logical pixels per millimetre, for both the SCAMIN scale denominator
  // and physical-size line widths. logicalDotsPerInch gives a consistent
  // physical scale independent of raw pixel density (Qt applies the device
  // pixel ratio on top), so a given S-52 width renders the same physical
  // thickness on any monitor.
  if (window && window->screen() &&
      window->screen()->logicalDotsPerInch() > 1.0) {
    m_screen_ppmm = window->screen()->logicalDotsPerInch() / 25.4;
  }

  m_billboards.clear();
  auto addBillboard = [&](const QImage& image, QPointF worldPos,
                          QPointF pivotPx, int scamin, bool isSounding,
                          float depth) {
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
    m_billboards.append({xform, worldPos, scamin, isSounding, depth});
  };

  // Text labels (soundings, names) -- centred on the anchor for now.
  for (const s52sg::Label& lab : m_buffer.labels) {
    QImage img = renderLabelImage(lab);
    const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
    addBillboard(img, QPointF(lab.pos.x(), -lab.pos.y()),
                 QPointF(img.width() / dpr / 2.0, img.height() / dpr / 2.0),
                 lab.scamin, lab.isSounding, lab.depth);
  }

  // Point symbols (buoys/beacons) -- pivot is the symbol's hot-spot.
  for (const s52sg::Symbol& sym : m_buffer.symbols) {
    addBillboard(sym.image, QPointF(sym.pos.x(), -sym.pos.y()), sym.pivot,
                 sym.scamin, /*isSounding=*/false, /*depth=*/0.0f);
  }

  updateBillboards(viewport);
  m_built = true;
  return root;
}

}  // namespace ocpn::qtui
