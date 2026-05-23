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
#include <QSGOpacityNode>
#include <QSGTextureMaterial>
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
  // Billboarded point items (symbols/text) re-apply their counter-scale,
  // and lines/patterns relay out, only when the viewport ZOOMS. A pure pan
  // needs none of that -- the World-anchored root transform moves all this
  // chart's geometry for free, billboards keep the same counter-scale, and
  // the (world-space) declutter grid is unchanged. So dirty the layer only
  // on a scale change; panning then costs zero provider work (the compositor
  // just reuses the cached subtree). This is the key pan-performance lever.
  if (m_viewport) {
    connect(m_viewport, &Viewport::changed, this, [this]() {
      const double s = m_viewport->scale();
      if (s != m_emit_scale) {
        m_emit_scale = s;
        Q_EMIT changed();
      }
    });
  }
}

void S52VectorChartProvider::setDisplayCategory(int cat) {
  if (cat == m_displayCategory) return;
  m_displayCategory = cat;
  // Force a full rebuild filtering by the new category. m_built=false makes
  // renderChart construct a fresh subtree (the compositor frees the old).
  m_built = false;
  Q_EMIT changed();
}

void S52VectorChartProvider::rebuildLines(double scale) {
  if (scale <= 0.0) return;
  // Each line feature is N parallel 1px polylines offset perpendicular from
  // the centreline. The strip count N (= rounded physical pen width in
  // logical px) is fixed; only the world-space offset distance changes with
  // scale, so here we just recompute each strip's vertices:
  //   offset_world = (j - (N-1)/2) px / scale, along the per-vertex bisector
  //   normal of the centreline.
  // Stacking ~1px-spaced thin polylines (each rasterised with clean joins +
  // MSAA) yields a smooth thick line without triangle-tessellation joints.
  for (const LineGeom& lg : m_lines) {
    const QList<QPointF>& p = lg.worldPts;
    const int n = static_cast<int>(lg.strips.size());
    if (n == 0) continue;

    if (p.size() < 2) {
      for (QSGGeometryNode* s : lg.strips)
        if (s && s->geometry()) {
          s->geometry()->allocate(0);
          s->markDirty(QSGNode::DirtyGeometry);
        }
      continue;
    }

    // Per-vertex unit bisector normals (averaged adjacent segment normals).
    QList<QPointF> normal;
    normal.reserve(p.size());
    const auto segN = [](const QPointF& a, const QPointF& b, bool& ok) {
      double dx = b.x() - a.x(), dy = b.y() - a.y();
      double len = std::hypot(dx, dy);
      ok = len > 0.0;
      return ok ? QPointF(-dy / len, dx / len) : QPointF(0, 0);
    };
    for (qsizetype i = 0; i < p.size(); ++i) {
      QPointF nIn, nOut;
      bool okIn = false, okOut = false;
      if (i > 0) nIn = segN(p[i - 1], p[i], okIn);
      if (i + 1 < p.size()) nOut = segN(p[i], p[i + 1], okOut);
      QPointF nm = (okIn && okOut) ? (nIn + nOut) : (okOut ? nOut : nIn);
      const double l = std::hypot(nm.x(), nm.y());
      normal.append(l > 1e-9 ? nm / l : QPointF(0, 0));
    }

    for (int j = 0; j < n; ++j) {
      const double offsetPx = j - (n - 1) / 2.0;
      const double offsetWorld = offsetPx / scale;
      QSGGeometry* geo = lg.strips[j]->geometry();
      geo->allocate(static_cast<int>(p.size()));
      QSGGeometry::Point2D* v = geo->vertexDataAsPoint2D();
      for (qsizetype i = 0; i < p.size(); ++i) {
        v[i].set(static_cast<float>(p[i].x() + normal[i].x() * offsetWorld),
                 static_cast<float>(p[i].y() + normal[i].y() * offsetWorld));
      }
      lg.strips[j]->markDirty(QSGNode::DirtyGeometry);
    }
  }
}

void S52VectorChartProvider::rebuildPatternUVs(double scale) {
  if (scale <= 0.0) return;
  // Screen-fixed tiling: one pattern tile spans tileW/tileH logical px on
  // screen, i.e. (tileW/scale) world units. UV = worldCoord / tileWorld =
  // worldCoord * scale / tilePx. The constant canvas-centre offset only
  // shifts the tile phase, which is irrelevant.
  for (const PatternGeom& pg : m_patterns) {
    if (!pg.node) continue;
    QSGGeometry* geo = pg.node->geometry();
    geo->allocate(static_cast<int>(pg.tris.size()));
    QSGGeometry::TexturedPoint2D* v = geo->vertexDataAsTexturedPoint2D();
    const double ku = scale / pg.tileW;
    const double kv = scale / pg.tileH;
    for (qsizetype i = 0; i < pg.tris.size(); ++i) {
      const double wx = pg.tris[i].x();
      const double wy = -pg.tris[i].y();  // world y = -lat
      v[i].set(static_cast<float>(wx), static_cast<float>(wy),
               static_cast<float>(wx * ku), static_cast<float>(wy * kv));
    }
    pg.node->markDirty(QSGNode::DirtyGeometry);
  }
}

void S52VectorChartProvider::updateBillboards(const Viewport& viewport) {
  const double s = viewport.scale();  // pixels per degree
  if (s <= 0.0) return;

  // Everything here -- line offsets, pattern UVs, billboard counter-scale,
  // and the world-space sounding declutter -- depends only on scale, not on
  // pan. Bail out if the scale hasn't changed since the last update so a
  // pan (or a redundant call) does no work.
  if (s == m_last_line_scale) return;
  rebuildLines(s);
  rebuildPatternUVs(s);
  m_last_line_scale = s;

  // Current chart scale as a 1:N denominator, for SCAMIN decluttering.
  // N = ground-metres-per-pixel / screen-metres-per-pixel:
  //   ground m/px = (metres per degree of lat) / (pixels per degree)
  //   screen m/px = 1 / (m_screen_ppmm * 1000)
  // m_screen_ppmm is the real display density (set from QScreen at build).
  constexpr double kMetresPerDegLat = 111320.0;
  const double chart_scale_n =
      (kMetresPerDegLat / s) * m_screen_ppmm * 1000.0;

  // Screen-density declutter (progressive detail). Cells are screen pixels =
  // worldPos * s / cellPx; the constant canvas-centre offset doesn't change
  // which cell a point falls in, so this is pan-invariant. At low zoom items
  // crowd into the same cells -- we drop the losers, so detail thins out and
  // stays readable as you zoom out, and hidden items become opacity-0 (no
  // draw call).
  //   - Soundings: fine grid, keep the SHALLOWEST (safety).
  //   - Text labels: declutter by the label's whole screen bounding box (the
  //     names are long and overlap horizontally even when anchors are far
  //     apart), keep the first, mark every ~20px cell it covers as occupied.
  //   - Symbols / vector marks: kept (nav aids), subject only to SCAMIN.
  constexpr double kSoundingCellPx = 46.0;
  constexpr double kOccCellPx = 20.0;  // label bbox occupancy grid
  const auto soundKey = [&](const QPointF& wp) -> qint64 {
    const long cx = std::lround(wp.x() * s / kSoundingCellPx);
    const long cy = std::lround(wp.y() * s / kSoundingCellPx);
    return (static_cast<qint64>(cx) << 32) ^ static_cast<quint32>(cy);
  };
  const auto occKey = [](long cx, long cy) -> qint64 {
    return (static_cast<qint64>(cx) << 32) ^ static_cast<quint32>(cy);
  };

  // Pass 1: per-cell winners. Soundings -> shallowest; labels -> first that
  // fits, by bounding box.
  QHash<qint64, int> cellShallowest;  // sounding cell -> billboard index
  QSet<qint64> occupied;              // label-bbox-occupied cells
  QHash<int, bool> labelKeep;         // label billboard index -> keep?
  for (int i = 0; i < m_billboards.size(); ++i) {
    const Billboard& b = m_billboards[i];
    if (chart_scale_n > b.scamin) continue;  // SCAMIN-culled anyway
    if (b.kind == BbKind::Sounding) {
      const qint64 key = soundKey(b.worldPos);
      auto it = cellShallowest.find(key);
      if (it == cellShallowest.end() || b.depth < m_billboards[it.value()].depth)
        cellShallowest[key] = i;
    } else if (b.kind == BbKind::Label) {
      // Screen-pixel bbox of the label (centred on the anchor), in occupancy
      // cells.
      const double sx = b.worldPos.x() * s, sy = b.worldPos.y() * s;
      const long c0x = std::lround((sx - b.screenW / 2.0) / kOccCellPx);
      const long c1x = std::lround((sx + b.screenW / 2.0) / kOccCellPx);
      const long c0y = std::lround((sy - b.screenH / 2.0) / kOccCellPx);
      const long c1y = std::lround((sy + b.screenH / 2.0) / kOccCellPx);
      bool clash = false;
      for (long cy = c0y; cy <= c1y && !clash; ++cy)
        for (long cx = c0x; cx <= c1x; ++cx)
          if (occupied.contains(occKey(cx, cy))) { clash = true; break; }
      labelKeep[i] = !clash;
      if (!clash)
        for (long cy = c0y; cy <= c1y; ++cy)
          for (long cx = c0x; cx <= c1x; ++cx) occupied.insert(occKey(cx, cy));
    }
  }

  // Pass 2: show/hide each billboard via its opacity node (opacity 0 =
  // culled, no draw call); set the counter-scale transform when shown.
  for (int i = 0; i < m_billboards.size(); ++i) {
    const Billboard& b = m_billboards[i];
    if (!b.opacity || !b.xform) continue;
    bool hidden = chart_scale_n > b.scamin;  // SCAMIN hard floor
    if (!hidden && b.kind == BbKind::Sounding)
      hidden = (cellShallowest.value(soundKey(b.worldPos), -1) != i);
    else if (!hidden && b.kind == BbKind::Label)
      hidden = !labelKeep.value(i, true);

    b.opacity->setOpacity(hidden ? 0.0 : 1.0);
    if (!hidden) {
      // Placed under the World-anchored root (transform M = ...*scale(s)).
      // translate to the world anchor, then scale(1/s) so M*this leaves the
      // content at screen-pixel size regardless of zoom.
      QMatrix4x4 m;
      m.translate(static_cast<float>(b.worldPos.x()),
                  static_cast<float>(b.worldPos.y()));
      m.scale(static_cast<float>(1.0 / s), static_cast<float>(1.0 / s));
      b.xform->setMatrix(m);
    }
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
  m_patterns.clear();
  m_last_line_scale = -1.0;

  // Logical pixels per millimetre, for both physical-size line widths and
  // the SCAMIN scale denominator. logicalDotsPerInch gives a consistent
  // physical scale independent of raw pixel density (Qt applies the device
  // pixel ratio on top). Computed before the prim loop so line strip counts
  // are known.
  if (window && window->screen() &&
      window->screen()->logicalDotsPerInch() > 1.0) {
    m_screen_ppmm = window->screen()->logicalDotsPerInch() / 25.4;
  }
  // S-52 pen unit ~0.32mm -> logical px.
  constexpr double kS52PenWidthMM = 0.32;

  for (const s52sg::Prim& prim : m_buffer.prims) {
    if (prim.verts.isEmpty()) continue;
    if (prim.dispCat > m_displayCategory) continue;  // display-category filter

    // Line features: N parallel 1px polylines (Qt RHI caps real line width
    // at 1). N is fixed by the physical pen width; create the strips now
    // with their colour, and let rebuildLines() lay out the offset vertices
    // once the scale is known and again whenever it changes.
    if (prim.type == s52sg::PrimType::LineStrip) {
      const double widthLogicalPx =
          prim.width * kS52PenWidthMM * m_screen_ppmm;
      const int n = std::max(1, static_cast<int>(std::lround(widthLogicalPx)));

      LineGeom lg;
      lg.worldPts.reserve(prim.verts.size());
      for (const QPointF& p : prim.verts)
        lg.worldPts.append(QPointF(p.x(), -p.y()));  // world (x=lon, y=-lat)
      for (int j = 0; j < n; ++j) {
        auto* geo =
            new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geo->setDrawingMode(QSGGeometry::DrawLineStrip);
        geo->setLineWidth(1.0f);
        auto* mat = new QSGFlatColorMaterial();
        mat->setColor(prim.color);
        auto* node = new QSGGeometryNode();
        node->setGeometry(geo);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node);
        lg.strips.append(node);
      }
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

  // AP pattern fills: tessellated triangles drawn with a tiling texture.
  // Positions are static; rebuildPatternUVs() lays out screen-fixed UVs
  // once the scale is known and on each zoom. Drawn after solid fills,
  // before lines/symbols.
  m_patterns.clear();
  for (const s52sg::PatternFill& pf : m_buffer.patternFills) {
    if (pf.dispCat > m_displayCategory || pf.tris.isEmpty() || pf.pattern.isNull() ||
        !window)
      continue;
    QSGTexture* tex = window->createTextureFromImage(
        pf.pattern, QQuickWindow::TextureHasAlphaChannel);
    if (!tex) continue;
    tex->setHorizontalWrapMode(QSGTexture::Repeat);
    tex->setVerticalWrapMode(QSGTexture::Repeat);
    tex->setFiltering(QSGTexture::Linear);
    auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(),
                                static_cast<int>(pf.tris.size()));
    geo->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* mat = new QSGTextureMaterial();
    mat->setTexture(tex);
    mat->setFlag(QSGMaterial::Blending);  // patterns have transparent gaps
    auto* node = new QSGGeometryNode();
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    root->appendChildNode(node);

    const qreal dpr =
        pf.pattern.devicePixelRatio() > 0 ? pf.pattern.devicePixelRatio() : 1.0;
    PatternGeom pg;
    pg.node = node;
    pg.tris = pf.tris;
    pg.tileW = pf.pattern.width() / dpr;
    pg.tileH = pf.pattern.height() / dpr;
    m_patterns.append(pg);
  }

  // Billboarded point items: one QSGTransformNode (placed at the world
  // anchor, counter-scaled per viewport) wrapping a textured quad. Built
  // once with their textures; updateBillboards only touches the transform.
  m_billboards.clear();
  auto addBillboard = [&](const QImage& image, QPointF worldPos,
                          QPointF pivotPx, int scamin, BbKind kind,
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
    auto* opacity = new QSGOpacityNode();
    opacity->appendChildNode(xform);
    root->appendChildNode(opacity);
    Billboard b;
    b.opacity = opacity;
    b.xform = xform;
    b.worldPos = worldPos;
    b.scamin = scamin;
    b.kind = kind;
    b.depth = depth;
    b.screenW = static_cast<float>(w);
    b.screenH = static_cast<float>(h);
    m_billboards.append(b);
  };

  // Point symbols (buoys/beacons) -- pivot is the symbol's hot-spot. Drawn
  // BEFORE text labels so a town/feature dot sits UNDER its name (e.g. the
  // "East Oakland" POPL dot), not over it.
  for (const s52sg::Symbol& sym : m_buffer.symbols) {
    if (sym.dispCat > m_displayCategory) continue;
    addBillboard(sym.image, QPointF(sym.pos.x(), -sym.pos.y()), sym.pivot,
                 sym.scamin, BbKind::Symbol, /*depth=*/0.0f);
  }

  // Vector (HPGL) symbols -- billboarded geometry. The op coords are
  // symbol-local pixels (pivot at origin); the billboard transform places
  // + screen-fixes them like the raster symbols.
  for (const s52sg::VectorSymbol& vs : m_buffer.vectorSymbols) {
    if (vs.dispCat > m_displayCategory) continue;
    auto* xform = new QSGTransformNode();
    for (const s52sg::VectorOp& op : vs.ops) {
      const int need = op.filled ? 3 : 2;
      if (op.verts.size() < need) continue;
      auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),
                                  static_cast<int>(op.verts.size()));
      geo->setDrawingMode(op.filled ? QSGGeometry::DrawTriangles
                                    : QSGGeometry::DrawLines);
      QSGGeometry::Point2D* v = geo->vertexDataAsPoint2D();
      for (qsizetype i = 0; i < op.verts.size(); ++i)
        v[i].set(static_cast<float>(op.verts[i].x()),
                 static_cast<float>(op.verts[i].y()));
      auto* mat = new QSGFlatColorMaterial();
      mat->setColor(op.color);
      auto* node = new QSGGeometryNode();
      node->setGeometry(geo);
      node->setFlag(QSGNode::OwnsGeometry);
      node->setMaterial(mat);
      node->setFlag(QSGNode::OwnsMaterial);
      xform->appendChildNode(node);
    }
    if (xform->childCount() == 0) {
      delete xform;
      continue;
    }
    auto* opacity = new QSGOpacityNode();
    opacity->appendChildNode(xform);
    root->appendChildNode(opacity);
    Billboard b;
    b.opacity = opacity;
    b.xform = xform;
    b.worldPos = QPointF(vs.pos.x(), -vs.pos.y());
    b.scamin = vs.scamin;
    b.kind = BbKind::Vector;
    m_billboards.append(b);
  }

  // Text labels (soundings, names) -- appended LAST so they draw on top of
  // the point symbols / dots they annotate.
  for (const s52sg::Label& lab : m_buffer.labels) {
    if (lab.dispCat > m_displayCategory) continue;
    QImage img = renderLabelImage(lab);
    const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
    addBillboard(img, QPointF(lab.pos.x(), -lab.pos.y()),
                 QPointF(img.width() / dpr / 2.0, img.height() / dpr / 2.0),
                 lab.scamin, lab.isSounding ? BbKind::Sounding : BbKind::Label,
                 lab.depth);
  }

  updateBillboards(viewport);
  m_built = true;
  return root;
}

}  // namespace ocpn::qtui
