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

#include <cmath>
#include <cstring>
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
      break;
    case 2:  // night
      m_sea = QColor(18, 28, 44);
      m_land = QColor(34, 32, 27);
      m_coast = QColor(48, 44, 36);
      break;
    default:  // day
      m_sea = QColor(170, 195, 220);
      m_land = QColor(225, 213, 180);
      m_coast = QColor(120, 110, 90);
      break;
  }
  Q_EMIT changed();
}

void ShapefileBasemapProvider::load(const QString& shp_path) {
  shp::ShapefileReader reader(shp_path.toStdString());
  if (!reader.isOpen()) {
    qWarning("Basemap: cannot open %s", qPrintable(shp_path));
    return;
  }
  QElapsedTimer timer;
  timer.start();

  int rings = 0;

  // libtess2 consumes (and frees) its mesh inside each tessTesselate() call,
  // so a single tessellator cannot be asked for both the filled polygons and
  // the boundary contours -- the second pass would run on a NULL mesh and
  // produce nothing (or stale garbage). Keep the feature's rings and run each
  // pass on its own freshly-fed tessellator, from the SAME even-odd contours,
  // so the fill triangles and the boundary loops describe the identical
  // region and the shade/outline ride the tan land/sea edge exactly.
  std::vector<QList<float>> feature_contours;  // interleaved x,y per ring

  for (const auto& feature : reader) {
    auto* poly = static_cast<shp::Polygon*>(feature.getGeometry());
    if (!poly) continue;

    // Collect every ring of this polygon (outer + holes) as interleaved
    // world coords; tessellate the feature together so lakes punch through.
    feature_contours.clear();
    for (const shp::Ring& ring : poly->getRings()) {
      const std::vector<shp::Point>& pts = ring.getPoints();
      if (pts.size() < 3) continue;
      QList<float> contour;
      contour.reserve(static_cast<int>(pts.size()) * 2);
      for (const shp::Point& p : pts) {
        contour.append(static_cast<float>(p.getX()));    // lon
        contour.append(
            static_cast<float>(Viewport::latToWorldY(p.getY())));  // Mercator
      }
      feature_contours.push_back(std::move(contour));
      ++rings;
    }
    if (feature_contours.empty()) continue;

    const auto addContours = [&](TESStesselator* t) {
      for (const QList<float>& c : feature_contours)
        tessAddContour(t, 2, c.constData(), sizeof(float) * 2, c.size() / 2);
    };

    // Even-odd fill so holes (lakes) subtract regardless of ring direction.
    TESStesselator* fill = tessNewTess(nullptr);
    addContours(fill);
    if (tessTesselate(fill, TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, nullptr)) {
      const float* verts = tessGetVertices(fill);
      const TESSindex* elems = tessGetElements(fill);
      const int ne = tessGetElementCount(fill);
      for (int i = 0; i < ne; ++i) {
        for (int j = 0; j < 3; ++j) {
          const TESSindex idx = elems[i * 3 + j];
          if (idx == TESS_UNDEF) break;
          m_land_tris.append(QPointF(verts[idx * 2], verts[idx * 2 + 1]));
        }
      }
    }
    tessDeleteTess(fill);

    // Single source of truth for the coast outline + shade: ask libtess2 for
    // the BOUNDARY CONTOURS of the *filled* region under the SAME even-odd
    // rule used for the fill above. These loops are exactly the fill's edge
    // (holes punched, overlapping/shared ring edges merged), so the outline
    // and the inland shade ride the tan land/sea boundary precisely instead
    // of the raw input rings, which can diverge from the merged fill edge.
    // A separate tessellator is required because the fill pass already
    // destroyed its mesh.
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
        m_coastlines.append(std::move(loop));
      }
    }
    tessDeleteTess(bound);
  }

  m_loaded = !m_land_tris.isEmpty();
  qWarning("Basemap: %d rings, %lld triangles in %lld ms", rings,
           (long long)(m_land_tris.size() / 3), (long long)timer.elapsed());
}

QSGNode* ShapefileBasemapProvider::renderChart(QSGNode* old_subtree,
                                               const Viewport& /*viewport*/,
                                               QQuickWindow* /*window*/) {
  // Static geometry -- build once; the viewport transform reprojects it.
  if (old_subtree) return old_subtree;
  if (!m_loaded) return nullptr;

  auto* root = new QSGNode();

  // 1. Sea backdrop quad over the whole world (drawn first).
  {
    auto* sea = sg::makeFlatColorNode(m_sea, QSGGeometry::DrawTriangles, 6);
    QSGGeometry::Point2D* v = sea->geometry()->vertexDataAsPoint2D();
    // World Y spans the clamped Mercator range (lat +/-kMercMaxLat).
    const float xl = -180, xr = 180;
    const float yt = static_cast<float>(Viewport::latToWorldY(90.0));
    const float yb = static_cast<float>(Viewport::latToWorldY(-90.0));
    v[0].set(xl, yt); v[1].set(xr, yt); v[2].set(xr, yb);
    v[3].set(xl, yt); v[4].set(xr, yb); v[5].set(xl, yb);
    root->appendChildNode(sea);
  }

  // 2. Land fill: the tessellated triangle list.
  {
    auto* land = sg::makeFlatColorNode(m_land, QSGGeometry::DrawTriangles,
                                       m_land_tris.size());
    QSGGeometry::Point2D* v = land->geometry()->vertexDataAsPoint2D();
    for (int i = 0; i < m_land_tris.size(); ++i)
      v[i].set(static_cast<float>(m_land_tris[i].x()),
               static_cast<float>(m_land_tris[i].y()));
    root->appendChildNode(land);
  }

  // 3. Inland shade: a soft gradient band just inside the coast (darkening
  //    fading to transparent ~6px inland) so land lifts off the water. Built
  //    from the same fill-boundary loops as the outline (and the fill), so its
  //    coastal edge sits exactly on the land/sea boundary. Skip grid clip
  //    edges so the tile boundaries aren't shaded.
  if (auto* shade = makeCoastShadeNode(
          m_coastlines, QColor(0, 0, 0), /*width_px=*/6.0f, /*max_alpha=*/0.38f,
          [](const QPointF& a, const QPointF& b) { return !isGridEdge(a, b); }))
    root->appendChildNode(shade);

  // 4. Coastline outline: real-coast segments only (grid clip edges skipped),
  //    so the basemap shows a clean coast and no tile grid.
  {
    std::vector<QSGGeometry::Point2D> seg;
    for (const auto& c : m_coastlines) {
      const int n = c.size();
      if (n < 2) continue;
      for (int i = 0; i < n; ++i) {
        const QPointF& a = c[i];
        const QPointF& b = c[(i + 1) % n];  // wrap to close
        if (isGridEdge(a, b)) continue;     // skip tile-grid clip edges
        QSGGeometry::Point2D pa, pb;
        pa.set(static_cast<float>(a.x()), static_cast<float>(a.y()));
        pb.set(static_cast<float>(b.x()), static_cast<float>(b.y()));
        seg.push_back(pa);
        seg.push_back(pb);
      }
    }
    if (!seg.empty()) {
      auto* coast = sg::makeFlatColorNode(m_coast, QSGGeometry::DrawLines,
                                          static_cast<int>(seg.size()));
      std::memcpy(coast->geometry()->vertexData(), seg.data(),
                  seg.size() * sizeof(QSGGeometry::Point2D));
      root->appendChildNode(coast);
    }
  }

  return root;
}

}  // namespace ocpn::qtui
