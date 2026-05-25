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
 * Plain value types for the nav-overlay layers (P2.11): AIS targets,
 * own-ship state, routes, tracks, waypoints.
 *
 * Deliberately decoupled from the model's wx-era classes (AisTargetData,
 * Route, RoutePoint, Track): the overlay Layers consume these Qt-typed
 * snapshots through the NavDataProvider interface (nav_data_provider.h), so
 * the renderer is identical whether the data comes from the synthetic demo
 * provider or, later, an adapter over g_pAIS / pRouteList / g_TrackList.
 *
 * Positions are geographic degrees (lat north +, lon east +); the Layers
 * convert to world coordinates (x = lon, y = -lat) at build time.
 */

#ifndef OCPN_QT_NAV_DATA_H_
#define OCPN_QT_NAV_DATA_H_

#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>

namespace ocpn::qtui {

// Course/heading "not available" sentinel (per AIS convention 511 deg).
inline constexpr double kHeadingUnavailable = 511.0;

struct AisTarget {
  int mmsi = 0;
  double lat = 0.0;
  double lon = 0.0;
  double cog = 0.0;                    // course over ground, deg true
  double sog = 0.0;                    // speed over ground, knots
  double hdg = kHeadingUnavailable;    // heading, deg true (511 = N/A)
  QString name;
};

struct OwnShipState {
  bool valid = false;
  double lat = 0.0;
  double lon = 0.0;
  double cog = 0.0;
  double sog = 0.0;
  double hdg = kHeadingUnavailable;
  // Wind + speed-through-water (#39). Angle sentinel -1000 = no data; speed
  // < 0 = no data. Angles are degrees relative to the bow (0 = ahead).
  double awa = -1000.0;  // apparent wind angle
  double aws = -1.0;     // apparent wind speed, knots
  double twa = -1000.0;  // true wind angle
  double tws = -1.0;     // true wind speed, knots
  double stw = -1.0;     // speed through water, knots
};

struct NavRoute {
  QString name;
  QString guid;                        // model Route GUID ("" for the draft)
  QColor color = QColor(200, 0, 200);  // S-52-ish route magenta
  QList<QPointF> points;               // (lon, lat) in geographic degrees
};

struct NavWaypoint {
  QString name;
  double lat = 0.0;
  double lon = 0.0;
  QColor color = QColor(255, 140, 0);
};

struct NavTrack {
  QColor color = QColor(60, 60, 60);
  QList<QPointF> points;               // (lon, lat)
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NAV_DATA_H_
