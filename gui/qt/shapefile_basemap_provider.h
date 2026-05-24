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
#include <QList>
#include <QPointF>
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

  /** Land-polygon ring contours (world coords, x=lon y=-lat), each closed.
   *  Used by the land-shade pass. */
  const QList<QList<QPointF>>& coastlines() const { return m_coastlines; }

private:
  void load(const QString& shp_path);

  QList<QPointF> m_land_tris;            // (x=lon, y=-lat) triangle list
  QList<QList<QPointF>> m_coastlines;    // closed ring contours, world coords
  QColor m_sea{170, 195, 220};
  QColor m_land{225, 213, 180};
  QColor m_coast{120, 110, 90};
  bool m_loaded = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SHAPEFILE_BASEMAP_PROVIDER_H_
