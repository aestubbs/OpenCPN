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

#include <algorithm>

#include <QDate>
#include <QDateTime>
#include <QTimer>

#include "route_defaults_config.h"  // trackAutoDaily mode
#include "model/config_vars.h"  // g_default_wp_icon
#include "model/navobj_db.h"
#include "model/own_ship.h"   // gLon (LMT day)
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"  // pRouteList, pWayPointMan, g_pRouteMan
#include "model/track.h"     // g_TrackList, g_pActiveTrack
#include "model/wx_qt_string.h"  // wxString_to_QString

namespace ocpn::qtui {

namespace {
// Julian-day index of "today" in the chosen time base (1 computer/local,
// 2 UTC, 3 LMT ~ UTC + longitude/15h). 0 = off -> no rollover.
qint64 trackDayIndex(int mode) {
  const QDateTime utc = QDateTime::currentDateTimeUtc();
  switch (mode) {
    case 1: return QDate::currentDate().toJulianDay();  // computer local
    case 2: return utc.date().toJulianDay();            // UTC
    case 3: {                                            // local mean time
      const double lon = std::isfinite(gLon) ? gLon : 0.0;
      return utc.addSecs(static_cast<qint64>(lon / 15.0 * 3600.0))
          .date()
          .toJulianDay();
    }
    default: return 0;
  }
}
}  // namespace

namespace {
// One NavRoute per model Route, in pRouteList order (so an index from
// userRoutes() maps straight back to (*pRouteList)[index]).
QList<NavRoute> readModelRoutes() {
  QList<NavRoute> out;
  if (!pRouteList) return out;
  RoutePoint* active_wp =
      g_pRouteMan ? g_pRouteMan->GetpActivePoint() : nullptr;
  for (Route* r : *pRouteList) {
    NavRoute nr;
    if (r) {
      nr.name = r->GetName();
      nr.guid = r->GetGUID();
      if (r->pRoutePointList)
        for (RoutePoint* wp : *r->pRoutePointList)
          if (wp) nr.points.append(QPointF(wp->m_lon, wp->m_lat));
      // Per-route mark icon + SCAMIN: the route's points share one icon (set
      // via the route-details dialog) and a SCAMIN; read both off the first
      // point.
      if (r->GetnPoints() > 0)
        if (RoutePoint* p0 = r->GetPoint(1)) {
          nr.pointIcon = p0->GetIconName();
          // Only honour SCAMIN when it's actually enabled for the point
          // (b_UseScamin); otherwise 0 = never cull. The default m_ScaMin is
          // non-zero, so gating on the value alone hid ordinary routes.
          nr.scamin = p0->GetUseSca() ? p0->GetScaMin() : 0;
        }
      // Route-following state (P3.16): mark the active route + active leg so
      // the overlay can highlight it (the active destination's 0-based index).
      if (r->m_bRtIsActive) {
        nr.active = true;
        if (active_wp) nr.activeLeg = r->GetIndexOf(active_wp);  // 0-based, -1
      }
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
    nw.guid = wp->m_GUID;
    nw.comment = wp->GetDescription();
    nw.iconName = wp->GetIconName();
    nw.lat = wp->m_lat;
    nw.lon = wp->m_lon;
    nw.visible = wp->IsVisible();
    nw.scamin = wp->GetUseSca() ? wp->GetScaMin() : 0;  // 0 = never cull
    const QDateTime ct = wp->GetCreateTime();
    nw.createTimeMs = ct.isValid() ? ct.toMSecsSinceEpoch() : 0;
    out.append(nw);
  }
  return out;
}

QList<NavTrack> SwitchableNavDataProvider::tracks() const {
  QList<NavTrack> out;
  // The recording track lives in g_TrackList too (started by startTrack); mark
  // it active + violet. All tracks are listed (the drawer tab lists them even
  // with <2 points); the TrackLayer skips drawing a polyline under 2 points.
  for (Track* tk : g_TrackList) {
    if (!tk) continue;
    const bool active = (tk == g_pActiveTrack);
    NavTrack nt;
    nt.name = tk->GetName(true);  // dated auto-name if unnamed
    nt.guid = tk->m_GUID;
    nt.lengthNm = tk->Length();
    nt.visible = tk->IsVisible();
    nt.active = active;
    nt.color = active ? QColor(150, 0, 200) : QColor(60, 60, 60);
    const int n = tk->GetnPoints();
    for (int i = 0; i < n; ++i)
      if (TrackPoint* tp = tk->GetPoint(i))
        nt.points.append(QPointF(tp->m_lon, tp->m_lat));
    if (n > 0)
      if (TrackPoint* first = tk->GetPoint(0)) {
        const QDateTime ct = first->GetCreateTime();
        nt.startTimeMs = ct.isValid() ? ct.toMSecsSinceEpoch() : 0;
      }
    out.append(nt);
  }
  return out;
}

// Dated track name with a #n suffix to disambiguate same-day tracks.
QString SwitchableNavDataProvider::makeTrackName() const {
  const QString date = QDate::currentDate().toString(Qt::ISODate);
  int sameDay = 0;
  for (Track* tk : g_TrackList)
    if (tk && tk->GetName(true).startsWith(date)) ++sameDay;
  return sameDay > 0 ? QStringLiteral("%1 #%2").arg(date).arg(sameDay + 1)
                     : date;
}

void SwitchableNavDataProvider::startTrack() {
  if (g_pActiveTrack && g_pActiveTrack->IsRunning()) return;  // already on
  // Discard the stale empty ActiveTrack nav_core pre-creates (never started,
  // not in the list) so we don't leak it.
  if (g_pActiveTrack && g_pActiveTrack->GetnPoints() == 0 &&
      std::find(g_TrackList.begin(), g_TrackList.end(), g_pActiveTrack) ==
          g_TrackList.end()) {
    delete g_pActiveTrack;
    g_pActiveTrack = nullptr;
  }
  auto* t = new ActiveTrack();
  t->SetName(makeTrackName());
  g_TrackList.push_back(t);
  g_pActiveTrack = t;
  NavObj_dB::GetInstance().InsertTrack(t);
  t->Start();  // self-records off the own-ship fix
  m_recording = true;
  // Auto-create-daily: remember the start day + run the minute-rollover check.
  m_track_day = trackDayIndex(RouteDefaultsConfig::instance().trackAutoDaily());
  if (!m_rollover_timer) {
    m_rollover_timer = new QTimer(this);
    m_rollover_timer->setInterval(60000);  // check each minute
    connect(m_rollover_timer, &QTimer::timeout, this,
            &SwitchableNavDataProvider::checkDailyRollover);
  }
  m_rollover_timer->start();
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::checkDailyRollover() {
  if (!m_recording) return;
  const int mode = RouteDefaultsConfig::instance().trackAutoDaily();
  if (mode == 0) return;  // off
  const qint64 today = trackDayIndex(mode);
  if (m_track_day < 0) {
    m_track_day = static_cast<int>(today);
    return;
  }
  if (today != m_track_day) {
    m_track_day = static_cast<int>(today);
    resetTrack();  // finalize the day's track + start a fresh dated one
  }
}

void SwitchableNavDataProvider::stopTrack() {
  if (!g_pActiveTrack) return;
  g_pActiveTrack->Stop();
  if (g_pActiveTrack->GetnPoints() < 2) {  // too short -> discard
    NavObj_dB::GetInstance().DeleteTrack(g_pActiveTrack);
    g_TrackList.erase(
        std::remove(g_TrackList.begin(), g_TrackList.end(), g_pActiveTrack),
        g_TrackList.end());
    delete g_pActiveTrack;
  } else {
    NavObj_dB::GetInstance().UpdateTrack(g_pActiveTrack);  // finalize
  }
  g_pActiveTrack = nullptr;
  m_recording = false;
  if (m_rollover_timer) m_rollover_timer->stop();
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::resetTrack() {
  stopTrack();
  startTrack();  // a fresh dated track -> a new tile
}

void SwitchableNavDataProvider::renameTrack(const QString& guid,
                                            const QString& name) {
  for (Track* tk : g_TrackList)
    if (tk && tk->m_GUID == guid) {
      tk->SetName(name);
      NavObj_dB::GetInstance().UpdateTrack(tk);
      Q_EMIT staticChanged();
      return;
    }
}

void SwitchableNavDataProvider::setTrackVisible(const QString& guid,
                                                bool visible) {
  for (Track* tk : g_TrackList)
    if (tk && tk->m_GUID == guid) {
      tk->SetVisible(visible);
      NavObj_dB::GetInstance().UpdateTrack(tk);
      Q_EMIT staticChanged();
      return;
    }
}

void SwitchableNavDataProvider::deleteTrack(const QString& guid) {
  for (auto it = g_TrackList.begin(); it != g_TrackList.end(); ++it) {
    Track* tk = *it;
    if (!tk || tk->m_GUID != guid) continue;
    NavObj_dB::GetInstance().DeleteTrack(tk);
    if (tk == g_pActiveTrack) {
      g_pActiveTrack = nullptr;
      m_recording = false;
    }
    g_TrackList.erase(it);
    delete tk;
    Q_EMIT staticChanged();
    return;
  }
}

bool SwitchableNavDataProvider::finishRoute() {
  if (!m_building) return false;
  const bool ok = m_draft.points.size() >= 2 && pRouteList;
  if (ok) {
    Route* rte = new Route();
    rte->m_RouteNameString = m_draft.name;
    const long sca = RouteDefaultsConfig::instance().scaminMin();
    for (const QPointF& ll : m_draft.points) {  // ll = (lon, lat)
      RoutePoint* p = new RoutePoint(ll.y(), ll.x(), QString(), QString());
      if (sca > 0) {  // declutter at small scale (SCAMIN), enabled per point
        p->SetScaMin(sca);
        p->SetUseSca(true);
      }
      rte->AddPoint(p);
    }
    pRouteList->push_back(rte);
    NavObj_dB::GetInstance().InsertRoute(rte);  // persists route + its points
  }
  m_building = false;
  m_has_rubber = false;
  m_draft = NavRoute{};
  Q_EMIT staticChanged();
  return ok;
}

int SwitchableNavDataProvider::createRoute(const QString& name,
                                           const QList<QPointF>& points) {
  if (!pRouteList || points.size() < 2) return -1;
  Route* rte = new Route();
  rte->m_RouteNameString = name;
  for (const QPointF& ll : points) {  // ll = (lon, lat)
    RoutePoint* p = new RoutePoint(ll.y(), ll.x(), QString(), QString());
    rte->AddPoint(p);
  }
  pRouteList->push_back(rte);
  NavObj_dB::GetInstance().InsertRoute(rte);  // persists route + its points
  Q_EMIT staticChanged();
  return static_cast<int>(pRouteList->size()) - 1;
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

void SwitchableNavDataProvider::reverseRoute(int route) {
  if (!pRouteList || route < 0 || route >= static_cast<int>(pRouteList->size()))
    return;
  Route* r = (*pRouteList)[route];
  if (!r) return;
  r->Reverse();
  NavObj_dB::GetInstance().UpdateRoute(r);
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::duplicateRoute(int route) {
  if (!pRouteList || route < 0 || route >= static_cast<int>(pRouteList->size()))
    return;
  Route* src = (*pRouteList)[route];
  if (!src) return;
  Route* dup = new Route();
  const QString base = src->m_RouteNameString.isEmpty()
                           ? QStringLiteral("Route")
                           : src->m_RouteNameString;
  dup->m_RouteNameString = base + QStringLiteral(" copy");
  const int n = src->GetnPoints();
  for (int i = 1; i <= n; ++i) {  // GetPoint is 1-based
    RoutePoint* p = src->GetPoint(i);
    if (!p) continue;
    RoutePoint* np = new RoutePoint(p->m_lat, p->m_lon, QString(), QString());
    np->SetIconName(p->GetIconName());  // carry the per-route icon + SCAMIN
    np->SetScaMin(p->GetScaMin());
    np->SetUseSca(p->GetUseSca());
    dup->AddPoint(np);
  }
  pRouteList->push_back(dup);
  NavObj_dB::GetInstance().InsertRoute(dup);  // persists route + its points
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::renameRoute(int route, const QString& name) {
  if (!pRouteList || route < 0 || route >= static_cast<int>(pRouteList->size()))
    return;
  Route* r = (*pRouteList)[route];
  if (!r) return;
  r->m_RouteNameString = name;
  NavObj_dB::GetInstance().UpdateDBRouteAttributes(r);
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::setRoutePointIcon(int route,
                                                  const QString& icon) {
  if (!pRouteList || route < 0 || route >= static_cast<int>(pRouteList->size()))
    return;
  Route* r = (*pRouteList)[route];
  if (!r) return;  // empty icon clears back to a plain dot
  // One icon for the whole route's points (the route-details dialog's "Point
  // icon"); stamp every point and persist.
  const int n = r->GetnPoints();
  for (int i = 1; i <= n; ++i)
    if (RoutePoint* p = r->GetPoint(i)) p->SetIconName(icon);
  NavObj_dB::GetInstance().UpdateRoute(r);
  Q_EMIT staticChanged();
}

// --- Marks (free / isolated waypoints) ---------------------------------------

void SwitchableNavDataProvider::dropMark(double lat, double lon,
                                         const QString& name,
                                         const QString& comment,
                                         const QString& icon) {
  if (!pWayPointMan) return;
  // Fall back to the configured default mark icon (g_default_wp_icon, set from
  // Options > User Interface > Routes & Marks) when none is supplied.
  const QString ic =
      !icon.isEmpty() ? icon
                      : (g_default_wp_icon.IsEmpty()
                             ? QStringLiteral("triangle")
                             : wxString_to_QString(g_default_wp_icon));
  // The ctor (bAddToList defaults true) registers it with pWayPointMan and
  // assigns a GUID + create-time.
  RoutePoint* wp = new RoutePoint(lat, lon, ic, name, QString());
  wp->m_bIsolatedMark = true;
  wp->m_MarkDescription = comment;
  // Stamp the default SCAMIN (Options > ... > Routes & Marks) so the mark
  // declutters at small scale like a chart object.
  const long sca = RouteDefaultsConfig::instance().scaminMin();
  if (sca > 0) {
    wp->SetScaMin(sca);
    wp->SetUseSca(true);
  }
  NavObj_dB::GetInstance().InsertRoutePoint(wp);  // persist mark + position
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::renameWaypoint(const QString& guid,
                                               const QString& name) {
  RoutePoint* wp =
      pWayPointMan ? pWayPointMan->FindRoutePointByGUID(guid) : nullptr;
  if (!wp) return;
  wp->SetName(name);
  NavObj_dB::GetInstance().UpdateDBRoutePointAttributes(wp);
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::setWaypointComment(const QString& guid,
                                                   const QString& comment) {
  RoutePoint* wp =
      pWayPointMan ? pWayPointMan->FindRoutePointByGUID(guid) : nullptr;
  if (!wp) return;
  wp->m_MarkDescription = comment;
  NavObj_dB::GetInstance().UpdateDBRoutePointAttributes(wp);
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::setWaypointIcon(const QString& guid,
                                                const QString& icon) {
  RoutePoint* wp =
      pWayPointMan ? pWayPointMan->FindRoutePointByGUID(guid) : nullptr;
  if (!wp || icon.isEmpty()) return;
  wp->SetIconName(icon);
  NavObj_dB::GetInstance().UpdateDBRoutePointAttributes(wp);
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::setWaypointVisible(const QString& guid,
                                                   bool visible) {
  RoutePoint* wp =
      pWayPointMan ? pWayPointMan->FindRoutePointByGUID(guid) : nullptr;
  if (!wp) return;
  wp->SetVisible(visible);
  NavObj_dB::GetInstance().UpdateDBRoutePointViz(wp);
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::deleteWaypoint(const QString& guid) {
  RoutePoint* wp =
      pWayPointMan ? pWayPointMan->FindRoutePointByGUID(guid) : nullptr;
  if (!wp) return;
  NavObj_dB::GetInstance().DeleteRoutePoint(wp);
  pWayPointMan->RemoveRoutePoint(wp);  // unlist (does not free)
  delete wp;
  Q_EMIT staticChanged();
}

void SwitchableNavDataProvider::setRecordingTrack(bool on) {
  if (on == m_recording) return;
  if (on)
    startTrack();
  else
    stopTrack();
}

}  // namespace ocpn::qtui
