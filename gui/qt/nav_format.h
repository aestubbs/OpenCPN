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
 * Small shared formatters for nav readouts (used by the HUD + AIS info
 * view-models, P3.4 / P3.9).
 */

#ifndef OCPN_QT_NAV_FORMAT_H_
#define OCPN_QT_NAV_FORMAT_H_

#include <cmath>

#include <QChar>
#include <QString>

namespace ocpn::qtui::navfmt {

/** A signed degree value as D°M.m' with a hemisphere suffix. */
inline QString coord(double deg, int deg_width, QChar pos, QChar neg) {
  const QChar hemi = deg >= 0.0 ? pos : neg;
  deg = std::abs(deg);
  const int d = static_cast<int>(deg);
  const double m = (deg - d) * 60.0;
  return QStringLiteral("%1°%2'%3")
      .arg(d, deg_width, 10, QChar('0'))
      .arg(m, 5, 'f', 2, QChar('0'))
      .arg(hemi);
}

inline QString latLon(double lat, double lon) {
  return coord(lat, 2, QChar('N'), QChar('S')) + QStringLiteral("  ") +
         coord(lon, 3, QChar('E'), QChar('W'));
}

inline QString sog(double knots) {
  return QStringLiteral("%1 kn").arg(knots, 0, 'f', 1);
}

inline QString cog(double deg) {
  return QStringLiteral("%1°").arg(deg, 3, 'f', 0, QChar('0'));
}

}  // namespace ocpn::qtui::navfmt

#endif  // OCPN_QT_NAV_FORMAT_H_
