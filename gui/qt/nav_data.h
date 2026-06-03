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
  int shipType = 0;                    // AIS ship-and-cargo type (0-99)
  QString name;
  bool isSart = false;                 // SART / MOB / EPIRB distress beacon
  bool isDsc = false;                  // DSC distress relay target

  // CPA/TCPA solution vs own ship (mirrors wx AisTargetData; computed by
  // ais_cpa.cpp). Valid only when cpaValid; -1 = not computed.
  double rangeNm = -1.0;   // current range to target, NM
  double bearingDeg = -1.0;// current bearing to target, deg true
  double cpaNm = -1.0;     // closest point of approach, NM
  double tcpaMin = -1.0;   // time to CPA, minutes (>=0 when valid)
  bool cpaValid = false;   // CPA/TCPA solution available
  bool dangerous = false;  // crosses the CPA/TCPA warning thresholds
};

// One recorded past position of an AIS target (the persisted trail). Stored
// globally in SQLite and queried back to draw a selected vessel's trail.
// COG/SOG/HDG are the vessel's *reported* values (kept, not derived: heading
// is underivable from position, and reported COG/SOG beat noisy deltas) so a
// historical target can be redrawn with its true orientation + speed.
struct AisTrackPoint {
  qint64 t = 0;     // epoch ms
  double lat = 0.0;
  double lon = 0.0;
  double cog = -1.0;                   // course over ground, deg true (-1 = n/a)
  double sog = -1.0;                   // speed over ground, knots (-1 = n/a)
  double hdg = kHeadingUnavailable;    // heading, deg true (511 = n/a)
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
  // Route-following state (P3.16). active = this is the route being navigated;
  // activeLeg = 0-based index of the active destination point in points[] (the
  // active leg runs points[activeLeg-1] -> points[activeLeg]). -1 when none.
  bool active = false;
  int activeLeg = -1;
};

struct NavWaypoint {
  QString name;
  QString guid;                        // model RoutePoint GUID (stable id)
  QString comment;                     // RoutePoint m_MarkDescription
  QString iconName;                    // RoutePoint icon id (e.g. "triangle")
  double lat = 0.0;
  double lon = 0.0;
  bool visible = true;                 // RoutePoint m_bIsVisible (the eye)
  qint64 createTimeMs = 0;             // creation time, for reverse-chrono sort
  QColor color = QColor(255, 140, 0);
};

struct NavTrack {
  QString name;
  QString guid;                        // model Track GUID
  double lengthNm = 0.0;
  qint64 startTimeMs = 0;              // first point's time, for reverse-chrono
  bool visible = true;
  bool active = false;                 // the live recording track
  QColor color = QColor(60, 60, 60);
  QList<QPointF> points;               // (lon, lat)
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NAV_DATA_H_
