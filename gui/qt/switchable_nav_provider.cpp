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
 * Model-backed methods of SwitchableNavDataProvider (#32): routes / tracks /
 * waypoints read from the model (pRouteList / g_TrackList / pWayPointMan) and
 * route edits persisted to the SQLite navobj DB (NavObj_dB). Kept out of the
 * header so the model/wx headers don't leak into every translation unit that
 * includes the provider.
 */

#include "switchable_nav_provider.h"

#include "model/navobj_db.h"
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"  // pRouteList, pWayPointMan, g_pRouteMan
#include "model/track.h"     // g_TrackList, g_pActiveTrack

namespace ocpn::qtui {

namespace {
// One NavRoute per model Route, in pRouteList order (so an index from
// userRoutes() maps straight back to (*pRouteList)[index]).
QList<NavRoute> readModelRoutes() {
  QList<NavRoute> out;
  if (!pRouteList) return out;
  for (Route* r : *pRouteList) {
    NavRoute nr;
    if (r) {
      nr.name = r->GetName();
      nr.guid = r->GetGUID();
      if (r->pRoutePointList)
        for (RoutePoint* wp : *r->pRoutePointList)
          if (wp) nr.points.append(QPointF(wp->m_lon, wp->m_lat));
    }
    out.append(nr);  // keep even empty routes to preserve index alignment
  }
  return out;
}
}  // namespace

QList<NavRoute> SwitchableNavDataProvider::userRoutes() const {
  return readModelRoutes();
}

QList<NavRoute> SwitchableNavDataProvider::routes() const {
  QList<NavRoute> r = readModelRoutes();
  if (m_building && !m_draft.points.isEmpty()) {
    NavRoute d = m_draft;
    if (m_has_rubber) d.points.append(m_rubber);  // live segment to cursor
    r.append(d);
  }
  return r;
}

QList<NavWaypoint> SwitchableNavDataProvider::waypoints() const {
  QList<NavWaypoint> out;
  if (!pWayPointMan) return out;
  const RoutePointList* list = pWayPointMan->GetWaypointList();
  if (!list) return out;
  for (RoutePoint* wp : *list) {
    if (!wp || wp->m_bIsInRoute) continue;  // route points draw via routes()
    NavWaypoint nw;
    nw.name = wp->GetName();
    nw.lat = wp->m_lat;
    nw.lon = wp->m_lon;
    out.append(nw);
  }
  return out;
}

QList<NavTrack> SwitchableNavDataProvider::tracks() const {
  QList<NavTrack> out;
  const auto append = [&](Track* tk, const QColor& color) {
    if (!tk) return;
    NavTrack nt;
    nt.color = color;
    const int n = tk->GetnPoints();
    for (int i = 0; i < n; ++i)
      if (TrackPoint* tp = tk->GetPoint(i))
        nt.points.append(QPointF(tp->m_lon, tp->m_lat));
    if (nt.points.size() >= 2) out.append(nt);
  };
  for (Track* tk : g_TrackList) append(tk, QColor(60, 60, 60));
  if (g_pActiveTrack && g_pActiveTrack->IsRunning())
    append(g_pActiveTrack, QColor(150, 0, 200));  // recording track violet
  return out;
}

bool SwitchableNavDataProvider::finishRoute() {
  if (!m_building) return false;
  const bool ok = m_draft.points.size() >= 2 && pRouteList;
  if (ok) {
    Route* rte = new Route();
    rte->m_RouteNameString = m_draft.name;
    for (const QPointF& ll : m_draft.points)  // ll = (lon, lat)
      rte->AddPoint(new RoutePoint(ll.y(), ll.x(), QString(), QString()));
    pRouteList->push_back(rte);
    NavObj_dB::GetInstance().InsertRoute(rte);  // persists route + its points
  }
  m_building = false;
  m_has_rubber = false;
  m_draft = NavRoute{};
  Q_EMIT staticChanged();
  return ok;
}

void SwitchableNavDataProvider::moveRoutePoint(int route, int pt, double lat,
                                               double lon) {
  if (!pRouteList || route < 0 || route >= static_cast<int>(pRouteList->size()))
    return;
  Route* r = (*pRouteList)[route];
  if (!r) return;
  RoutePoint* rp = r->GetPoint(pt + 1);  // GetPoint is 1-based
  if (!rp) return;
  rp->SetPosition(lat, lon);
  Q_EMIT editChanged();  // cheap redraw; persisted on commitRouteEdit
}

void SwitchableNavDataProvider::commitRouteEdit() {
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::insertRoutePoint(int route, int seg,
                                                 double lat, double lon) {
  if (!pRouteList || route < 0 || route >= static_cast<int>(pRouteList->size()))
    return;
  Route* r = (*pRouteList)[route];
  if (!r) return;
  RoutePoint* after = r->GetPoint(seg + 1);  // start of segment `seg` (1-based)
  if (!after) return;
  r->InsertPointAfter(after, lat, lon);
  NavObj_dB::GetInstance().UpdateRoute(r);
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::deleteRoutePoint(int route, int pt) {
  if (!pRouteList || route < 0 || route >= static_cast<int>(pRouteList->size()))
    return;
  Route* r = (*pRouteList)[route];
  if (!r) return;
  RoutePoint* rp = r->GetPoint(pt + 1);
  if (!rp) return;
  if (r->GetnPoints() <= 2) {  // would leave a degenerate route -> delete it
    deleteRoute(route);
    return;
  }
  r->DeletePoint(rp);
  NavObj_dB::GetInstance().UpdateRoute(r);
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::deleteRoute(int route) {
  if (!pRouteList || route < 0 || route >= static_cast<int>(pRouteList->size()))
    return;
  Route* r = (*pRouteList)[route];
  if (!r) return;
  NavObj_dB::GetInstance().DeleteRoute(r);
  if (g_pRouteMan)
    g_pRouteMan->DeleteRoute(r);  // proper model teardown (list + cleanup)
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::setRecordingTrack(bool on) {
  if (on == m_recording) return;
  m_recording = on;
  if (g_pActiveTrack) {
    if (on)
      g_pActiveTrack->Start();  // self-records off the own-ship fix + persists
    else
      g_pActiveTrack->Stop();
  }
  Q_EMIT staticChanged();
}

}  // namespace ocpn::qtui
