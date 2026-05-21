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
 * MakeTestChart -- generate a QImage that looks vaguely chart-like, for
 * the Phase 2 prototype (no real chart loader integration yet). Drawn at
 * startup with QPainter; cheap, no external file dependency.
 */

#ifndef OCPN_QT_TEST_CHART_H_
#define OCPN_QT_TEST_CHART_H_

#include <QImage>

namespace ocpn::qtui {

/** Produce a 1024x512 test "chart" QImage spanning the given lat/lon box.
 *  Sea, two land blobs, a 1° grid, a compass-rose-like corner label, and
 *  a "TEST CHART" title. */
QImage MakeTestChart(double north_lat, double south_lat,
                     double west_lon, double east_lon);

}  // namespace ocpn::qtui

#endif  // OCPN_QT_TEST_CHART_H_
