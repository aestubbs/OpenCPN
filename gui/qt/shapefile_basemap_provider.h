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
 * ShapefileBasemapProvider -- the always-on world background chart, built
 * from OpenCPN's bundled shapefile basemap (data/basemap_shp/basemap_low.shp,
 * with basemap_crude_10x10.shp as a fallback). This is the "default world
 * map" the wx app uses -- proper land polygons, far higher resolution than
 * the crude GSHHS poly file and with no 1°-cell square artifacts.
 *
 * Reads the shapefile via libs/ShapefileCpp (shp::ShapefileReader), emitting
 * world-coordinate geometry: a sea backdrop quad, tessellated land fill
 * (libtess2), and the polygon ring contours kept for the coastline outline +
 * the land-shade pass (P3.x). Static in world space (x = lon, y = -lat), so
 * the viewport transform does all projection -- no re-decode on pan/zoom.
 *
 * Ring winding (shapefile convention: outer rings clockwise, holes
 * counter-clockwise) identifies the land side for the inland shade.
 */

#ifndef OCPN_QT_SHAPEFILE_BASEMAP_PROVIDER_H_
#define OCPN_QT_SHAPEFILE_BASEMAP_PROVIDER_H_

#include <vector>

#include <QColor>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSet>
#include <QString>

#include "chart_provider.h"

namespace ocpn::qtui {

class ShapefileBasemapProvider : public ChartProvider {
  Q_OBJECT

public:
  /** `shp_path` is the absolute path to the basemap .shp. Read + tessellated
   *  once here on the calling (main) thread. */
  explicit ShapefileBasemapProvider(const QString& shp_path,
                                    QObject* parent = nullptr);

  QString id() const override { return QStringLiteral("world.basemap"); }
  QString name() const override { return QStringLiteral("World background"); }
  double northLat() const override { return 90.0; }
  double southLat() const override { return -90.0; }
  double westLon() const override { return -180.0; }
  double eastLon() const override { return 180.0; }

  QSGNode* renderChart(QSGNode* old_subtree, const Viewport& viewport,
                       QQuickWindow* window) override;

  /** Re-tint the basemap for the display colour scheme (0=day,1=dusk,
   *  2=night) and request a rebuild, so the world backdrop matches the S-52
   *  palette. */
  void setColorScheme(int scheme);

  /** NODATA mode (ECDIS): when on, the whole backdrop (sea + land) is painted
   *  the S-52 "no data" grey instead of the cartographic sea/land tint, so
   *  wherever an ENC cell does NOT draw over it the mariner sees the standard
   *  unsurveyed/no-coverage fill rather than a friendly world map. Off by
   *  default (the basemap stays a roamable world map). Rebuilds on change. */
  void setNoDataMode(bool on);
  bool noDataMode() const { return m_nodata; }

  /** Land fill-boundary contours (world coords, x=lon y=-lat), each a closed
   *  loop. These are libtess2's BOUNDARY_CONTOURS of the filled region (same
   *  even-odd rule as the fill), so they coincide exactly with the land/sea
   *  fill edge -- the single source of truth shared by the coast outline and
   *  the inland-shade pass. */
  const QList<QList<QPointF>>& coastlines() const { return m_lod_full.coastlines; }

  /** Wire the viewport: the basemap submits only VIEW-INTERSECTING 15-deg
   *  tiles of its (multi-million-vertex) world geometry, and re-emits
   *  changed() when panning crosses a tile boundary. Without this, every
   *  scene change made Qt's batch renderer re-copy ~3M world vertices --
   *  the ~1 s pan-into-new-cell hitch (PERF-6 measurement). */
  void setViewport(const Viewport* vp);

private:
  // One level-of-detail: tessellated world geometry, bucketed into the 15-deg
  // tile grid (PERF-6, index ty*kTilesX+tx) so a rebuild submits only the
  // visible tiles. Both tiers come from the SAME source shapefile (basemap_low):
  // the FULL tier is the real coastline; the COARSE tier is the same coastline
  // with its ring points Douglas-Peucker-culled, then re-tessellated -- so the
  // fill triangles AND the outline thin out together, faithfully (no separate
  // crude file to "pop" to). Drawn zoomed out, where the fine coastline is
  // invisible, GPU-bound, and piles into a "thick" smudge in intricate areas.
  struct Lod {
    QList<QList<QPointF>> coastlines;            // fill-boundary loops, world
    QHash<int, QList<QPointF>> tile_tris;        // per-tile land triangle list
    QHash<int, QList<QPointF>> tile_coast_segs;  // per-tile (a,b) seg pairs
    bool loaded = false;
  };
  // A polygon feature = its rings as interleaved world (x=lon, y=Mercator).
  using Feature = std::vector<QList<float>>;
  void load(const QString& detail_path);  // reads source, builds both LOD tiers
  // Tessellate features (each ring DP-simplified by `eps` world-units; 0 = full
  // detail) + bucket into tiles -> out. Static so it touches no per-instance
  // state and can run from the shared-geometry cache.
  static void buildTier(const std::vector<Feature>& features, double eps,
                        Lod& out, const char* label);
  const Lod& activeLod() const;  // full or coarse, by current zoom
  bool useCoarse() const;        // true when the coarse tier should be drawn
  QSet<int> visibleTiles() const;  // tile indices intersecting the view

  Lod m_lod_full;    // real coastline -- drawn zoomed in
  Lod m_lod_coarse;  // point-culled + re-tessellated -- drawn zoomed out
  const Viewport* m_vp = nullptr;
  QSet<int> m_attached;            // tile set of the last build
  bool m_attached_coarse = false;  // LOD of the last build (detect crossover)
  QColor m_sea{212, 234, 238};   // S-52 DEPDW day_bright -- match ENC deep water
  QColor m_land{201, 185, 122};  // S-52 LANDA day_bright -- match the ENC land
  QColor m_coast{120, 110, 90};
  bool m_nodata = false;  // paint backdrop S-52 NODATA grey (ECDIS toggle)
  // S-52 NODTA fill (the no-coverage grey); land/coast slightly darker so the
  // coastline still reads when the backdrop is grey.
  QColor m_nodata_fill{200, 200, 200};
  QColor m_nodata_coast{150, 150, 150};
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SHAPEFILE_BASEMAP_PROVIDER_H_
