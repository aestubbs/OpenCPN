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
 * RasterChartProvider -- a ChartProvider that wraps a QImage placed at
 * given lat/lon corners. Renders as a single QSGImageNode (Qt's built-in
 * textured-quad node) fed a QSGTexture produced via
 * `QQuickWindow::createTextureFromImage()`. No custom shader.
 *
 * This is the minimum-viable raster path -- one chart, one tile. Real
 * raster chart loading (KAP/BSB, MBTiles, tile pyramids) plugs in by
 * implementing additional ChartProviders or by adding a tile-managing
 * provider that owns many sub-providers.
 */

#ifndef OCPN_QT_RASTER_CHART_PROVIDER_H_
#define OCPN_QT_RASTER_CHART_PROVIDER_H_

#include <QImage>
#include <QRectF>
#include <QString>

#include "chart_provider.h"

QT_BEGIN_NAMESPACE
class QSGTexture;
QT_END_NAMESPACE

namespace ocpn::qtui {

class RasterChartProvider : public ChartProvider {
  Q_OBJECT

public:
  /** Construct from a QImage and its lat/lon corners. The image's top row
   *  maps to `north_lat`, bottom row to `south_lat`, left column to
   *  `west_lon`, right column to `east_lon`. */
  RasterChartProvider(QString id, QImage image,
                      double north_lat, double south_lat,
                      double west_lon, double east_lon,
                      QObject* parent = nullptr);
  ~RasterChartProvider() override;

  QString id() const override { return m_id; }
  QString name() const override { return m_id; }
  double northLat() const override { return m_north; }
  double southLat() const override { return m_south; }
  double westLon() const override { return m_west; }
  double eastLon() const override { return m_east; }

  QSGNode* renderChart(QSGNode* old_subtree,
                       const Viewport& viewport,
                       QQuickWindow* window) override;

private:
  QString m_id;
  QImage m_image;
  double m_north;
  double m_south;
  double m_west;
  double m_east;
  // World-space destination rect (Y-down: y = -lat). See viewport.h.
  QRectF m_world_rect;
  // Owned. Created lazily on first renderChart (needs the QQuickWindow).
  QSGTexture* m_texture = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_RASTER_CHART_PROVIDER_H_
