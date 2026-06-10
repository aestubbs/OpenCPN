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
 * Minimal KAP/BSB raster chart reader (P2.7 v1): text-header parse
 * (RA size, SC scale, RGB palette, REF reference points, NA name), the
 * standard BSB RLE raster decode (depth-bit pixel values, 7-bit run
 * continuation, EOF line-offset table) into a QImage, and a LINEAR
 * world-coordinate mapping fitted by least squares over the REF points
 * (x -> lon, y -> Mercator world Y) -- exact for the Mercator north-up
 * charts that make up the overwhelming majority of KAPs; skewed or
 * polyconic charts are rejected by fit residual and reported.
 */

#ifndef OCPN_QT_RASTER_CHART_H_
#define OCPN_QT_RASTER_CHART_H_

#include <QImage>
#include <QString>

namespace ocpn::qtui {

struct RasterChart {
  bool ok = false;
  QString error;
  QString name;
  int nativeScale = 0;
  QImage image;  // ARGB32
  // Linear world mapping (Viewport convention: x = lon, y = Mercator
  // degree-equivalent, Y-down).
  double west = 0, east = 0;
  double worldYTop = 0, worldYBottom = 0;
  // Geographic extent, for the catalog.
  double north = 0, south = 0;
};

class RasterChartReader {
public:
  /** Full decode (header + palette + raster + georef fit). */
  static RasterChart load(const QString& path);
  /** Header-only: extent + scale + name (cheap, for the catalog scan). */
  static RasterChart scanHeader(const QString& path);
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_RASTER_CHART_H_
