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
  const QList<QList<QPointF>>& coastlines() const { return m_coastlines; }

  /** Wire the viewport: the basemap submits only VIEW-INTERSECTING 15-deg
   *  tiles of its (multi-million-vertex) world geometry, and re-emits
   *  changed() when panning crosses a tile boundary. Without this, every
   *  scene change made Qt's batch renderer re-copy ~3M world vertices --
   *  the ~1 s pan-into-new-cell hitch (PERF-6 measurement). */
  void setViewport(const Viewport* vp);

private:
  void load(const QString& shp_path);
  QSet<int> visibleTiles() const;  // tile indices intersecting the view

  QList<QPointF> m_land_tris;            // (x=lon, y=-lat) triangle list
  QList<QList<QPointF>> m_coastlines;    // fill-boundary loops, world coords
  // PERF-6: per-tile buckets of the world geometry (15-deg grid, index
  // ty*kTilesX+tx). Built once at load; renderChart concatenates only the
  // visible tiles' buckets.
  QHash<int, QList<QPointF>> m_tile_tris;
  QHash<int, QList<QPointF>> m_tile_coast_segs;  // flattened (a,b) pairs
  const Viewport* m_vp = nullptr;
  QSet<int> m_attached;  // tile set of the last build
  QColor m_sea{170, 195, 220};
  QColor m_land{225, 213, 180};
  QColor m_coast{120, 110, 90};
  bool m_loaded = false;
  bool m_nodata = false;  // paint backdrop S-52 NODATA grey (ECDIS toggle)
  // S-52 NODTA fill (the no-coverage grey); land/coast slightly darker so the
  // coastline still reads when the backdrop is grey.
  QColor m_nodata_fill{200, 200, 200};
  QColor m_nodata_coast{150, 150, 150};
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SHAPEFILE_BASEMAP_PROVIDER_H_
