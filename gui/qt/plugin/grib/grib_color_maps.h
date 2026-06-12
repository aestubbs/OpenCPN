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
 * The wx grib_pi colour tables (GribOverlayFactory.cpp GetGraphicColor),
 * ported verbatim: 10 ramps (zyGrib/NOAA palettes) + the per-data-type
 * normalization ranges (GribOverlaySettings::GetMin/GetMax). Values feed
 * in as a 0..1 fraction of the type's range -- the fraction is invariant
 * under unit conversion (affine), so SI record values give exact wx
 * colours.
 */

#ifndef OCPN_QT_GRIB_COLOR_MAPS_H_
#define OCPN_QT_GRIB_COLOR_MAPS_H_

#include <QColor>
#include <QString>

namespace ocpn::qtui::gribmaps {

enum Map {
  Generic,
  Wind,
  AirTemp,
  SeaTemp,
  Precipitation,
  Cloud,
  Current,
  Cape,
  Refc,
  Windy,
};

/** Colour at `fraction` (0..1, clamped) of the map's range, gradually
 *  interpolated between table stops (wx UseGradualColors). */
QRgb color(Map map, double fraction);

/** The wx ramp spec for an opencpn-qt GRIB type key ("wind", "gust",
 *  "waves", ...): which map, and the SI-unit [min,max] the fraction
 *  normalizes over. Returns false for unknown keys. */
bool rampForKey(const QString& key, Map* map, double* minSI, double* maxSI);

}  // namespace ocpn::qtui::gribmaps

#endif  // OCPN_QT_GRIB_COLOR_MAPS_H_
