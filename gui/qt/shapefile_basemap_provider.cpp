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
 * Implement shapefile_basemap_provider.h.
 */

#include "shapefile_basemap_provider.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include <QElapsedTimer>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>

#include "Feature.hpp"
#include "Polygon.hpp"
#include "Ring.hpp"
#include "ShapefileReader.hpp"
#include "coast_shade.h"
#include "sg_helpers.h"
#include "tesselator.h"
#include "viewport.h"

namespace ocpn::qtui {

namespace {
// The basemap polygons are clipped to a lat/lon grid (on integer degrees);
// those clip edges are axis-aligned segments lying on a grid line and are
// NOT real coastline. Detect them so we can skip outlining + shading them
// (the wx app sidesteps this by only filling, never stroking the rings).
// World coords: x = lon (whole-degree meridians stay integer), but y is now
// Mercator: y = latToWorldY(lat), which is NOT integer at integer latitude.
// So the horizontal (parallel) clip edges must be tested back in LATITUDE
// space via worldYToLat -- testing the raw world-Y (as before the Mercator
// change) no longer recognised them, so every whole-degree parallel clip edge
// leaked into the outline/shade passes as a dark horizontal line.
inline bool isGridEdge(const QPointF& a, const QPointF& b) {
  constexpr double kAxis = 1e-4;   // treat as axis-aligned
  constexpr double kGrid = 3e-3;   // distance to a whole-degree grid line (deg)
  const auto nearInt = [](double v) {
    return std::abs(v - std::round(v)) < kGrid;
  };
  // Vertical clip edge: x ~constant on a tile meridian. The basemap tile cuts
  // are loxodromes (basemap_low.shp), so the longitude bows away from the whole
  // degree toward the poles (meridian convergence) -- the offset crosses kGrid
  // around |lat| 80, which is why uncorrected meridian cuts leak as dark
  // vertical lines in the far N/S but not near the equator. Widen the tolerance
  // by sec(lat) (using the edge midpoint's latitude) so high-latitude meridian
  // cuts are still recognised, while staying tight near the equator.
  if (std::abs(a.x() - b.x()) < kAxis) {
    const double lat = Viewport::worldYToLat(0.5 * (a.y() + b.y()));
    const double sec = 1.0 / std::max(0.05, std::cos(lat * M_PI / 180.0));
    if (std::abs(a.x() - std::round(a.x())) < kGrid * sec) return true;
  }
  // Horizontal clip edge: world-Y constant; map back to latitude (Mercator
  // world-Y is non-integer at integer latitude) and test for a whole degree.
  if (std::abs(a.y() - b.y()) < kAxis && nearInt(Viewport::worldYToLat(a.y())))
    return true;
  return false;
}

// PERF-6 tile grid: 15-degree columns x 12 Mercator-Y rows.
constexpr int kTilesX = 24;
constexpr int kTilesY = 12;
inline int tileIndex(const QPointF& w) {
  const double yTop = Viewport::latToWorldY(90.0);
  const double yBot = Viewport::latToWorldY(-90.0);
  const int tx = qBound(0, int((w.x() + 180.0) / (360.0 / kTilesX)),
                        kTilesX - 1);
  const int ty =
      qBound(0, int((w.y() - yTop) / ((yBot - yTop) / kTilesY)), kTilesY - 1);
  return ty * kTilesX + tx;
}

// Douglas-Peucker point culling for one ring (interleaved x,y world coords),
// keeping the endpoints. `eps` is the max allowed perpendicular deviation in
// world units; points whose removal moves the line by less than eps are
// dropped. This is the coarse-LOD coastline simplification -- it thins dense
// coastline (fjords, river deltas) uniformly while leaving simple coast
// untouched, so the re-tessellated land keeps the real shape with far fewer
// points. O(n log n) typical via the split stack.
QList<float> simplifyRing(const QList<float>& pts, double eps) {
  const int n = pts.size() / 2;
  if (n < 3 || eps <= 0.0) return pts;
  std::vector<bool> keep(n, false);
  keep[0] = keep[n - 1] = true;
  const double eps2 = eps * eps;
  std::vector<std::pair<int, int>> stack;
  stack.emplace_back(0, n - 1);
  while (!stack.empty()) {
    const auto [a, b] = stack.back();
    stack.pop_back();
    if (b <= a + 1) continue;
    const double ax = pts[a * 2], ay = pts[a * 2 + 1];
    const double dx = pts[b * 2] - ax, dy = pts[b * 2 + 1] - ay;
    const double seg2 = dx * dx + dy * dy;
    double maxD2 = -1.0;
    int idx = -1;
    for (int i = a + 1; i < b; ++i) {
      const double px = pts[i * 2] - ax, py = pts[i * 2 + 1] - ay;
      double d2;
      if (seg2 <= 1e-20) {
        d2 = px * px + py * py;  // degenerate segment: distance to A
      } else {
        const double t = std::clamp((px * dx + py * dy) / seg2, 0.0, 1.0);
        const double ex = px - t * dx, ey = py - t * dy;
        d2 = ex * ex + ey * ey;
      }
      if (d2 > maxD2) {
        maxD2 = d2;
        idx = i;
      }
    }
    if (maxD2 > eps2 && idx > 0) {
      keep[idx] = true;
      stack.emplace_back(a, idx);
      stack.emplace_back(idx, b);
    }
  }
  QList<float> out;
  out.reserve(n * 2);
  for (int i = 0; i < n; ++i)
    if (keep[i]) {
      out.append(pts[i * 2]);
      out.append(pts[i * 2 + 1]);
    }
  return out;
}
}  // namespace

ShapefileBasemapProvider::ShapefileBasemapProvider(const QString& shp_path,
                                                   QObject* parent)
    : ChartProvider(parent) {
  load(shp_path);
}

void ShapefileBasemapProvider::setColorScheme(int scheme) {
  // Tint the backdrop to roughly track the S-52 day/dusk/night palettes so
  // the world map doesn't glare beneath dimmed charts.
  switch (scheme) {
    case 1:  // dusk
      m_sea = QColor(70, 92, 120);
      m_land = QColor(110, 104, 86);
      m_coast = QColor(70, 64, 52);
      m_nodata_fill = QColor(120, 120, 120);
      m_nodata_coast = QColor(90, 90, 90);
      break;
    case 2:  // night
      m_sea = QColor(18, 28, 44);
      m_land = QColor(34, 32, 27);
      m_coast = QColor(48, 44, 36);
      m_nodata_fill = QColor(40, 40, 40);
      m_nodata_coast = QColor(64, 64, 64);
      break;
    default:  // day
      m_sea = QColor(212, 234, 238);   // S-52 DEPDW day_bright (match deep water)
      m_land = QColor(201, 185, 122);  // S-52 LANDA day_bright (match ENC land)
      m_coast = QColor(120, 110, 90);
      m_nodata_fill = QColor(200, 200, 200);
      m_nodata_coast = QColor(150, 150, 150);
      break;
  }
  emit changed();
}

void ShapefileBasemapProvider::setNoDataMode(bool on) {
  if (on == m_nodata) return;
  m_nodata = on;
  emit changed();  // forces a rebuild (renderChart re-tints the backdrop)
}

void ShapefileBasemapProvider::buildTier(const std::vector<Feature>& features,
                                         double eps, Lod& out,
                                         const char* label) {
  QElapsedTimer timer;
  timer.start();
  QList<QPointF> land_tris;  // (x=lon, y=Mercator) triangle list (this tier)
  int rings = 0;

  // libtess2 consumes (and frees) its mesh inside each tessTesselate() call,
  // so a single tessellator cannot be asked for both the filled polygons and
  // the boundary contours -- the second pass would run on a NULL mesh. Run each
  // pass on its own freshly-fed tessellator, from the SAME (eps-simplified)
  // contours, so the fill triangles and the boundary loops describe the
  // identical region and the shade/outline ride the land/sea edge exactly.
  std::vector<QList<float>> contours;  // this feature's simplified rings
  for (const Feature& feat : features) {
    contours.clear();
    for (const QList<float>& ring : feat) {
      // Simplify in lat/lon (uniform degrees), THEN project to world Mercator.
      QList<float> c = eps > 0.0 ? simplifyRing(ring, eps) : ring;
      if (c.size() < 6) continue;  // need >= 3 points to tessellate
      for (int i = 1; i < c.size(); i += 2)
        c[i] = static_cast<float>(Viewport::latToWorldY(c[i]));  // lat -> Merc
      contours.push_back(std::move(c));
      ++rings;
    }
    if (contours.empty()) continue;

    const auto addContours = [&](TESStesselator* t) {
      for (const QList<float>& c : contours)
        tessAddContour(t, 2, c.constData(), sizeof(float) * 2, c.size() / 2);
    };

    // Even-odd fill so holes (lakes) subtract regardless of ring direction.
    TESStesselator* fill = tessNewTess(nullptr);
    addContours(fill);
    if (tessTesselate(fill, TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, nullptr)) {
      const float* verts = tessGetVertices(fill);
      const TESSindex* elems = tessGetElements(fill);
      const int ne = tessGetElementCount(fill);
      for (int i = 0; i < ne; ++i)
        for (int j = 0; j < 3; ++j) {
          const TESSindex idx = elems[i * 3 + j];
          if (idx == TESS_UNDEF) break;
          land_tris.append(QPointF(verts[idx * 2], verts[idx * 2 + 1]));
        }
    }
    tessDeleteTess(fill);

    // Boundary contours of the *filled* region (same even-odd rule) -- the
    // single source of truth for the outline + inland shade, so they ride the
    // merged land/sea edge rather than the raw rings. Separate tessellator
    // because the fill pass destroyed its mesh.
    TESStesselator* bound = tessNewTess(nullptr);
    addContours(bound);
    if (tessTesselate(bound, TESS_WINDING_ODD, TESS_BOUNDARY_CONTOURS, 0, 2,
                      nullptr)) {
      const float* verts = tessGetVertices(bound);
      const TESSindex* elems = tessGetElements(bound);
      const int nc = tessGetElementCount(bound);
      for (int i = 0; i < nc; ++i) {
        const TESSindex base = elems[i * 2];
        const TESSindex count = elems[i * 2 + 1];
        if (count < 3) continue;
        QList<QPointF> loop;
        loop.reserve(count);
        for (TESSindex j = 0; j < count; ++j)
          loop.append(QPointF(verts[(base + j) * 2], verts[(base + j) * 2 + 1]));
        out.coastlines.append(std::move(loop));
      }
    }
    tessDeleteTess(bound);
  }

  out.loaded = !land_tris.isEmpty();

  // PERF-6: bucket into the 15-degree tile grid so a rebuild submits only the
  // visible tiles. Triangles bucket by centroid; coast segments by midpoint
  // (grid clip edges dropped here -- neither outline nor shade draws them).
  for (int i = 0; i + 2 < land_tris.size(); i += 3) {
    const QPointF c =
        (land_tris[i] + land_tris[i + 1] + land_tris[i + 2]) / 3.0;
    QList<QPointF>& bucket = out.tile_tris[tileIndex(c)];
    bucket.append(land_tris[i]);
    bucket.append(land_tris[i + 1]);
    bucket.append(land_tris[i + 2]);
  }
  for (const auto& loop : out.coastlines) {
    const int n = loop.size();
    for (int i = 0; i < n; ++i) {
      const QPointF& a = loop[i];
      const QPointF& b = loop[(i + 1) % n];
      if (isGridEdge(a, b)) continue;
      QList<QPointF>& bucket = out.tile_coast_segs[tileIndex((a + b) / 2.0)];
      bucket.append(a);
      bucket.append(b);
    }
  }

  qWarning("Basemap[%s]: %d rings, %lld triangles in %lld ms (%lld tiles)",
           label, rings, (long long)(land_tris.size() / 3),
           (long long)timer.elapsed(), (long long)out.tile_tris.size());
}

void ShapefileBasemapProvider::load(const QString& detail_path) {
  // The basemap is STATIC world geometry, identical for every canvas, and
  // building both LOD tiers costs ~1.5 s + tens of MB. With a dual-pane layout
  // there are two ChartCanvas instances (Main.qml), each constructing its own
  // provider -- so cache the built (view-independent) tiers per source path and
  // reuse them: Qt's containers are copy-on-write, so the second canvas shares
  // the same storage at O(1) and never mutates it (renderChart only reads).
  // Both providers are constructed on the main thread, so the static cache
  // needs no lock; the data is read-only on the render thread thereafter.
  struct Cached {
    Lod full;
    Lod coarse;
  };
  static QHash<QString, Cached> s_cache;
  if (auto it = s_cache.constFind(detail_path); it != s_cache.constEnd()) {
    m_lod_full = it->full;
    m_lod_coarse = it->coarse;
    qWarning("Basemap: reused cached geometry for %s",
             qPrintable(detail_path));
    return;
  }

  shp::ShapefileReader reader(detail_path.toStdString());
  if (!reader.isOpen()) {
    qWarning("Basemap: cannot open %s", qPrintable(detail_path));
    return;
  }

  // Read every feature's rings ONCE into world coords; both LOD tiers are
  // tessellated from this (the coarse tier first DP-simplifies each ring).
  std::vector<Feature> features;
  for (const auto& feature : reader) {
    auto* poly = static_cast<shp::Polygon*>(feature.getGeometry());
    if (!poly) continue;
    Feature feat;
    for (const shp::Ring& ring : poly->getRings()) {
      const std::vector<shp::Point>& pts = ring.getPoints();
      if (pts.size() < 3) continue;
      QList<float> contour;
      contour.reserve(static_cast<int>(pts.size()) * 2);
      for (const shp::Point& p : pts) {
        contour.append(static_cast<float>(p.getX()));   // lon (degrees)
        contour.append(static_cast<float>(p.getY()));   // lat (degrees) -- the
        // Mercator transform is applied per-tier AFTER simplification (below),
        // so Douglas-Peucker runs in uniform lat/lon degrees and never perturbs
        // the whole-degree meridian clip-edges off-grid (the coastline whisker
        // artifact, worst near the poles where Mercator-Y is sec(lat)-stretched).
      }
      feat.push_back(std::move(contour));
    }
    if (!feat.empty()) features.push_back(std::move(feat));
  }

  // Coarse-tier point-cull tolerance (world units ~ degrees). Generous enough
  // to thin dense coastline at world/continental zoom but keep the shape;
  // env-tunable for Pi tuning.
  static const double kCoarseEps = [] {
    bool ok = false;
    const double v = qgetenv("OCPN_QT_BASEMAP_COARSE_EPS").toDouble(&ok);
    return ok && v > 0.0 ? v : 0.08;
  }();
  buildTier(features, 0.0, m_lod_full, "full");
  buildTier(features, kCoarseEps, m_lod_coarse, "coarse");

  s_cache.insert(detail_path, Cached{m_lod_full, m_lod_coarse});
}

bool ShapefileBasemapProvider::useCoarse() const {
  // Draw the coarse tier when zoomed out past this many logical pixels per
  // degree of longitude (m_vp->scale()). Below it the culled coastline is
  // indistinguishable from the full one but ~Nx cheaper to draw; env-tunable.
  static const double kCoarseBelowPxPerDeg = [] {
    bool ok = false;
    const double v = qgetenv("OCPN_QT_BASEMAP_COARSE_PXDEG").toDouble(&ok);
    return ok && v > 0.0 ? v : 20.0;
  }();
  return m_lod_coarse.loaded && m_vp &&
         m_vp->scale() < kCoarseBelowPxPerDeg;
}

const ShapefileBasemapProvider::Lod& ShapefileBasemapProvider::activeLod()
    const {
  return useCoarse() ? m_lod_coarse : m_lod_full;
}

void ShapefileBasemapProvider::setViewport(const Viewport* vp) {
  m_vp = vp;
  if (!m_vp) return;
  connect(m_vp, &Viewport::changed, this, [this] {
    // Re-render only when the visible tile/shift set or the LOD changes (a pan
    // within the same tiles costs nothing; the world-anchored transform pans).
    if (attachSig(visibleWrapped(), useCoarse()) != m_attach_sig) emit changed();
  });
}

ShapefileBasemapProvider::ShiftTiles ShapefileBasemapProvider::visibleWrapped()
    const {
  ShiftTiles out;
  if (!m_vp) {  // no viewport wired: whole world, no shift
    QSet<int> all;
    for (auto it = m_lod_full.tile_tris.cbegin();
         it != m_lod_full.tile_tris.cend(); ++it)
      all.insert(it.key());
    if (!all.isEmpty()) out.append({0.0, all});
    return out;
  }
  const QRectF r = m_vp->visibleWorldBounds(/*marginPx=*/512.0);
  if (r.isEmpty()) return out;
  // Defend ONLY against an un-sized canvas: visibleWorldBounds then returns a
  // ~2e9-wide rect, and the copy loop below would spin millions of longitude
  // copies and OOM. A real view is never more than the world plus a little, so
  // a generous cap separates the two; render one copy of all tiles in that case.
  if (r.width() > 2000.0) {
    QSet<int> all;
    for (auto it = m_lod_full.tile_tris.cbegin();
         it != m_lod_full.tile_tris.cend(); ++it)
      all.insert(it.key());
    if (!all.isEmpty()) out.append({0.0, all});
    return out;
  }
  const double yTop = Viewport::latToWorldY(90.0);
  const double yBot = Viewport::latToWorldY(-90.0);
  const double tileW = 360.0 / kTilesX;
  const double tileH = (yBot - yTop) / kTilesY;
  const int ty0 = qBound(0, int((r.top() - yTop) / tileH), kTilesY - 1);
  const int ty1 = qBound(0, int((r.bottom() - yTop) / tileH), kTilesY - 1);
  // Longitude copies whose world span [k*360-180, k*360+180] meets the view, so
  // the world repeats E-W across the antimeridian (continuous scroll). A view up
  // to ~360 deg wide spans at most ~2-3 copies; cap defensively regardless.
  const int k0 = static_cast<int>(std::ceil((r.left() - 180.0) / 360.0));
  int k1 = static_cast<int>(std::floor((r.right() + 180.0) / 360.0));
  if (k1 - k0 > 4) k1 = k0 + 4;
  for (int k = k0; k <= k1; ++k) {
    const double shift = k * 360.0;
    const double lonL = std::max(-180.0, r.left() - shift);
    const double lonR = std::min(180.0, r.right() - shift);
    if (lonR < lonL) continue;
    const int tx0 = qBound(0, int((lonL + 180.0) / tileW), kTilesX - 1);
    const int tx1 = qBound(0, int((lonR + 180.0) / tileW), kTilesX - 1);
    QSet<int> tiles;
    for (int ty = ty0; ty <= ty1; ++ty)
      for (int tx = tx0; tx <= tx1; ++tx) tiles.insert(ty * kTilesX + tx);
    if (!tiles.isEmpty()) out.append({shift, tiles});
  }
  return out;
}

QString ShapefileBasemapProvider::attachSig(const ShiftTiles& v, bool coarse) {
  QString s = coarse ? QStringLiteral("c") : QStringLiteral("f");
  for (const auto& st : v) {
    s += QLatin1Char(';');
    s += QString::number(static_cast<int>(st.first));
    s += QLatin1Char(':');
    QList<int> ts = st.second.values();
    std::sort(ts.begin(), ts.end());
    for (int t : ts) {
      s += QString::number(t);
      s += QLatin1Char(',');
    }
  }
  return s;
}

QSGNode* ShapefileBasemapProvider::renderChart(QSGNode* old_subtree,
                                               const Viewport& /*viewport*/,
                                               QQuickWindow* /*window*/) {
  // Geometry is static, but the SUBMITTED SUBSET follows the view: only
  // the visible 15-degree tiles' vertices enter the scene graph, so the
  // batch renderer's full rebuilds (every scene change) stop re-copying
  // ~3M world vertices -- the measured ~1 s pan-into-new-cell hitch.
  // setViewport's tile-set watcher re-dirties this layer when panning
  // crosses a tile boundary; within a tile set old_subtree is reused.
  const bool coarse = useCoarse();
  const ShiftTiles cfg = visibleWrapped();
  const QString sig = attachSig(cfg, coarse);
  if (old_subtree && sig == m_attach_sig) return old_subtree;
  const Lod& lod = activeLod();
  if (!lod.loaded) return nullptr;
  m_attach_sig = sig;

  auto* root = new QSGNode();

  // ECDIS NODATA mode: paint the whole backdrop (sea + land) the S-52 no-data
  // grey, so wherever an ENC cell doesn't draw over it the mariner sees the
  // standard no-coverage fill rather than the cartographic world map.
  const QColor seaCol = m_nodata ? m_nodata_fill : m_sea;
  const QColor landCol = m_nodata ? m_nodata_fill : m_land;
  const QColor coastCol = m_nodata ? m_nodata_coast : m_coast;

  // Visible longitude span (covers the antimeridian seam for continuous scroll).
  double viewL = -180.0, viewR = 180.0;
  if (m_vp) {
    const QRectF r = m_vp->visibleWorldBounds(/*marginPx=*/512.0);
    if (!r.isEmpty()) {
      viewL = r.left();
      viewR = r.right();
    }
  }

  // 1. Sea backdrop quad. Y spans the clamped Mercator range; X spans the whole
  //    visible longitude range so the wrapped (off-[-180,180]) part has a sea
  //    backdrop too.
  {
    auto* sea = sg::makeFlatColorNode(seaCol, QSGGeometry::DrawTriangles, 6);
    QSGGeometry::Point2D* v = sea->geometry()->vertexDataAsPoint2D();
    const float xl = static_cast<float>(viewL), xr = static_cast<float>(viewR);
    const float yt = static_cast<float>(Viewport::latToWorldY(90.0));
    const float yb = static_cast<float>(Viewport::latToWorldY(-90.0));
    v[0].set(xl, yt); v[1].set(xr, yt); v[2].set(xr, yb);
    v[3].set(xl, yt); v[4].set(xr, yb); v[5].set(xl, yb);
    root->appendChildNode(sea);
  }

  // 2. Land fill: the visible tiles' tessellated triangles, emitted in chunks
  //    no larger than the scene graph's 16-bit batch limit. A single node with
  //    all visible tiles' vertices (~1.86M at world zoom) renders on desktop/
  //    Metal RHI (32-bit indices) but is SILENTLY DROPPED on the Pi's V3D/Mesa
  //    GLES backend, where the batch renderer is bounded to 65535 vertices --
  //    so the land vanished and the map showed sea + coastline only. Chunking
  //    on a triangle boundary keeps every node drawable on both backends.
  {
    constexpr int kMaxVerts = 65532;  // < 65536 and a multiple of 3 (triangles)
    std::vector<QSGGeometry::Point2D> chunk;
    chunk.reserve(kMaxVerts);
    const auto flush = [&]() {
      if (chunk.empty()) return;
      auto* land = sg::makeFlatColorNode(landCol, QSGGeometry::DrawTriangles,
                                         static_cast<int>(chunk.size()));
      std::memcpy(land->geometry()->vertexData(), chunk.data(),
                  chunk.size() * sizeof(QSGGeometry::Point2D));
      root->appendChildNode(land);
      chunk.clear();
    };
    // Each longitude copy: its visible tiles, X-translated by the copy's shift
    // (0 for the main world, +/-360 for the wrapped seam) for continuous scroll.
    for (const auto& st : cfg) {
      const double shift = st.first;
      for (int t : st.second)
        for (const QPointF& p : lod.tile_tris.value(t)) {
          if (static_cast<int>(chunk.size()) >= kMaxVerts) flush();
          QSGGeometry::Point2D q;
          q.set(static_cast<float>(p.x() + shift), static_cast<float>(p.y()));
          chunk.push_back(q);
        }
    }
    flush();
  }

  // 3 + 4. Inland shade AND coastline outline: BOTH DROPPED. The basemap is now
  //    just sea + land FILL; the land/sea fill edge reads as a clean coastline
  //    at every zoom, and all coastal detail is left to the ENC chart cells.
  //    The stroked outline drew spurious thin-feature spikes (GSHHG river/sliver
  //    polygons -- fill invisible land-on-land, but the stroke showed as N-S
  //    "whiskers"); the inland alpha shade added a coastline darkening band that
  //    isn't wanted under the charts. (coastCol / lod.coastlines / tile_coast_segs
  //    are retained but unused here.)
  Q_UNUSED(coastCol);

  return root;
}

}  // namespace ocpn::qtui
