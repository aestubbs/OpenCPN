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
 * Implement model_nav_data_provider.h.
 */

#include "model_nav_data_provider.h"

#include <cmath>

#include <QTimer>

#include "model/ais_decoder.h"
#include "model/ais_target_data.h"
#include "model/own_ship.h"
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"
#include "model/track.h"

namespace ocpn::qtui {

namespace {
inline bool finitePos(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90.0 &&
         std::abs(lon) <= 360.0 && !(lat == 0.0 && lon == 0.0);
}
}  // namespace

ModelNavDataProvider::ModelNavDataProvider(QObject* parent)
    : NavDataProvider(parent) {
  m_timer = new QTimer(this);
  m_timer->setInterval(250);  // 4 Hz poll of the model
  connect(m_timer, &QTimer::timeout, this, &ModelNavDataProvider::poll);
}

void ModelNavDataProvider::setRunning(bool run) {
  if (run)
    m_timer->start();
  else
    m_timer->stop();
}

QList<AisTarget> ModelNavDataProvider::aisTargets() const {
  QList<AisTarget> out;
  if (!g_pAIS) return out;
  for (const auto& [mmsi, td] : g_pAIS->GetTargetList()) {
    if (!td || td->b_lost || td->b_removed) continue;
    if (!finitePos(td->Lat, td->Lon)) continue;
    AisTarget t;
    t.mmsi = td->MMSI;
    t.lat = td->Lat;
    t.lon = td->Lon;
    t.cog = std::isfinite(td->COG) ? td->COG : 0.0;
    t.sog = std::isfinite(td->SOG) ? td->SOG : 0.0;
    t.hdg = td->HDG;
    t.name = td->GetFullName().trimmed();
    out.append(t);
  }
  return out;
}

OwnShipState ModelNavDataProvider::ownShip() const {
  OwnShipState s;
  if (!finitePos(gLat, gLon)) return s;  // invalid -> hidden
  s.valid = true;
  s.lat = gLat;
  s.lon = gLon;
  s.cog = std::isfinite(gCog) ? gCog : 0.0;
  s.sog = std::isfinite(gSog) ? gSog : 0.0;
  s.hdg = std::isfinite(gHdt) ? gHdt : kHeadingUnavailable;
  return s;
}

QList<NavRoute> ModelNavDataProvider::routes() const {
  QList<NavRoute> out;
  if (!pRouteList) return out;
  for (Route* r : *pRouteList) {
    if (!r || !r->pRoutePointList) continue;
    NavRoute nr;
    nr.name = r->GetName();
    for (RoutePoint* wp : *r->pRoutePointList)
      if (wp) nr.points.append(QPointF(wp->m_lon, wp->m_lat));
    if (!nr.points.isEmpty()) out.append(nr);
  }
  return out;
}

QList<NavWaypoint> ModelNavDataProvider::waypoints() const {
  QList<NavWaypoint> out;
  if (!pWayPointMan) return out;
  const RoutePointList* list = pWayPointMan->GetWaypointList();
  if (!list) return out;
  for (RoutePoint* wp : *list) {
    if (!wp) continue;
    // Skip points that belong to a route (those draw via routes()).
    if (wp->m_bIsInRoute) continue;
    NavWaypoint nw;
    nw.name = wp->GetName();
    nw.lat = wp->m_lat;
    nw.lon = wp->m_lon;
    out.append(nw);
  }
  return out;
}

QList<NavTrack> ModelNavDataProvider::tracks() const {
  QList<NavTrack> out;
  for (Track* tk : g_TrackList) {
    if (!tk) continue;
    NavTrack nt;
    const int n = tk->GetnPoints();
    for (int i = 0; i < n; ++i) {
      TrackPoint* tp = tk->GetPoint(i);
      if (tp) nt.points.append(QPointF(tp->m_lon, tp->m_lat));
    }
    if (nt.points.size() >= 2) out.append(nt);
  }
  return out;
}

void ModelNavDataProvider::poll() {
  Q_EMIT dynamicChanged();

  // Cheap static change-detect: route/waypoint/track counts.
  const int sig = (pRouteList ? static_cast<int>(pRouteList->size()) : 0) * 73 +
                  static_cast<int>(g_TrackList.size()) * 17 +
                  (pWayPointMan && pWayPointMan->GetWaypointList()
                       ? static_cast<int>(pWayPointMan->GetWaypointList()->size())
                       : 0);
  if (sig != m_last_static_sig) {
    m_last_static_sig = sig;
    Q_EMIT staticChanged();
  }
}

}  // namespace ocpn::qtui
