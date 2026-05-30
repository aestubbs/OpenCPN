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

#include <algorithm>
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
#include <QTimer>
#include <QScreen>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGTransformNode>

#include "aa_line.h"
#include "coast_shade.h"
#include "sg_helpers.h"
#include "sg_texture_cache.h"
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
  p.set(static_cast<float>(v.x()),
        static_cast<float>(Viewport::latToWorldY(v.y())));
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
  // Zoom rebuild is debounced: keep GPU-transforming the cached subtree
  // during the gesture and re-lay-out (declutter + billboard counter-scale)
  // once it settles. m_zoom_timer fires changed() ~110ms after the last
  // scale change.
  m_zoom_timer = new QTimer(this);
  m_zoom_timer->setSingleShot(true);
  m_zoom_timer->setInterval(110);
  connect(m_zoom_timer, &QTimer::timeout, this, &ChartProvider::changed);
  if (m_viewport) {
    connect(m_viewport, &Viewport::changed, this, [this]() {
      const double s = m_viewport->scale();
      if (s != m_emit_scale) {
        m_emit_scale = s;
        m_zoom_timer->start();  // debounce; rebuild once zoom settles
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

namespace {
// Ray-casting point-in-polygon (ring in lon/lat; test point lon/lat).
bool pointInPoly(const QList<QPointF>& ring, double lon, double lat) {
  bool in = false;
  const int n = ring.size();
  for (int i = 0, j = n - 1; i < n; j = i++) {
    const double xi = ring[i].x(), yi = ring[i].y();
    const double xj = ring[j].x(), yj = ring[j].y();
    if (((yi > lat) != (yj > lat)) &&
        (lon < (xj - xi) * (lat - yi) / (yj - yi) + xi))
      in = !in;
  }
  return in;
}
// Distance (deg) from point p to segment a-b.
double distToSeg(double px, double py, double ax, double ay, double bx,
                 double by) {
  const double dx = bx - ax, dy = by - ay;
  const double len2 = dx * dx + dy * dy;
  double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
  t = std::clamp(t, 0.0, 1.0);
  const double cx = ax + t * dx, cy = ay + t * dy;
  return std::hypot(px - cx, py - cy);
}
}  // namespace

QList<s52sg::QueryObject> S52VectorChartProvider::objectsAt(
    double lat, double lon, double margin_deg) const {
  QList<s52sg::QueryObject> out;
  const double m = margin_deg;
  for (const s52sg::QueryObject& qo : m_buffer.queryObjects) {
    if (lon < qo.minLon - m || lon > qo.maxLon + m || lat < qo.minLat - m ||
        lat > qo.maxLat + m)
      continue;  // bbox reject
    bool hit = false;
    if (qo.geom == s52sg::QueryGeom::Area && qo.shape.size() >= 3) {
      hit = pointInPoly(qo.shape, lon, lat);
    } else if (qo.geom == s52sg::QueryGeom::Point && !qo.shape.isEmpty()) {
      hit = std::hypot(qo.shape[0].x() - lon, qo.shape[0].y() - lat) < m;
    } else if (qo.geom == s52sg::QueryGeom::Line) {
      for (int i = 0; i + 1 < qo.shape.size() && !hit; ++i)
        hit = distToSeg(lon, lat, qo.shape[i].x(), qo.shape[i].y(),
                        qo.shape[i + 1].x(), qo.shape[i + 1].y()) < m;
    }
    if (hit) out.append(qo);
  }
  return out;
}

void S52VectorChartProvider::setShowSoundings(bool on) {
  if (on == m_showSoundings) return;
  m_showSoundings = on;
  m_built = false;  // re-emit the subtree with soundings filtered in/out
  Q_EMIT changed();
}

void S52VectorChartProvider::setShowText(bool on) {
  if (on == m_showText) return;
  m_showText = on;
  m_built = false;
  Q_EMIT changed();
}

void S52VectorChartProvider::setShowLights(bool on) {
  if (on == m_showLights) return;
  m_showLights = on;
  m_built = false;
  Q_EMIT changed();
}

void S52VectorChartProvider::setShowBuoys(bool on) {
  if (on == m_showBuoys) return;
  m_showBuoys = on;
  m_built = false;
  Q_EMIT changed();
}

void S52VectorChartProvider::setDetailScale(double n) {
  if (n <= 0.0 || n == m_unset_scamin_n) return;
  m_unset_scamin_n = n;
  // Pure scale cull -- no geometry rebuild; just re-run the billboard cull.
  m_last_line_scale = -1.0;
  Q_EMIT changed();
}

void S52VectorChartProvider::setDeclutter(bool on) {
  if (on == m_declutter) return;
  m_declutter = on;
  // Label overlap-avoid is part of the per-frame billboard cull; just re-run
  // it (no geometry rebuild). Reset the scale gate so updateBillboards works.
  m_last_line_scale = -1.0;
  Q_EMIT changed();
}

bool S52VectorChartProvider::viewGroupEnabled(int vg) const {
  switch (vg) {
    case s52sg::VgLights: return m_showLights;
    case s52sg::VgBuoysBeacons: return m_showBuoys;
    default: return true;
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
      const double wy = Viewport::latToWorldY(pg.tris[i].y());  // Mercator
      v[i].set(static_cast<float>(wx), static_cast<float>(wy),
               static_cast<float>(wx * ku), static_cast<float>(wy * kv));
    }
    pg.node->markDirty(QSGNode::DirtyGeometry);
  }
}

void S52VectorChartProvider::rebuildComplexLines(double scale,
                                                 double chart_scale_n) {
  if (scale <= 0.0) return;
  constexpr double kScaminUnset = 1.0e8;
  constexpr int kMaxVerts = 60000;  // safety cap per LC feature
  const double k = 1.0 / scale;     // screen px -> world units (deg-equiv)
  for (ComplexLineGeom& g : m_complex_lines) {
    const s52sg::ComplexLine& cl = g.src;
    const double eff =
        cl.scamin >= kScaminUnset ? m_unset_scamin_n : double(cl.scamin);
    const bool hidden = chart_scale_n > eff || cl.lengthPx < 1.0;
    if (g.opacity) g.opacity->setOpacity(hidden ? 0.0 : 1.0);
    if (hidden || !g.node) {
      if (g.node) g.node->geometry()->allocate(0);
      continue;
    }
    const double stepW = cl.lengthPx * k;  // glyph repeat in world units
    std::vector<QSGGeometry::Point2D> verts;  // DrawLines: segment-pair list
    double carry = 0.0;  // distance into the next glyph, carried across segs
    QPointF prevW(cl.path[0].x(), Viewport::latToWorldY(cl.path[0].y()));
    for (qsizetype i = 1;
         i < cl.path.size() && verts.size() < (size_t)kMaxVerts; ++i) {
      const QPointF curW(cl.path[i].x(), Viewport::latToWorldY(cl.path[i].y()));
      const QPointF d = curW - prevW;
      const double segLen = std::hypot(d.x(), d.y());
      if (segLen > 1e-12) {
        const double ux = d.x() / segLen, uy = d.y() / segLen;
        // Rotate a local glyph point (screen px) to the segment direction,
        // scale px->world, translate to the stamp origin.
        for (double sdist = carry;
             sdist + stepW <= segLen && verts.size() < (size_t)kMaxVerts;
             sdist += stepW) {
          const double ox = prevW.x() + ux * sdist;
          const double oy = prevW.y() + uy * sdist;
          for (const s52sg::VectorOp& op : cl.symbol) {
            if (op.filled || op.verts.size() < 2) continue;  // line ops only
            for (const QPointF& lp : op.verts) {
              // (ux,uy) is the segment dir; rotate (lp.x,lp.y) by it.
              const double rx = lp.x() * ux - lp.y() * uy;
              const double ry = lp.x() * uy + lp.y() * ux;
              QSGGeometry::Point2D p;
              p.set(static_cast<float>(ox + rx * k),
                    static_cast<float>(oy + ry * k));
              verts.push_back(p);
            }
          }
        }
        // Carry the leftover so the glyph phase is continuous across segments.
        const double walked =
            std::floor((segLen - carry) / stepW) * stepW + carry;
        carry = (segLen > carry) ? segLen - walked : carry - segLen;
      }
      prevW = curW;
    }
    // DrawLines needs an even vertex count (segment pairs).
    if (verts.size() & 1) verts.pop_back();
    QSGGeometry* geo = g.node->geometry();
    geo->allocate(static_cast<int>(verts.size()));
    if (!verts.empty())
      std::memcpy(geo->vertexDataAsPoint2D(), verts.data(),
                  verts.size() * sizeof(QSGGeometry::Point2D));
    g.node->markDirty(QSGNode::DirtyGeometry);
  }
}

void S52VectorChartProvider::updateScaminNodes(double chart_scale_n) {
  // Only fills/lines with a REAL SCAMIN are wrapped (un-SCAMIN'd fills are
  // never in this list), so a node is hidden exactly when the chart is more
  // zoomed out than its SCAMIN -- the wx rule, applied per-frame via opacity
  // (the renderer skips opacity-0 subtrees, so no draw call). P2.14.
  for (const ScaminNode& sn : m_scamin_nodes) {
    if (!sn.opacity) continue;
    sn.opacity->setOpacity(chart_scale_n > sn.scamin ? 0.0 : 1.0);
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
  rebuildPatternUVs(s);  // lines are now zoom-invariant (AA-line shader)
  m_last_line_scale = s;

  // Current chart scale as a 1:N denominator, for SCAMIN decluttering.
  // N = ground-metres-per-pixel / screen-metres-per-pixel:
  //   ground m/px = (metres per degree of lat) / (pixels per degree)
  //   screen m/px = 1 / (m_screen_ppmm * 1000)
  // m_screen_ppmm is the real display density (set from QScreen at build).
  // Mercator: s is px per degree of LONGITUDE (ground 111320*cos(lat) m), so
  // include cos(centre_lat) for the true 1:N (matches displayScaleN).
  constexpr double kMetresPerDegLat = 111320.0;
  const double clat =
      std::max(0.05, std::cos(viewport.centerLat() * M_PI / 180.0));
  const double chart_scale_n =
      (kMetresPerDegLat * clat / s) * m_screen_ppmm * 1000.0;

  rebuildComplexLines(s, chart_scale_n);  // screen-fixed LC glyphs along lines
  updateScaminNodes(chart_scale_n);       // hide static fills/lines past SCAMIN

  // Effective SCAMIN: many ENC objects (esp. buoys, lights, their sector arcs)
  // carry NO SCAMIN, so they'd pile up at every zoom. Give those a configurable
  // default minimum-display scale (m_unset_scamin_n) so detail thins out when
  // zoomed out -- objects with a real SCAMIN keep it. The s52sg "unset"
  // sentinel is ~1e8.
  constexpr double kScaminUnset = 1.0e8;
  const auto effScamin = [&](const Billboard& b) -> double {
    // Soundings follow the CHART, not a separate SCAMIN cliff. A cell is only
    // rendered while it is content-eligible (within k x native, decided in
    // ChartCanvas::updateVisibleCells), so while it is on screen its soundings
    // stay visible and the shallowest-per-cell declutter below thins them
    // progressively as you zoom out -- instead of every sounding in the cell
    // vanishing the instant the single shared SOUNDG SCAMIN is crossed (the
    // "coarse chart loses all its soundings in one zoom step" symptom). So
    // soundings are never SCAMIN-culled here (declutter still applies).
    if (b.kind == BbKind::Sounding) return 1.0e12;
    const double base = b.scamin >= kScaminUnset ? m_unset_scamin_n
                                                 : static_cast<double>(b.scamin);
    // Nav aids (lights, buoys/beacons, sector arcs, their labels) are HARD-
    // capped at the detail scale even if they carry a large real SCAMIN -- many
    // ENC overview-cell aids have a huge SCAMIN so they'd otherwise pile up at
    // small scale (the NOAA west-coast clutter). Other features keep their own.
    if (b.viewGroup == s52sg::VgLights || b.viewGroup == s52sg::VgBuoysBeacons)
      return std::min(base, m_unset_scamin_n);
    return base;
  };

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
    if (chart_scale_n > effScamin(b)) continue;  // SCAMIN-culled anyway
    if (b.kind == BbKind::Sounding) {
      const qint64 key = soundKey(b.worldPos);
      auto it = cellShallowest.find(key);
      if (it == cellShallowest.end() || b.depth < m_billboards[it.value()].depth)
        cellShallowest[key] = i;
    } else if (b.kind == BbKind::Label) {
      // De-clutter (P2.23a): only suppress overlapping labels when the toggle
      // is on. Off (the wx default) keeps every label -- overlap allowed.
      if (!m_declutter) { labelKeep[i] = true; continue; }
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
    bool hidden = chart_scale_n > effScamin(b);  // SCAMIN hard floor
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

  // The subtree root is a texture cache: it owns and de-duplicates every
  // QSGTexture this chart's pattern fills / symbols / labels use, and frees
  // them (on the render thread) when the compositor releases this subtree.
  auto* root = new TextureCacheNode(window);
  m_patterns.clear();
  m_scamin_nodes.clear();
  m_last_line_scale = -1.0;

  // Clip each chart to its own BOUNDING BOX -- a simple, reliable rectangle (the
  // chart's extent). A chart's geometry already lies within its bbox, so this
  // removes no real content; it only bounds a finer chart to its box so it can't
  // bleed past it over the coarser chart beneath (the composite display rules,
  // Docs/QT_QUILT_VS_WX.md). It replaces the earlier M_COVR-polygon clip, whose
  // complex boundary tessellated incompletely -- deleting valid content (the
  // offshore "hole") and cutting symbols at the irregular coverage edge. The
  // bbox works uniformly for NOAA/OSENC/raster (all carry bounds; not all carry
  // M_COVR). World coords: x = lon, y = latToWorldY(lat) (north -> smaller y).
  QSGNode* content = root;
  {
    const float xl = static_cast<float>(m_west);
    const float xr = static_cast<float>(m_east);
    const float yt = static_cast<float>(Viewport::latToWorldY(m_north));
    const float yb = static_cast<float>(Viewport::latToWorldY(m_south));
    if (xr > xl && yb > yt) {
      auto* clipGeom =
          new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 6);
      clipGeom->setDrawingMode(QSGGeometry::DrawTriangles);
      QSGGeometry::Point2D* cv = clipGeom->vertexDataAsPoint2D();
      cv[0].set(xl, yt); cv[1].set(xr, yt); cv[2].set(xr, yb);
      cv[3].set(xl, yt); cv[4].set(xr, yb); cv[5].set(xl, yb);
      auto* clip = new QSGClipNode();
      clip->setGeometry(clipGeom);
      clip->setFlag(QSGNode::OwnsGeometry, true);
      clip->setIsRectangular(false);  // a rotated viewport makes it a quad
      root->appendChildNode(clip);
      content = clip;
    }
  }

  // Logical pixels per millimetre, for both physical-size line widths and
  // the SCAMIN scale denominator. logicalDotsPerInch gives a consistent
  // physical scale independent of raw pixel density (Qt applies the device
  // pixel ratio on top).
  if (window && window->screen() &&
      window->screen()->logicalDotsPerInch() > 1.0) {
    m_screen_ppmm = window->screen()->logicalDotsPerInch() / 25.4;
  }
  // S-52 pen unit ~0.32mm -> logical px.
  constexpr double kS52PenWidthMM = 0.32;

  // Route a static fill/line node into the tree (P2.14). If it carries a real
  // S-52 SCAMIN, wrap it in an opacity node so updateScaminNodes can hide it by
  // chart scale; otherwise append it directly -- no per-frame cost, and an
  // un-SCAMIN'd area fill always draws (it must persist as the composite
  // underlay). The opacity node is appended in place, so draw/priority order
  // is preserved.
  constexpr double kScaminUnset = 1.0e8;
  auto appendMaybeScamin = [&](QSGNode* node, int scamin) {
    if (static_cast<double>(scamin) < kScaminUnset) {
      auto* op = new QSGOpacityNode();
      op->appendChildNode(node);
      content->appendChildNode(op);
      m_scamin_nodes.append({op, scamin});
    } else {
      content->appendChildNode(node);
    }
  };

  for (const s52sg::Prim& prim : m_buffer.prims) {
    if (prim.verts.isEmpty()) continue;
    if (prim.dispCat > m_displayCategory) continue;  // display-category filter

    // Line features: one anti-aliased line through the shared AA-line shader
    // (aa_line.h). Width is the physical S-52 pen width in logical px, kept
    // screen-fixed by the shader -- no per-zoom rebuild, no parallel strips.
    if (prim.type == s52sg::PrimType::LineStrip) {
      const double widthPx =
          std::max(1.0, prim.width * kS52PenWidthMM * m_screen_ppmm);
      // S-52 dash, mm -> logical px (the AA-line shader runs it along the
      // screen arc length, so it stays a constant physical size at any zoom).
      const float dashOn =
          static_cast<float>(prim.dashOnMm * m_screen_ppmm);
      const float dashOff =
          static_cast<float>(prim.dashOffMm * m_screen_ppmm);
      QList<QPointF> world;
      world.reserve(prim.verts.size());
      for (const QPointF& p : prim.verts)
        world.append(QPointF(p.x(), Viewport::latToWorldY(p.y())));  // Mercator
      if (auto* node = makeAaLineNode(world, prim.color,
                                      static_cast<float>(widthPx),
                                      /*closed=*/false, dashOn, dashOff))
        appendMaybeScamin(node, prim.scamin);
      continue;
    }

    QList<QSGGeometry::Point2D> tris = expandToTriangles(prim);
    if (tris.isEmpty()) continue;

    auto* node = sg::makeFlatColorNode(prim.color, QSGGeometry::DrawTriangles,
                                       static_cast<int>(tris.size()));
    QSGGeometry::Point2D* v = node->geometry()->vertexDataAsPoint2D();
    for (qsizetype i = 0; i < tris.size(); ++i) v[i] = tris[i];
    appendMaybeScamin(node, prim.scamin);
  }

  // Coastline land-shade REMOVED: the inland gradient band followed the LNDARE
  // ring, but that ring is clipped to the ENC cell, so the band also drew
  // around the (often diagonal) chart-cell boundary -- a spurious shaded edge
  // mid-water (e.g. Monterey). The axis-aligned onSameBoundary filter only
  // caught N/S/E/W bbox edges, not the diagonal cell boundary. Per the agreed
  // approach, drop the shade entirely; the coastline now renders as its plain
  // S-52 boundary line (LNDARE/COALNE LS/LC, emitted by P2.15) over the LANDA
  // fill -- the originally-intended rendering. `m_buffer.landContours` is no
  // longer consumed here (left populated; harmless).

  // AP pattern fills: tessellated triangles drawn with a tiling texture.
  // Positions are static; rebuildPatternUVs() lays out screen-fixed UVs
  // once the scale is known and on each zoom. Drawn after solid fills,
  // before lines/symbols.
  m_patterns.clear();
  for (const s52sg::PatternFill& pf : m_buffer.patternFills) {
    if (pf.dispCat > m_displayCategory || pf.tris.isEmpty() || pf.pattern.isNull() ||
        !window)
      continue;
    QSGTexture* tex = root->texture(pf.pattern);  // cache-owned, deduped
    if (!tex) continue;
    tex->setHorizontalWrapMode(QSGTexture::Repeat);
    tex->setVerticalWrapMode(QSGTexture::Repeat);
    tex->setFiltering(QSGTexture::Linear);
    // patterns have transparent gaps -> blending
    auto* node = sg::makeTextureNode(tex, QSGGeometry::DrawTriangles,
                                     static_cast<int>(pf.tris.size()),
                                     /*blending=*/true);
    appendMaybeScamin(node, pf.scamin);  // SCAMIN-cull when it carries one (P2.14)

    const qreal dpr =
        pf.pattern.devicePixelRatio() > 0 ? pf.pattern.devicePixelRatio() : 1.0;
    PatternGeom pg;
    pg.node = node;
    pg.tris = pf.tris;
    pg.tileW = pf.pattern.width() / dpr;
    pg.tileH = pf.pattern.height() / dpr;
    m_patterns.append(pg);
  }

  // Complex (LC) lines: one node per glyph-line; geometry is (re)generated by
  // rebuildComplexLines on each scale change (screen-fixed glyph walked along
  // the path). Drawn over fills, under the point symbols/labels.
  m_complex_lines.clear();
  for (const s52sg::ComplexLine& cl : m_buffer.complexLines) {
    if (cl.dispCat > m_displayCategory || cl.path.size() < 2 ||
        cl.symbol.isEmpty())
      continue;
    auto* node = sg::makeFlatColorNode(cl.color, QSGGeometry::DrawLines, 0);
    auto* opacity = new QSGOpacityNode();
    opacity->appendChildNode(node);
    content->appendChildNode(opacity);
    ComplexLineGeom g;
    g.node = node;
    g.opacity = opacity;
    g.src = cl;
    m_complex_lines.append(g);
  }

  // Billboarded point items: one QSGTransformNode (placed at the world
  // anchor, counter-scaled per viewport) wrapping a textured quad. Built
  // once with their textures; updateBillboards only touches the transform.
  m_billboards.clear();
  auto addBillboard = [&](const QImage& image, QPointF worldPos,
                          QPointF pivotPx, int scamin, BbKind kind,
                          float depth, double rotationDeg = 0.0,
                          int viewGroup = 0) {
    if (image.isNull() || !window) return;
    QSGTexture* tex = root->texture(image);  // cache-owned, deduped by name
    if (!tex) return;
    const qreal dpr = image.devicePixelRatio() > 0 ? image.devicePixelRatio()
                                                    : 1.0;
    const qreal w = image.width() / dpr;
    const qreal h = image.height() / dpr;
    auto* img = window->createImageNode();
    img->setTexture(tex);
    img->setOwnsTexture(false);  // TextureCacheNode (root) owns it
    img->setRect(QRectF(-pivotPx.x(), -pivotPx.y(), w, h));
    img->setFiltering(QSGTexture::Linear);
    auto* xform = new QSGTransformNode();
    if (rotationDeg != 0.0) {
      // Fixed rotation about the pivot (which sits at the node origin), e.g.
      // an S-52 SY angle. Sits inside the per-frame placement xform.
      auto* rot = new QSGTransformNode();
      QMatrix4x4 m;
      m.rotate(static_cast<float>(rotationDeg), 0.0f, 0.0f, 1.0f);
      rot->setMatrix(m);
      rot->appendChildNode(img);
      xform->appendChildNode(rot);
    } else {
      xform->appendChildNode(img);
    }
    auto* opacity = new QSGOpacityNode();
    opacity->appendChildNode(xform);
    content->appendChildNode(opacity);
    Billboard b;
    b.opacity = opacity;
    b.xform = xform;
    b.worldPos = worldPos;
    b.scamin = scamin;
    b.kind = kind;
    b.viewGroup = viewGroup;
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
    if (!viewGroupEnabled(sym.viewGroup)) continue;  // Lights/Buoys toggle
    addBillboard(sym.image,
                 QPointF(sym.pos.x(), Viewport::latToWorldY(sym.pos.y())),
                 sym.pivot, sym.scamin, BbKind::Symbol, /*depth=*/0.0f,
                 sym.rotationDeg, sym.viewGroup);
  }

  // Vector (HPGL) symbols -- billboarded geometry. The op coords are
  // symbol-local pixels (pivot at origin); the billboard transform places
  // + screen-fixes them like the raster symbols.
  for (const s52sg::VectorSymbol& vs : m_buffer.vectorSymbols) {
    if (vs.dispCat > m_displayCategory) continue;
    if (!viewGroupEnabled(vs.viewGroup)) continue;  // Lights/Buoys toggle
    auto* xform = new QSGTransformNode();
    for (const s52sg::VectorOp& op : vs.ops) {
      const int need = op.filled ? 3 : 2;
      if (op.verts.size() < need) continue;
      auto* node = sg::makeFlatColorNode(
          op.color,
          op.filled ? QSGGeometry::DrawTriangles : QSGGeometry::DrawLines,
          static_cast<int>(op.verts.size()));
      QSGGeometry::Point2D* v = node->geometry()->vertexDataAsPoint2D();
      for (qsizetype i = 0; i < op.verts.size(); ++i)
        v[i].set(static_cast<float>(op.verts[i].x()),
                 static_cast<float>(op.verts[i].y()));
      xform->appendChildNode(node);
    }
    if (xform->childCount() == 0) {
      delete xform;
      continue;
    }
    auto* opacity = new QSGOpacityNode();
    opacity->appendChildNode(xform);
    content->appendChildNode(opacity);
    Billboard b;
    b.opacity = opacity;
    b.xform = xform;
    b.worldPos = QPointF(vs.pos.x(), Viewport::latToWorldY(vs.pos.y()));
    b.scamin = vs.scamin;
    b.kind = BbKind::Vector;
    b.viewGroup = vs.viewGroup;
    m_billboards.append(b);
  }

  // Text labels (soundings, names) -- appended LAST so they draw on top of
  // the point symbols / dots they annotate.
  for (const s52sg::Label& lab : m_buffer.labels) {
    if (lab.dispCat > m_displayCategory) continue;
    // Viewing-group filter: soundings vs. other text (names), each
    // independently toggleable (mirrors s52plib's ShowSoundings /
    // ShowS57Text).
    if (lab.isSounding ? !m_showSoundings : !m_showText) continue;
    QImage img = renderLabelImage(lab);
    const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
    addBillboard(img, QPointF(lab.pos.x(), Viewport::latToWorldY(lab.pos.y())),
                 QPointF(img.width() / dpr / 2.0, img.height() / dpr / 2.0),
                 lab.scamin, lab.isSounding ? BbKind::Sounding : BbKind::Label,
                 lab.depth, /*rotationDeg=*/0.0, lab.viewGroup);
  }

  updateBillboards(viewport);
  m_built = true;
  return root;
}

}  // namespace ocpn::qtui
