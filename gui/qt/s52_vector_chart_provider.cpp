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
#include <QVarLengthArray>
#include <QVector2D>
#include <QQuickWindow>
#include <QTimer>
#include <QScreen>
#include <QSGClipNode>

#include "tesselator.h"  // libtess2 -- M_COVR clip tessellation (P2.17)
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGTransformNode>

#include "aa_line.h"
#include "area_pattern_material.h"
#include "coast_shade.h"
#include "sg_helpers.h"
#include "sg_texture_cache.h"
#include "viewport.h"

namespace ocpn::qtui {

namespace {
// Screen-pixel margin added around the view when frustum-culling billboards, so
// a symbol/label whose anchor is just off-screen (but whose glyph straddles the
// edge) isn't culled mid-stroke.
constexpr double kBillboardCullMarginPx = 64.0;

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

// Render a sounding to an RGBA image following the S-52 SNDFRM convention
// (mirrors libs/s52plib SNDFRM02 + IHO S-52 "Soundings"), using a system font
// in place of the proprietary DepthFont symbol set:
//   * the depth (held in metres) is converted to the user's display unit;
//   * the integer part is drawn full size, the tenths digit (if any) as a
//     right-SUBSCRIPT with NO decimal point ("9.5" -> "9" + small low "5");
//   * a drying height (negative depth -- a feature that uncovers, charted as a
//     height above datum) has its integer figures UNDERLINED and no sign;
//   * a sounding at or shallower than the safety depth is EMPHASISED (bold,
//     solid black -- the legacy SOUNDS vs SOUNDG split), deeper ones are the
//     lighter grey the chart used before.
// Decimals are shown only to 31 display-units (S-52); deeper values and feet
// are whole numbers. `safetyMetres` is the safety depth in metres (raw, so the
// comparison is unit-agnostic). `basePt` is the integer-figure point size.
QImage renderSoundingImage(double depthMetres, int depthUnit,
                           double safetyMetres, float basePt, bool swept,
                           bool lowAccuracy) {
  // Guard bogus SENC/ENC values (mirrors SNDFRM02): absurdly deep -> no sounding
  // figure of merit; far-above-datum -> clamp to datum.
  double dm = depthMetres;
  if (dm > 40000.0) dm = 99999.0;
  else if (dm < -1000.0) dm = 0.0;

  // Value in the display unit (DisplayConfig order: 0 m, 1 ft, 2 fathoms).
  double v = dm;
  switch (depthUnit) {
    case 1: v *= 1.0 / 0.3048; break;        // feet
    case 2: v *= 1.0 / 0.3048 / 6.0; break;  // fathoms
    default: break;                          // metres
  }
  const bool drying = dm < 0.0;             // above chart datum (uncovers)
  const bool emphasis = dm <= safetyMetres;  // <= safety depth -> bold/black
  const double av = std::fabs(v);

  // Split into integer + single tenths digit; never emit a decimal point.
  QString intStr, fracStr;
  const bool showTenths = depthUnit != 1 /*feet are whole*/ && av < 31.0;
  if (showTenths) {
    const double r = std::round(av * 10.0) / 10.0;  // round to one decimal
    long long ip = static_cast<long long>(std::floor(r + 1e-6));
    int fp = static_cast<int>(std::llround((r - static_cast<double>(ip)) * 10.0));
    if (fp >= 10) {  // carry from the rounding above
      ip += 1;
      fp = 0;
    }
    intStr = QString::number(ip);
    if (fp > 0) fracStr = QString::number(fp);
  } else {
    intStr = QString::number(static_cast<long long>(std::llround(av)));
  }

  // Fonts: integer full size; the tenths digit a bit smaller (kept large enough
  // to read). The drying-height underline + low-accuracy italic apply to the
  // integer figures; the subscript shares the italic but not the underline.
  QFont fInt;
  fInt.setPointSizeF(basePt);
  fInt.setBold(emphasis);
  fInt.setUnderline(drying);
  fInt.setItalic(lowAccuracy);
  QFont fFrac = fInt;
  fFrac.setPointSizeF(basePt * 0.80);
  fFrac.setUnderline(false);

  const qreal dpr = 2.0;
  QFontMetricsF fmI(fInt), fmF(fFrac);
  const qreal wI = fmI.horizontalAdvance(intStr);
  const qreal wF = fracStr.isEmpty() ? 0.0 : fmF.horizontalAdvance(fracStr);
  // The subscript baseline drops below the integer baseline so it sits low and
  // to the right, as on a paper chart.
  const qreal drop = fmI.ascent() * 0.30;
  const qreal pad = 2.0;
  // Reserve room below the figures for the swept-depth bracket (bar + ticks).
  const qreal sweptGap = swept ? std::max<qreal>(3.0, basePt * 0.35) : 0.0;
  const qreal w = wI + wF + pad * 2.0;
  const qreal h = fmI.ascent() + fmI.descent() + drop + sweptGap + pad * 2.0;

  QImage img(qRound(w * dpr), qRound(h * dpr),
             QImage::Format_RGBA8888_Premultiplied);
  img.setDevicePixelRatio(dpr);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  const QColor col = emphasis ? QColor(0, 0, 0) : QColor(60, 60, 60);
  p.setPen(col);
  const qreal baseY = pad + fmI.ascent();
  p.setFont(fInt);
  p.drawText(QPointF(pad, baseY), intStr);
  if (!fracStr.isEmpty()) {
    p.setFont(fFrac);
    p.drawText(QPointF(pad + wI, baseY + drop), fracStr);
  }
  // Swept depth (TECSOU "swept by wire drag"): a horizontal bar under the
  // integer figures with short upturned end ticks -- the S-52 SOUNDS/GB1 glyph.
  if (swept) {
    const qreal y = baseY + fmI.descent() + sweptGap * 0.5;
    const qreal x0 = pad, x1 = pad + wI;
    const qreal tick = std::max<qreal>(2.0, basePt * 0.22);
    QPen pen(col);
    pen.setWidthF(std::max<qreal>(1.0, basePt * 0.09));
    p.setPen(pen);
    p.drawLine(QPointF(x0, y), QPointF(x1, y));         // bar
    p.drawLine(QPointF(x0, y), QPointF(x0, y - tick));  // left tick (up)
    p.drawLine(QPointF(x1, y), QPointF(x1, y - tick));  // right tick (up)
  }
  p.end();
  return img;
}

// S-52 label placement: returns the point of the text image (logical px,
// top-left origin) that must coincide with the object anchor, from the PresLib
// justification + offsets. hjust 1=centre 2=right 3=left; vjust 1=bottom
// 2=centre 3=top; xoffs/yoffs then shift the text +right/+down from the anchor
// (in average-char-width / char-height units). Mirrors the legacy text adjust
// in s52plib.cpp. Soundings keep their own centred placement.
QPointF labelPivot(qreal w, qreal h, const s52sg::Label& lab) {
  QFont font;
  font.setPointSizeF(lab.pointSize > 0 ? lab.pointSize : 10.0f);
  const QFontMetricsF fm(font);
  qreal refX, refY;
  switch (lab.hjust) {
    case '2': refX = w; break;        // right-justified: anchor at right edge
    case '3': refX = 0.0; break;      // left-justified: anchor at left edge
    default:  refX = w / 2.0; break;  // '1' centred
  }
  switch (lab.vjust) {
    case '3': refY = 0.0; break;      // top: anchor at top edge
    case '2': refY = h / 2.0; break;  // centred
    default:  refY = h; break;        // '1' bottom: anchor at bottom edge
  }
  return QPointF(refX - lab.xoffs * fm.averageCharWidth(),
                 refY - lab.yoffs * fm.height());
}

// Build a billboard's world-anchor placement matrix. The World-anchored root
// applies M = T(centre)*R(theta)*S(scale)*T(-worldCentre), so a local point v
// in this billboard lands on screen at anchor + R(theta)*(v/scale*scale) terms;
// concretely M*T(worldPos)*S(1/scale)*u = anchor_screen + R(theta)*u.
//   * Non-upright (symbols/vector marks): B = T(worldPos)*S(1/scale). The glyph
//     rotates with the chart -- correct for an ORIENT'd light/beacon.
//   * Upright (system-font text): the image is built CENTRED on its origin and
//     B = T(worldPos)*S(1/scale)*T(offset)*R(-theta). Then a glyph point v maps
//     to anchor_screen + R(theta)*offset + v: the placement OFFSET rotates with
//     the chart (label stays on the right side of its feature) while the GLYPH
//     stays upright (v is un-rotated). At theta = 0 (north-up default) the extra
//     T(offset)*R(0) collapses to a plain offset translate, identical to baking
//     the offset into the image rect -- so the common case is unchanged.
QMatrix4x4 billboardMatrix(const QPointF& worldPos, double invScale,
                           bool upright, double rotationRad,
                           const QPointF& offsetPx) {
  QMatrix4x4 m;
  m.translate(static_cast<float>(worldPos.x()),
              static_cast<float>(worldPos.y()));
  m.scale(static_cast<float>(invScale), static_cast<float>(invScale));
  if (upright) {
    m.translate(static_cast<float>(offsetPx.x()),
                static_cast<float>(offsetPx.y()));
    if (rotationRad != 0.0)
      m.rotate(static_cast<float>(-rotationRad * 180.0 / M_PI), 0.0f, 0.0f,
               1.0f);
  }
  return m;
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
  // once it settles. m_zoom_timer fires changed() ~60ms after the last
  // scale change.
  m_zoom_timer = new QTimer(this);
  m_zoom_timer->setSingleShot(true);
  m_zoom_timer->setInterval(60);  // PERF option 2: faster zoom settle
  connect(m_zoom_timer, &QTimer::timeout, this, [this]() {
    // Zoom settled: owe a full declutter re-layout (density depends on scale).
    m_relayout_pending = true;
    Q_EMIT changed();
  });
  if (m_viewport) {
    connect(m_viewport, &Viewport::changed, this, [this]() {
      const double s = m_viewport->scale();
      if (s != m_emit_scale) {
        m_emit_scale = s;
        m_zoom_timer->start();  // zoom: defer the declutter to settle
      }
      // EVERY viewport change (pan, zoom, resize) re-runs the cheap per-frame
      // pass in renderChart: view-frustum-cull (so only on-screen billboards
      // are batched/drawn) and, on a zoom, re-counter-scale. A pan thus toggles
      // opacity only -- no declutter, no geometry rebuild, same subtree node
      // (so the compositor doesn't restructure). This is what bounds the draw
      // to on-screen detail at any zoom.
      Q_EMIT changed();
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

void S52VectorChartProvider::setHiddenClasses(const QSet<QString>& hidden) {
  if (hidden == m_hiddenClasses) return;
  m_hiddenClasses = hidden;
  rebuildHiddenIdx();
  m_built = false;
  Q_EMIT changed();
}

void S52VectorChartProvider::rebuildHiddenIdx() {
  // Translate the acronym set into per-buffer class-index flags so the
  // per-primitive check in the build pass is an array lookup.
  m_hiddenIdx.resize(m_buffer.classes.size());
  for (int i = 0; i < m_buffer.classes.size(); ++i)
    m_hiddenIdx[i] = m_hiddenClasses.contains(m_buffer.classes[i]);
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

void S52VectorChartProvider::setDepthUnit(int unit) {
  if (unit == m_depth_unit) return;
  m_depth_unit = unit;
  m_built = false;  // re-raster sounding labels in the new unit
  Q_EMIT changed();
}

void S52VectorChartProvider::setSafetyDepth(double metres) {
  if (metres == m_safety_depth_m) return;
  m_safety_depth_m = metres;
  m_built = false;  // shallow-sounding (<= safety) emphasis threshold moved
  Q_EMIT changed();
}

void S52VectorChartProvider::setSoundingScale(double mult) {
  if (mult <= 0.0 || mult == m_sounding_scale) return;
  m_sounding_scale = mult;
  m_built = false;  // re-raster soundings at the new figure size
  Q_EMIT changed();
}

void S52VectorChartProvider::setNativeScale(int n) {
  if (n == m_native_scale) return;
  m_native_scale = n;
  // The hatch node is built lazily on the next build; if already built, owe a
  // re-layout so its show/hide + spacing reflect the new native scale.
  m_relayout_pending = true;
  Q_EMIT changed();
}

void S52VectorChartProvider::setOverscaleThreshold(double t) {
  if (t <= 0.0 || t == m_overscale_threshold) return;
  m_overscale_threshold = t;
  m_relayout_pending = true;  // re-evaluate the hatch show/hide
  Q_EMIT changed();
}

void S52VectorChartProvider::setFinerCoverage(
    const QList<QPolygonF>& finer_lonlat) {
  // Transform each finer-cell coverage ring (lon/lat) to WORLD coords
  // (x = lon, y = latToWorldY(lat)) so a billboard's worldPos can be tested
  // directly in coveredByFiner -- no per-point Mercator inverse at cull time.
  QList<QPolygonF> world;
  world.reserve(finer_lonlat.size());
  for (const QPolygonF& poly : finer_lonlat) {
    QPolygonF w;
    w.reserve(poly.size());
    for (const QPointF& p : poly)
      w << QPointF(p.x(), Viewport::latToWorldY(p.y()));
    world << w;
  }
  if (world == m_finer_coverage_world) return;  // unchanged -> no relayout
  m_finer_coverage_world = std::move(world);
  // Changes which point annotations survive owner-cull -- owe a declutter pass.
  m_relayout_pending = true;
  Q_EMIT changed();
}

bool S52VectorChartProvider::coveredByFiner(const QPointF& world_pos) const {
  for (const QPolygonF& poly : m_finer_coverage_world)
    if (poly.containsPoint(world_pos, Qt::OddEvenFill)) return true;
  return false;
}

void S52VectorChartProvider::setDetailScale(double n) {
  if (n <= 0.0 || n == m_unset_scamin_n) return;
  m_unset_scamin_n = n;
  // Changes which billboards survive SCAMIN -- owe a declutter re-layout.
  m_relayout_pending = true;
  Q_EMIT changed();
}

void S52VectorChartProvider::setDeclutter(bool on) {
  if (on == m_declutter) return;
  m_declutter = on;
  // Label overlap-avoid is part of the declutter pass -- owe a re-layout.
  m_relayout_pending = true;
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
  // Reference world coord subtracted from UVs to keep them near zero. With
  // QSGTexture::Repeat the absolute UV phase is irrelevant, but the magnitude
  // is critical: absolute world coords (Mercator Y ~ 59 at this latitude) times
  // scale/tilePx give UVs in the tens of thousands, where float32 precision
  // (ULP ~0.002) is coarser than a tile texel -- the sparse pattern marks then
  // can't be resolved (they smear into a solid band or vanish). Anchoring to the
  // cell corner keeps UVs O(cell-span * scale / tilePx), restoring sub-texel
  // precision so the dots tile crisply.
  const double refX = m_west;
  const double refY = Viewport::latToWorldY(m_north);
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
      // texcoord carries the world offset from the cell reference (small, so it
      // interpolates precisely); AreaPatternMaterial scales it to tile coords in
      // the fragment. This is zoom-invariant -- only kScale changes with zoom.
      v[i].set(static_cast<float>(wx), static_cast<float>(wy),
               static_cast<float>(wx - refX), static_cast<float>(wy - refY));
    }
    if (auto* mat = static_cast<AreaPatternMaterial*>(pg.node->material()))
      mat->kScale = QVector2D(static_cast<float>(ku), static_cast<float>(kv));
    pg.node->markDirty(QSGNode::DirtyGeometry);
    pg.node->markDirty(QSGNode::DirtyMaterial);
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

void S52VectorChartProvider::rebuildOverscaleHatch(double scale,
                                                   double chart_scale_n) {
  // S-52 over-scale indication (OVERSC01): when the display is zoomed in FINER
  // than this cell's compilation scale, hatch the cell so the mariner sees the
  // chart is magnified beyond its survey detail. "Finer" = display 1:N SMALLER
  // than the cell's native 1:N. The hatch is a set of evenly-spaced verticals
  // across the cell bbox, screen-fixed spacing (rebuilt on zoom), inside the
  // cell's own clipped subtree so a finer cell drawn on top hides it.
  if (!m_overscale_hatch || scale <= 0.0) return;
  // Show only when the display is finer than native by MORE than the over-scale
  // threshold. The quilt renders charts overzoomed up to the over-zoom factor as
  // normal display, so hatching at the first hint of overzoom (native*0.99)
  // would paint nearly every chart; the threshold sits above that band (matches
  // the HUD banner + wx's 3.9x). m_native_scale 0 = unknown -> never hatch.
  const bool overscaled =
      m_native_scale > 0 &&
      chart_scale_n < m_native_scale / m_overscale_threshold;
  m_overscale_hatch->setOpacity(overscaled ? 1.0 : 0.0);
  if (!overscaled) return;  // skip the geometry rebuild while hidden

  auto* node = m_overscale_hatch->childCount() > 0
                   ? static_cast<QSGGeometryNode*>(m_overscale_hatch->firstChild())
                   : nullptr;
  if (!node) return;
  // One vertical line every ~kSpacingPx screen px, across the bbox, full height.
  constexpr double kSpacingPx = 14.0;
  const double stepLon = kSpacingPx / scale;  // world (deg) between verticals
  if (stepLon <= 0.0) return;
  const double yt = Viewport::latToWorldY(m_north);
  const double yb = Viewport::latToWorldY(m_south);
  // Cap the vertical count so a coarse cell at extreme overscale can't allocate
  // an unbounded buffer (the cell is clipped to the view anyway).
  int count = static_cast<int>((m_east - m_west) / stepLon);
  if (count < 1) return;
  count = std::min(count, 4000);
  QSGGeometry* geom = node->geometry();
  geom->allocate(count * 2);
  QSGGeometry::Point2D* v = geom->vertexDataAsPoint2D();
  for (int i = 0; i < count; ++i) {
    const float x = static_cast<float>(m_west + (i + 0.5) * stepLon);
    v[i * 2].set(x, static_cast<float>(yt));
    v[i * 2 + 1].set(x, static_cast<float>(yb));
  }
  node->markDirty(QSGNode::DirtyGeometry);
}

void S52VectorChartProvider::recomputeDeclutter(const Viewport& viewport) {
  const double s = viewport.scale();  // pixels per degree
  if (s <= 0.0) return;
  const double rot = viewport.rotation();  // chart rotation (rad), 0 = north-up

  // Scale-dependent, pan-invariant re-layout (pattern UVs, complex lines,
  // static SCAMIN, and the per-billboard `kept` flag + counter-scale matrix).
  // renderChart gates this so it runs only on build and at zoom-settle, never
  // per pan frame.
  rebuildPatternUVs(s);  // lines are now zoom-invariant (AA-line shader)

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
  rebuildOverscaleHatch(s, chart_scale_n);  // S-52 over-scale hatch over bbox

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
  QHash<QString, QList<QRectF>> placedByText;  // same-name dedup: text -> rects
  for (int i = 0; i < m_billboards.size(); ++i) {
    const Billboard& b = m_billboards[i];
    if (coveredByFiner(b.worldPos)) continue;  // a finer quilt cell owns it
    if (chart_scale_n > effScamin(b)) continue;  // SCAMIN-culled anyway
    if (b.kind == BbKind::Sounding) {
      const qint64 key = soundKey(b.worldPos);
      auto it = cellShallowest.find(key);
      if (it == cellShallowest.end() || b.depth < m_billboards[it.value()].depth)
        cellShallowest[key] = i;
    } else if (b.kind == BbKind::Label) {
      // Screen-pixel bbox of the label, centred on the anchor PLUS the S-52
      // placement offset (screenCx/Cy), so tests use where the text draws --
      // offset names/light text don't collide on the symbol anchor.
      const double sx = b.worldPos.x() * s + b.screenCx;
      const double sy = b.worldPos.y() * s + b.screenCy;
      // Same-name de-dup (ALWAYS on): one cell often repeats a place/feature
      // name -- "River Yar" along the river, "Isle of Wight" on each land
      // polygon. Drop a label whose TEXT matches one already placed in this cell
      // AND whose (slightly grown) screen rect overlaps it: redundant stacked
      // copies go, while distinct same-name labels far apart are kept. Unlike
      // the m_declutter toggle below (which thins DIFFERENT overlapping names),
      // this is unconditional -- a name drawn twice in the same spot is noise.
      if (!b.text.isEmpty()) {
        constexpr double kDupMarginPx = 4.0;
        const QRectF probe(sx - b.screenW / 2.0 - kDupMarginPx,
                           sy - b.screenH / 2.0 - kDupMarginPx,
                           b.screenW + 2 * kDupMarginPx,
                           b.screenH + 2 * kDupMarginPx);
        bool dup = false;
        const auto it2 = placedByText.constFind(b.text);
        if (it2 != placedByText.constEnd())
          for (const QRectF& r : it2.value())
            if (r.intersects(probe)) { dup = true; break; }
        if (dup) { labelKeep[i] = false; continue; }
        placedByText[b.text].append(
            QRectF(sx - b.screenW / 2.0, sy - b.screenH / 2.0, b.screenW,
                   b.screenH));
      }
      // General de-clutter (P2.23a): only suppress overlapping (different)
      // labels when the toggle is on. Off (the wx default) keeps every label.
      if (!m_declutter) { labelKeep[i] = true; continue; }
      // Occupancy-grid test for the label's screen bbox.
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

  // Pass 2: record each billboard's declutter result in `kept` (SCAMIN +
  // density), and pre-set the counter-scale matrix on every kept billboard so
  // it is correct the instant a pan brings it on-screen (the per-frame view-cull
  // then only has to toggle opacity). Opacity itself is applied by
  // applyBillboardVisibility, which the caller runs immediately after.
  for (int i = 0; i < m_billboards.size(); ++i) {
    Billboard& b = m_billboards[i];
    bool hidden = chart_scale_n > effScamin(b);  // SCAMIN hard floor
    if (!hidden) hidden = coveredByFiner(b.worldPos);  // finer cell owns it
    if (!hidden && b.kind == BbKind::Sounding)
      hidden = (cellShallowest.value(soundKey(b.worldPos), -1) != i);
    else if (!hidden && b.kind == BbKind::Label)
      hidden = !labelKeep.value(i, true);
    b.kept = !hidden;
    if (b.kept && b.xform)
      b.xform->setMatrix(billboardMatrix(b.worldPos, 1.0 / s, b.upright,
                                         rot, QPointF(b.screenCx, b.screenCy)));
  }
  m_bb_scale = s;
  m_bb_rotation = rot;
}

void S52VectorChartProvider::applyBillboardVisibility(double s, double rot,
                                                      const QRectF& worldView) {
  // Per-frame pass (pan + zoom): show a billboard only if it survived declutter
  // (`kept`) AND lies inside the view. Off-screen / culled ones get opacity 0,
  // which the renderer treats as a blocked subtree: NOT batched, NOT drawn. So
  // the draw-call count tracks ON-SCREEN detail, not the whole (often many-
  // screens-wide) cell -- the win that lets a dense harbour pan/zoom at speed.
  // setOpacity is a no-op when the value is unchanged, so a billboard that does
  // not cross the view edge costs nothing. The matrix is refreshed only when the
  // scale changed (a zoom) OR the chart rotation changed (a course-/head-up
  // turn -- upright text counter-rotates and its offset rotates); a pure pan
  // leaves it (recomputeDeclutter already set it for every kept billboard).
  if (s <= 0.0) return;
  const bool needMatrix = (s != m_bb_scale) || (rot != m_bb_rotation);
  for (const Billboard& b : m_billboards) {
    if (!b.opacity || !b.xform) continue;
    const bool shown = b.kept && worldView.contains(b.worldPos);
    b.opacity->setOpacity(shown ? 1.0 : 0.0);
    if (shown && needMatrix)
      b.xform->setMatrix(billboardMatrix(b.worldPos, 1.0 / s, b.upright,
                                         rot, QPointF(b.screenCx, b.screenCy)));
  }
  m_bb_scale = s;
  m_bb_rotation = rot;
}

void S52VectorChartProvider::applyPrimCull(const QRectF& worldView) {
  // Per-frame: a prim tile whose world bbox doesn't meet the view is opacity 0
  // -> blocked subtree -> its fills/lines are neither batched nor drawn. So a
  // cell costs only its ON-SCREEN geometry, even when its extent is many screens
  // wide (the coarse-underlay / dense-neighbour case). setOpacity is guarded, so
  // a tile that doesn't cross the view edge costs nothing.
  for (const PrimTile& t : m_prim_tiles) {
    if (!t.opacity) continue;
    t.opacity->setOpacity(worldView.intersects(t.bbox) ? 1.0 : 0.0);
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
    // Run the expensive scale-dependent declutter only when owed (zoom settle,
    // or a detail/declutter setting change) -- never per pan frame. Then run the
    // cheap per-frame view-cull (+ counter-scale on a zoom) every time.
    if (m_relayout_pending) {
      recomputeDeclutter(viewport);
      m_relayout_pending = false;
    }
    const QRectF worldView =
        viewport.visibleWorldBounds(kBillboardCullMarginPx);
    applyBillboardVisibility(viewport.scale(), viewport.rotation(), worldView);
    applyPrimCull(worldView);
    return old_subtree;
  }

  // The subtree root is a texture cache: it owns and de-duplicates every
  // QSGTexture this chart's pattern fills / symbols / labels use, and frees
  // them (on the render thread) when the compositor releases this subtree.
  auto* root = new TextureCacheNode(window);
  m_patterns.clear();
  m_scamin_nodes.clear();
  m_overscale_hatch = nullptr;  // recreated below; old node freed with subtree
  m_bb_scale = -1.0;  // force the counter-scale matrices to be set on this build

  // Clip the chart's AREA FILLS + LINES to its own BOUNDING BOX -- a simple,
  // reliable rectangle (the chart's extent). A chart's geometry already lies
  // within its bbox, so this removes no real content; it only bounds a finer
  // chart to its box so it can't bleed past it over the coarser chart beneath
  // (the composite display rules, Docs/QT_QUILT_VS_WX.md). It replaces the
  // earlier M_COVR-polygon clip, whose complex boundary tessellated
  // incompletely -- deleting valid content (the offshore "hole"). The bbox
  // works uniformly for NOAA/OSENC/raster (all carry bounds; not all carry
  // M_COVR). World coords: x = lon, y = latToWorldY(lat) (north -> smaller y).
  //
  // POINT ANNOTATIONS (symbols, text labels, CARC light-sector arcs) are NOT
  // put under this clip -- they are appended to `root` (unclipped) below. A
  // point annotation represents a feature AT a point and must draw in full; the
  // bbox clip would slice a light sector or a name that sweeps past the cell
  // edge (the Yarmouth "cut-off arc"). Cross-cell duplication is instead handled
  // by finest-owner suppression in recomputeDeclutter (see setFinerCoverage),
  // the scene-graph analogue of wx's m_covered_region.Subtract (quilt.cpp).
  QSGNode* content = root;
  {
    // P2.17: clip to the cell's M_COVR coverage union when the catalog
    // carries it (concave rings tessellated NONZERO, holes honoured by
    // winding); else the geographic bounding box, as before.
    QList<QSGGeometry::Point2D> tris;
    if (!m_coverage.isEmpty()) {
      TESStesselator* tess = tessNewTess(nullptr);
      for (const QPolygonF& ring : m_coverage) {
        if (ring.size() < 3) continue;
        QList<float> contour;
        contour.reserve(ring.size() * 2);
        for (const QPointF& p : ring) {  // (lon, lat) -> world
          contour.append(static_cast<float>(p.x()));
          contour.append(
              static_cast<float>(Viewport::latToWorldY(p.y())));
        }
        tessAddContour(tess, 2, contour.constData(), sizeof(float) * 2,
                       static_cast<int>(ring.size()));
      }
      if (tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 2,
                        nullptr)) {
        const float* verts = tessGetVertices(tess);
        const TESSindex* elems = tessGetElements(tess);
        const int ne = tessGetElementCount(tess);
        tris.reserve(ne * 3);
        for (int i = 0; i < ne; ++i) {
          bool degenerate = false;
          QSGGeometry::Point2D tri[3];
          for (int j = 0; j < 3; ++j) {
            const TESSindex idx = elems[i * 3 + j];
            if (idx == TESS_UNDEF) {
              degenerate = true;
              break;
            }
            tri[j].set(verts[idx * 2], verts[idx * 2 + 1]);
          }
          if (!degenerate) tris << tri[0] << tri[1] << tri[2];
        }
      }
      tessDeleteTess(tess);
    }
    const float xl = static_cast<float>(m_west);
    const float xr = static_cast<float>(m_east);
    const float yt = static_cast<float>(Viewport::latToWorldY(m_north));
    const float yb = static_cast<float>(Viewport::latToWorldY(m_south));
    if (tris.isEmpty() && xr > xl && yb > yt) {
      tris.reserve(6);
      QSGGeometry::Point2D p0, p1, p2, p3;
      p0.set(xl, yt); p1.set(xr, yt); p2.set(xr, yb); p3.set(xl, yb);
      tris << p0 << p1 << p2 << p0 << p2 << p3;
    }
    if (!tris.isEmpty()) {
      auto* clipGeom = new QSGGeometry(
          QSGGeometry::defaultAttributes_Point2D(), tris.size());
      clipGeom->setDrawingMode(QSGGeometry::DrawTriangles);
      QSGGeometry::Point2D* cv = clipGeom->vertexDataAsPoint2D();
      for (int i = 0; i < tris.size(); ++i) cv[i] = tris[i];
      auto* clip = new QSGClipNode();
      clip->setGeometry(clipGeom);
      clip->setFlag(QSGNode::OwnsGeometry, true);
      clip->setIsRectangular(false);
      root->appendChildNode(clip);
      content = clip;
    }
  }

  // Logical pixels per millimetre, for physical-size dash patterns and
  // the SCAMIN scale denominator. logicalDotsPerInch gives a consistent
  // physical scale independent of raw pixel density (Qt applies the device
  // pixel ratio on top).
  if (window && window->screen() &&
      window->screen()->logicalDotsPerInch() > 1.0) {
    m_screen_ppmm = window->screen()->logicalDotsPerInch() / 25.4;
  }
  // LS pen width: wx's GL path draws the S-52 width number as DEVICE
  // pixels (s52plib glLineWidth), so a width-2 dashed boundary is one
  // LOGICAL pixel on a 2x display. The earlier 0.32mm-physical reading
  // doubled that -- the magenta dashed area boundaries dominated the
  // chart (user feedback 2026-06-12). Match the wx convention.
  const double dpr = window && window->effectiveDevicePixelRatio() > 0
                         ? window->effectiveDevicePixelRatio()
                         : 1.0;

  // Route a static fill/line node into the tree (P2.14). If it carries a real
  // S-52 SCAMIN, wrap it in an opacity node so updateScaminNodes can hide it by
  // chart scale; otherwise append it directly -- no per-frame cost, and an
  // un-SCAMIN'd area fill always draws (it must persist as the composite
  // underlay). The opacity node is appended in place, so draw/priority order
  // is preserved.
  constexpr double kScaminUnset = 1.0e8;

  // --- Spatial cull grid (perf): bucket fills & lines into a grid of tiles over
  // the cell so applyPrimCull() can drop whole off-screen tiles (not batched,
  // not drawn) -- the fill/line analogue of the billboard frustum cull. This is
  // what lets a many-cell quilt pan fast: each cell submits only the geometry on
  // screen, not its whole (often >> the view) extent. Draw order: ALL fills then
  // ALL lines (S-52 area < line priority) -- the fill containers are appended
  // before the line containers. A prim spanning more than ~one tile bypasses
  // tiling into its per-type underlay node (few; they underlie the rest).
  m_prim_tiles.clear();
  const double cellWest = m_west;
  const double cellYTop = Viewport::latToWorldY(m_north);  // north -> smaller y
  const double cellYBot = Viewport::latToWorldY(m_south);
  const double cellSpanX = m_east - m_west;
  const double cellSpanY = cellYBot - cellYTop;
  constexpr int kPrimGrid = 12;
  const double tileW = cellSpanX > 0 ? cellSpanX / kPrimGrid : 1.0;
  const double tileH = cellSpanY > 0 ? cellSpanY / kPrimGrid : 1.0;
  auto* fillUnderlay = new QSGNode();
  content->appendChildNode(fillUnderlay);
  auto* fillTiles = new QSGNode();
  content->appendChildNode(fillTiles);
  // AP pattern fills get their OWN layer, appended after every solid (AC) fill so
  // they reliably draw ON TOP of a coincident solid fill (the dredged-area
  // DRGARE01 stipple sits over its own DEPMD fill, the CATZOC overlay over the
  // depth shade, ...). In the scene graph an opaque solid fill writes depth and a
  // blended pattern at the *same* geometry loses the depth test (z-fight) when
  // it is only a sibling appended just after the fill; a separate later layer
  // gives the pattern a clear render-order z in front. Drawn under the lines so
  // the depth-contour / dredged-area boundary lines stay on top (wx order).
  auto* patternUnderlay = new QSGNode();
  content->appendChildNode(patternUnderlay);
  auto* patternTiles = new QSGNode();
  content->appendChildNode(patternTiles);
  auto* lineUnderlay = new QSGNode();
  content->appendChildNode(lineUnderlay);
  auto* lineTiles = new QSGNode();
  content->appendChildNode(lineTiles);
  QVarLengthArray<int, kPrimGrid * kPrimGrid> fillGrid(kPrimGrid * kPrimGrid);
  QVarLengthArray<int, kPrimGrid * kPrimGrid> patternGrid(kPrimGrid * kPrimGrid);
  QVarLengthArray<int, kPrimGrid * kPrimGrid> lineGrid(kPrimGrid * kPrimGrid);
  std::fill(fillGrid.begin(), fillGrid.end(), -1);
  std::fill(patternGrid.begin(), patternGrid.end(), -1);
  std::fill(lineGrid.begin(), lineGrid.end(), -1);
  // Parent for a prim with world bbox [minx,maxx]x[miny,maxy]: its grid tile
  // (recorded in m_prim_tiles, created on first use), or `underlay` if it spans
  // more than ~one tile (a large always-drawn prim). The tile's bbox grows to
  // the union of its prims, so culling is conservative -- a tile stays shown
  // while any part of any prim it holds is on screen.
  auto tileParent = [&](QSGNode* tilesParent, QSGNode* underlay,
                        QVarLengthArray<int, kPrimGrid * kPrimGrid>& grid,
                        double minx, double miny, double maxx,
                        double maxy) -> QSGNode* {
    if (cellSpanX <= 0 || cellSpanY <= 0) return underlay;
    if ((maxx - minx) > tileW * 1.5 || (maxy - miny) > tileH * 1.5)
      return underlay;  // large prim: never culled
    const int tx = std::clamp(
        static_cast<int>((0.5 * (minx + maxx) - cellWest) / tileW), 0,
        kPrimGrid - 1);
    const int ty = std::clamp(
        static_cast<int>((0.5 * (miny + maxy) - cellYTop) / tileH), 0,
        kPrimGrid - 1);
    const int g = ty * kPrimGrid + tx;
    const QRectF box(QPointF(minx, miny), QPointF(maxx, maxy));
    if (grid[g] < 0) {
      auto* op = new QSGOpacityNode();
      tilesParent->appendChildNode(op);
      grid[g] = static_cast<int>(m_prim_tiles.size());
      m_prim_tiles.append({op, box});
      return op;
    }
    PrimTile& t = m_prim_tiles[grid[g]];
    t.bbox = t.bbox.united(box);
    return t.opacity;
  };
  auto appendMaybeScamin = [&](QSGNode* parent, QSGNode* node, int scamin) {
    if (static_cast<double>(scamin) < kScaminUnset) {
      auto* op = new QSGOpacityNode();
      op->appendChildNode(node);
      parent->appendChildNode(op);
      m_scamin_nodes.append({op, scamin});
    } else {
      parent->appendChildNode(node);
    }
  };

  // PERF-3: merge CONSECUTIVE prims sharing a draw bucket -- (tile parent,
  // SCAMIN, colour) for fills, + (width, dash) for lines -- into ONE
  // geometry node each. Prims arrive stable-sorted by S-52 priority and a
  // run never crosses a key change, so the draw order is exactly the
  // per-prim path's; only the node count drops (a dense cell's hundreds of
  // same-shade depth fills / contour lines collapse to a handful).
  struct FillRun {
    QSGNode* parent = nullptr;
    int scamin = 0;
    QRgb rgba = 0;
    QList<QSGGeometry::Point2D> tris;
  } fr;
  int nFillPrims = 0, nFillNodes = 0, nLinePrims = 0, nLineNodes = 0;
  auto flushFills = [&] {
    if (fr.parent && !fr.tris.isEmpty()) {
      auto* node =
          sg::makeFlatColorNode(QColor::fromRgba(fr.rgba),
                                QSGGeometry::DrawTriangles,
                                static_cast<int>(fr.tris.size()));
      QSGGeometry::Point2D* v = node->geometry()->vertexDataAsPoint2D();
      std::copy(fr.tris.cbegin(), fr.tris.cend(), v);
      appendMaybeScamin(fr.parent, node, fr.scamin);
      ++nFillNodes;
    }
    fr = FillRun{};
  };
  struct LineRun {
    QSGNode* parent = nullptr;
    int scamin = 0;
    QRgb rgba = 0;
    float widthPx = 0, dashOn = 0, dashOff = 0;
    QList<QList<QPointF>> strips;
  } lr;
  auto flushLines = [&] {
    if (lr.parent && !lr.strips.isEmpty()) {
      if (auto* node = makeAaLineNode(lr.strips, QColor::fromRgba(lr.rgba),
                                      lr.widthPx, lr.dashOn, lr.dashOff)) {
        appendMaybeScamin(lr.parent, node, lr.scamin);
        ++nLineNodes;
      }
    }
    lr = LineRun{};
  };

  for (const s52sg::Prim& prim : m_buffer.prims) {
    if (prim.verts.isEmpty()) continue;
    if (catCulled(prim.dispCat, prim.classIdx)) continue;  // category/class

    // Line features: anti-aliased lines through the shared AA-line shader
    // (aa_line.h). Width is the physical S-52 pen width in logical px, kept
    // screen-fixed by the shader -- no per-zoom rebuild, no parallel strips.
    if (prim.type == s52sg::PrimType::LineStrip) {
      const float widthPx = static_cast<float>(
          std::max(0.75, prim.width / dpr));
      // S-52 dash, mm -> logical px (the AA-line shader runs it along the
      // screen arc length, so it stays a constant physical size at any zoom).
      const float dashOn =
          static_cast<float>(prim.dashOnMm * m_screen_ppmm);
      const float dashOff =
          static_cast<float>(prim.dashOffMm * m_screen_ppmm);
      QList<QPointF> world;
      world.reserve(prim.verts.size());
      double lminx = 1e18, lminy = 1e18, lmaxx = -1e18, lmaxy = -1e18;
      for (const QPointF& p : prim.verts) {
        const double wx = p.x(), wy = Viewport::latToWorldY(p.y());  // Mercator
        world.append(QPointF(wx, wy));
        lminx = std::min(lminx, wx); lmaxx = std::max(lmaxx, wx);
        lminy = std::min(lminy, wy); lmaxy = std::max(lmaxy, wy);
      }
      QSGNode* parent = tileParent(lineTiles, lineUnderlay, lineGrid, lminx,
                                   lminy, lmaxx, lmaxy);
      if (lr.parent != parent || lr.scamin != prim.scamin ||
          lr.rgba != prim.color.rgba() || lr.widthPx != widthPx ||
          lr.dashOn != dashOn || lr.dashOff != dashOff)
        flushLines();
      if (!lr.parent) {
        lr.parent = parent;
        lr.scamin = prim.scamin;
        lr.rgba = prim.color.rgba();
        lr.widthPx = widthPx;
        lr.dashOn = dashOn;
        lr.dashOff = dashOff;
      }
      lr.strips.append(std::move(world));
      ++nLinePrims;
      continue;
    }

    QList<QSGGeometry::Point2D> tris = expandToTriangles(prim);
    if (tris.isEmpty()) continue;

    double fminx = 1e18, fminy = 1e18, fmaxx = -1e18, fmaxy = -1e18;
    for (qsizetype i = 0; i < tris.size(); ++i) {
      fminx = std::min<double>(fminx, tris[i].x);
      fmaxx = std::max<double>(fmaxx, tris[i].x);
      fminy = std::min<double>(fminy, tris[i].y);
      fmaxy = std::max<double>(fmaxy, tris[i].y);
    }
    QSGNode* parent = tileParent(fillTiles, fillUnderlay, fillGrid, fminx,
                                 fminy, fmaxx, fmaxy);
    if (fr.parent != parent || fr.scamin != prim.scamin ||
        fr.rgba != prim.color.rgba())
      flushFills();
    if (!fr.parent) {
      fr.parent = parent;
      fr.scamin = prim.scamin;
      fr.rgba = prim.color.rgba();
    }
    fr.tris.append(tris);
    ++nFillPrims;
  }
  flushFills();
  flushLines();
  if (qEnvironmentVariableIsSet("OCPN_QT_SG_STATS"))
    qInfo("provider: PERF-3 merge -- %d fill prims -> %d nodes, "
          "%d line prims -> %d nodes",
          nFillPrims, nFillNodes, nLinePrims, nLineNodes);

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
    if (catCulled(pf.dispCat, pf.classIdx) || pf.tris.isEmpty() || pf.pattern.isNull() ||
        !window)
      continue;
    QSGTexture* tex = root->texture(pf.pattern);  // cache-owned, deduped
    if (!tex) continue;
    // AreaPatternMaterial tiles the pattern per-fragment from a world offset
    // (rebuildPatternUVs writes the offset into the texcoord and sets kScale),
    // so the stipple survives finely-tessellated areas where per-vertex UV +
    // QSGTexture::Repeat collapsed each sub-tile polygon to one flat texel.
    auto* node = makeAreaPatternNode(tex, static_cast<int>(pf.tris.size()));
    // Pattern fills belong to the fill layer (after solid fills, before lines)
    // and are view-cullable like solid fills.
    double pminx = 1e18, pminy = 1e18, pmaxx = -1e18, pmaxy = -1e18;
    for (const QPointF& tp : pf.tris) {
      const double wx = tp.x(), wy = Viewport::latToWorldY(tp.y());
      pminx = std::min(pminx, wx); pmaxx = std::max(pmaxx, wx);
      pminy = std::min(pminy, wy); pmaxy = std::max(pmaxy, wy);
    }
    appendMaybeScamin(tileParent(patternTiles, patternUnderlay, patternGrid,
                                 pminx, pminy, pmaxx, pmaxy),
                      node, pf.scamin);  // SCAMIN-cull when it carries one (P2.14)

    const qreal dpr =
        pf.pattern.devicePixelRatio() > 0 ? pf.pattern.devicePixelRatio() : 1.0;
    PatternGeom pg;
    pg.node = node;
    pg.tris = pf.tris;
    // Vector patterns carry their own tiling period (a staggered tile's image
    // is baked double-height, so the image dimensions are NOT the period);
    // raster patterns leave tileW/H 0 and fall back to the image size.
    pg.tileW = pf.tileW > 0.0 ? pf.tileW : pf.pattern.width() / dpr;
    pg.tileH = pf.tileH > 0.0 ? pf.tileH : pf.pattern.height() / dpr;
    m_patterns.append(pg);
  }

  // Complex (LC) lines: one node per glyph-line; geometry is (re)generated by
  // rebuildComplexLines on each scale change (screen-fixed glyph walked along
  // the path). Drawn over fills, under the point symbols/labels.
  m_complex_lines.clear();
  for (const s52sg::ComplexLine& cl : m_buffer.complexLines) {
    if (catCulled(cl.dispCat, cl.classIdx) || cl.path.size() < 2 ||
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

  // S-52 over-scale hatch (A): a thin grey vertical-line set over the whole
  // cell bbox, shown by rebuildOverscaleHatch only when the display is zoomed
  // finer than the cell's native scale. Inside `content` (the cell's clipped
  // subtree) so a finer cell on top hides it; appended before the billboards so
  // names/symbols stay legible over it. Geometry is filled by the relayout.
  {
    auto* hatch = sg::makeFlatColorNode(QColor(120, 120, 120, 140),
                                        QSGGeometry::DrawLines, 0);
    m_overscale_hatch = new QSGOpacityNode();
    m_overscale_hatch->setOpacity(0.0);  // hidden until relayout decides
    m_overscale_hatch->appendChildNode(hatch);
    content->appendChildNode(m_overscale_hatch);
  }

  // Billboarded point items: one QSGTransformNode (placed at the world
  // anchor, counter-scaled per viewport) wrapping a textured quad. Built
  // once; the per-frame passes only touch the transform. PERF-4: texture
  // assignment is DEFERRED -- images collect in `atlasPending` and are
  // shelf-packed into shared atlas pages after the loops, so a cell
  // carries a handful of textures instead of one per label/symbol.
  m_billboards.clear();
  struct PendingAtlasImage {
    QSGImageNode* node;
    QImage image;
  };
  QList<PendingAtlasImage> atlasPending;
  auto addBillboard = [&](const QImage& image, QPointF worldPos,
                          QPointF pivotPx, int scamin, BbKind kind,
                          float depth, double rotationDeg = 0.0,
                          int viewGroup = 0, bool upright = false,
                          const QString& text = QString()) {
    if (image.isNull() || !window) return;
    const qreal dpr = image.devicePixelRatio() > 0 ? image.devicePixelRatio()
                                                    : 1.0;
    const qreal w = image.width() / dpr;
    const qreal h = image.height() / dpr;
    // The rect-centre offset from the anchor (declutter uses this regardless of
    // mode). For an upright label the image is instead drawn CENTRED on the node
    // origin and this offset is carried in the per-frame matrix (so it rotates
    // with the chart while the glyph counter-rotates upright); for a symbol the
    // offset is baked straight into the rect and the glyph rotates with chart.
    const QPointF centreOff(w / 2.0 - pivotPx.x(), h / 2.0 - pivotPx.y());
    auto* img = window->createImageNode();
    img->setOwnsTexture(false);  // TextureCacheNode (root) owns it
    atlasPending.append({img, image});
    if (upright)
      img->setRect(QRectF(-w / 2.0, -h / 2.0, w, h));  // centred; offset in mtx
    else
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
    root->appendChildNode(opacity);  // unclipped: never sliced by the cell bbox
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
    b.screenCx = static_cast<float>(centreOff.x());
    b.screenCy = static_cast<float>(centreOff.y());
    b.upright = upright;
    b.text = text;
    m_billboards.append(b);
  };

  // Point symbols (buoys/beacons) -- pivot is the symbol's hot-spot. Drawn
  // BEFORE text labels so a town/feature dot sits UNDER its name (e.g. the
  // "East Oakland" POPL dot), not over it.
  for (const s52sg::Symbol& sym : m_buffer.symbols) {
    if (catCulled(sym.dispCat, sym.classIdx)) continue;
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
    if (catCulled(vs.dispCat, vs.classIdx)) continue;
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
    root->appendChildNode(opacity);  // unclipped: a CARC arc is never sliced
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
    if (catCulled(lab.dispCat, lab.classIdx)) continue;
    // Viewing-group filter: soundings vs. other text (names), each
    // independently toggleable (mirrors s52plib's ShowSoundings /
    // ShowS57Text).
    if (lab.isSounding ? !m_showSoundings : !m_showText) continue;
    // Soundings get the unit-aware S-52 SNDFRM layout (integer + subscript
    // tenths, drying-height underline, safety-depth emphasis); other labels
    // (feature names) render as plain text.
    QImage img =
        lab.isSounding
            ? renderSoundingImage(
                  lab.depth, m_depth_unit, m_safety_depth_m,
                  lab.pointSize * static_cast<float>(m_sounding_scale),
                  lab.soundingSwept, lab.soundingLowAccuracy)
            : renderLabelImage(lab);
    const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
    const qreal w = img.width() / dpr;
    const qreal h = img.height() / dpr;
    // Soundings stay centred on their position; other text (names, light
    // descriptions, clearances) uses the S-52 justification + offsets so it
    // sits clear of the symbol instead of stacked on top of it.
    const QPointF pivot =
        lab.isSounding ? QPointF(w / 2.0, h / 2.0) : labelPivot(w, h, lab);
    addBillboard(img, QPointF(lab.pos.x(), Viewport::latToWorldY(lab.pos.y())),
                 pivot, lab.scamin,
                 lab.isSounding ? BbKind::Sounding : BbKind::Label, lab.depth,
                 /*rotationDeg=*/0.0, lab.viewGroup, /*upright=*/true,
                 lab.isSounding ? QString() : lab.text);
  }

  // PERF-4: pack the deferred billboard images into shared atlas pages.
  // Shelf packing, 2 px transparent gutter against linear-filter bleed;
  // dedup by QImage cacheKey (same dedup the per-image cache applied).
  // Oversized images (bigger than a page) keep a dedicated texture.
  {
    constexpr int kAtlasDim = 2048;  // device px per page side
    constexpr int kPad = 2;
    struct Placement {
      int page;
      QRect rect;  // device px within the page
    };
    QHash<qint64, Placement> placed;
    QList<QImage> pages;
    int cx = 0, cy = 0, rowH = 0;  // shelf cursor on the last page
    auto place = [&](const QImage& im) -> Placement {
      const int w = im.width(), h = im.height();
      if (!pages.isEmpty() && cx + w > kAtlasDim) {  // next shelf
        cx = 0;
        cy += rowH + kPad;
        rowH = 0;
      }
      if (pages.isEmpty() || cy + h > kAtlasDim) {  // next page
        pages.append(QImage(kAtlasDim, kAtlasDim,
                            QImage::Format_ARGB32_Premultiplied));
        pages.last().fill(Qt::transparent);
        cx = 0;
        cy = 0;
        rowH = 0;
      }
      const Placement p{static_cast<int>(pages.size()) - 1,
                        QRect(cx, cy, w, h)};
      QPainter painter(&pages[p.page]);
      painter.setCompositionMode(QPainter::CompositionMode_Source);
      painter.drawImage(p.rect.topLeft(), im);
      painter.end();
      cx += w + kPad;
      rowH = std::max(rowH, h);
      return p;
    };
    int oversized = 0;
    for (const PendingAtlasImage& p : atlasPending) {
      if (p.image.width() > kAtlasDim || p.image.height() > kAtlasDim) {
        ++oversized;
        continue;
      }
      const qint64 key = p.image.cacheKey();
      if (!placed.contains(key)) placed.insert(key, place(p.image));
    }
    QVarLengthArray<QSGTexture*, 4> pageTex;
    for (const QImage& pg : pages) pageTex.append(root->texture(pg));
    for (const PendingAtlasImage& p : atlasPending) {
      const auto it = placed.constFind(p.image.cacheKey());
      QSGTexture* tex = it != placed.cend() ? pageTex[it->page]
                                            : root->texture(p.image);
      if (Q_UNLIKELY(!tex)) {  // creation failed: draw nothing
        p.node->setRect(QRectF());
        continue;
      }
      // Order matters: setTexture may reset the source rect to the full
      // texture, so the sub-rect is applied after it.
      p.node->setTexture(tex);
      if (it != placed.cend()) p.node->setSourceRect(QRectF(it->rect));
    }
    if (qEnvironmentVariableIsSet("OCPN_QT_SG_STATS"))
      qInfo("provider: PERF-4 atlas -- %lld billboard images "
            "(%lld unique) -> %lld pages, %d oversized",
            static_cast<long long>(atlasPending.size()),
            static_cast<long long>(placed.size()),
            static_cast<long long>(pages.size()), oversized);
  }

  // Initial layout: declutter (sets `kept` + counter-scale on every kept
  // billboard) then view-cull to the current view. m_emit_scale is seeded so the
  // first viewport change after this build is correctly classified as pan vs zoom.
  recomputeDeclutter(viewport);
  {
    const QRectF worldView =
        viewport.visibleWorldBounds(kBillboardCullMarginPx);
    applyBillboardVisibility(viewport.scale(), viewport.rotation(), worldView);
    applyPrimCull(worldView);
  }
  m_emit_scale = viewport.scale();
  m_built = true;
  return root;
}

}  // namespace ocpn::qtui
