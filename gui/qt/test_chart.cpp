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
 * Implement test_chart.h.
 */

#include "test_chart.h"

#include <cmath>

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPointF>
#include <QRectF>

namespace ocpn::qtui {

QImage MakeTestChart(double north_lat, double south_lat,
                     double west_lon, double east_lon) {
  const int w = 1024;
  const int h = 512;
  const double lon_span = east_lon - west_lon;
  const double lat_span = north_lat - south_lat;

  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  img.fill(QColor(190, 220, 240));  // sea

  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, true);

  // A couple of "land" polygons so there's something recognisable.
  p.setBrush(QColor(190, 210, 140));
  p.setPen(QPen(QColor(120, 140, 80), 2));
  p.drawPolygon(QPolygonF({
      QPointF(w * 0.10, h * 0.30),
      QPointF(w * 0.28, h * 0.20),
      QPointF(w * 0.36, h * 0.50),
      QPointF(w * 0.18, h * 0.62),
      QPointF(w * 0.08, h * 0.50),
  }));
  p.drawPolygon(QPolygonF({
      QPointF(w * 0.55, h * 0.55),
      QPointF(w * 0.78, h * 0.50),
      QPointF(w * 0.92, h * 0.65),
      QPointF(w * 0.85, h * 0.85),
      QPointF(w * 0.62, h * 0.80),
  }));

  // 1° grid.
  p.setPen(QPen(QColor(60, 80, 100, 90), 1, Qt::DotLine));
  for (int lon = static_cast<int>(std::ceil(west_lon)); lon < east_lon; ++lon) {
    const double x = (lon - west_lon) / lon_span * w;
    p.drawLine(QPointF(x, 0), QPointF(x, h));
  }
  for (int lat = static_cast<int>(std::ceil(south_lat)); lat < north_lat;
       ++lat) {
    const double y = (north_lat - lat) / lat_span * h;
    p.drawLine(QPointF(0, y), QPointF(w, y));
  }

  // Lat/lon labels at the corners.
  p.setPen(QColor(30, 30, 30));
  p.setFont(QFont("Helvetica", 10));
  p.drawText(QRectF(6, 4, 200, 18), Qt::AlignLeft | Qt::AlignVCenter,
             QString("%1°N").arg(north_lat, 0, 'f', 1));
  p.drawText(QRectF(w - 206, 4, 200, 18), Qt::AlignRight | Qt::AlignVCenter,
             QString("%1°E").arg(east_lon, 0, 'f', 1));
  p.drawText(QRectF(6, h - 22, 200, 18), Qt::AlignLeft | Qt::AlignVCenter,
             QString("%1°N").arg(south_lat, 0, 'f', 1));
  p.drawText(QRectF(w - 206, h - 22, 200, 18),
             Qt::AlignRight | Qt::AlignVCenter,
             QString("%1°E").arg(west_lon, 0, 'f', 1));

  // Title.
  p.setFont(QFont("Helvetica", 22, QFont::Bold));
  p.setPen(QColor(0, 0, 0, 140));
  p.drawText(QRectF(0, h * 0.45, w, 40), Qt::AlignCenter,
             QStringLiteral("TEST CHART"));

  p.end();
  return img;
}

}  // namespace ocpn::qtui
