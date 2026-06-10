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
 * Implement route_follower.h.
 */

#include "route_follower.h"

#include <cmath>

#include "config_store.h"
#include "display_config.h"

#include "model/config_vars.h"  // g_persist_active_route, g_active_route
#include "model/own_ship.h"     // gLat/gLon/gCog/gSog
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"     // g_pRouteMan, pRouteList, Routeman
#include "model/wx_qt_string.h"  // QString_to_wxString

namespace ocpn::qtui {

RouteFollower::RouteFollower(QObject* parent) : QObject(parent) { snapshot(); }

bool RouteFollower::activate(int routeIndex) {
  return activateFrom(routeIndex, -1);
}

bool RouteFollower::activateFrom(int routeIndex, int wpIndex) {
  if (!g_pRouteMan || !pRouteList) return false;
  if (routeIndex < 0 || routeIndex >= static_cast<int>(pRouteList->size()))
    return false;
  Route* r = (*pRouteList)[routeIndex];
  if (!r || !r->pRoutePointList || r->GetnPoints() < 2) return false;

  // Already the active route -> nothing to do (idempotent activate).
  if (g_pRouteMan->GetpActiveRoute() == r) return true;

  // Only one route active at a time (mirrors wx).
  if (g_pRouteMan->IsAnyRouteActive()) g_pRouteMan->DeactivateRoute();

  RoutePoint* start = nullptr;
  if (wpIndex >= 0) {
    start = r->GetPoint(wpIndex + 1);  // GetPoint is 1-based
  } else {
    // Best start waypoint for the current position/course (wx parity): the one
    // we'd reach soonest given where the boat is heading.
    start = g_pRouteMan->FindBestActivatePoint(r, gLat, gLon, gCog, gSog);
  }

  r->SetVisible(true);  // a hidden route can't be followed blind
  g_pRouteMan->ActivateRoute(r, start);

  // Remember the active route so "Persist active route" can restore it next
  // launch (g_active_route is the model's notion; the GUID is also stored in
  // the Qt config since we drive the restore ourselves).
  g_active_route = QString_to_wxString(r->GetGUID());
  ConfigStore::instance().setString("routes/activeGuid", r->GetGUID());

  update();  // compute the first solution immediately
  Q_EMIT activeRouteChanged();
  return true;
}

void RouteFollower::deactivate() {
  if (g_pRouteMan && g_pRouteMan->IsAnyRouteActive()) {
    g_pRouteMan->DeactivateRoute(false);  // user-initiated, not an arrival
    ConfigStore::instance().setString("routes/activeGuid", QString());
    snapshot();
    Q_EMIT activeRouteChanged();
    Q_EMIT changed();
  }
}

void RouteFollower::restorePersisted() {
  // "Persist active route across restarts": re-activate the route saved last
  // session, if the option is on and it still exists. Called once at startup
  // after the nav objects have loaded.
  if (!g_persist_active_route || !pRouteList) return;
  if (g_pRouteMan && g_pRouteMan->IsAnyRouteActive()) return;
  const QString guid = ConfigStore::instance().getString("routes/activeGuid");
  if (guid.isEmpty()) return;
  for (int i = 0; i < static_cast<int>(pRouteList->size()); ++i) {
    Route* r = (*pRouteList)[i];
    if (r && r->GetGUID() == guid) {
      activate(i);
      return;
    }
  }
}

void RouteFollower::skip() {
  if (!g_pRouteMan || !g_pRouteMan->IsAnyRouteActive()) return;
  Route* r = g_pRouteMan->GetpActiveRoute();
  if (!g_pRouteMan->ActivateNextPoint(r, true)) {
    // Skipped past the last point -> end the route.
    const QString name = r ? r->GetName() : QString();
    g_pRouteMan->DeactivateRoute(true);
    snapshot();
    Q_EMIT activeRouteChanged();
    Q_EMIT ended(name);
  }
  Q_EMIT changed();
}

void RouteFollower::zeroXte() {
  if (!g_pRouteMan || !g_pRouteMan->IsAnyRouteActive()) return;
  g_pRouteMan->ZeroCurrentXTEToActivePoint();
  update();  // re-snapshot so the nav strip's XTE reads zero immediately
}

void RouteFollower::update() {
  if (!g_pRouteMan) return;

  // Capture the pre-tick active leg so we can detect an advance / route end
  // that UpdateProgress() performs internally.
  const bool was_active = g_pRouteMan->IsAnyRouteActive();
  Route* prev_route = g_pRouteMan->GetpActiveRoute();
  RoutePoint* prev_wp = g_pRouteMan->GetpActivePoint();
  const QString prev_wp_name = prev_wp ? prev_wp->GetName() : QString();
  const QString prev_route_name = prev_route ? prev_route->GetName() : QString();

  g_pRouteMan->UpdateProgress();  // the ported model engine (Step 1)

  const bool now_active = g_pRouteMan->IsAnyRouteActive();
  RoutePoint* now_wp = g_pRouteMan->GetpActivePoint();

  snapshot();
  Q_EMIT changed();

  if (was_active && !now_active) {
    // The route deactivated itself during the tick -> reached the end. Don't
    // restore a completed route next launch.
    ConfigStore::instance().setString("routes/activeGuid", QString());
    Q_EMIT activeRouteChanged();
    Q_EMIT ended(prev_route_name);
  } else if (was_active && now_active && now_wp != prev_wp) {
    // Advanced to the next waypoint.
    Q_EMIT arrived(prev_wp_name);
  }
}

void RouteFollower::snapshot() {
  DisplayConfig& dc = DisplayConfig::instance();
  Route* r = g_pRouteMan ? g_pRouteMan->GetpActiveRoute() : nullptr;
  RoutePoint* wp = g_pRouteMan ? g_pRouteMan->GetpActivePoint() : nullptr;

  m_active = (r != nullptr);
  if (!m_active) {
    m_route_name.clear();
    m_route_guid.clear();
    m_to_wp.clear();
    m_btw = m_dtw = m_xte = 0.0;
    m_xte_dir = 0;
    m_btw_text = m_dtw_text = m_xte_text = m_vmg_text = m_eta_text =
        m_leg_text = QString();
    m_arrival = false;
    return;
  }

  m_route_name = r->GetName();
  m_route_guid = r->GetGUID();
  m_to_wp = wp ? wp->GetName() : QString();

  m_btw = g_pRouteMan->GetCurrentBrgToActivePoint();
  m_dtw = g_pRouteMan->GetCurrentRngToActivePoint();
  m_xte = g_pRouteMan->GetCurrentXTEToActivePoint();
  m_xte_dir = g_pRouteMan->GetXTEDir();
  m_arrival = g_pRouteMan->GetArrival();

  m_btw_text = dc.formatBearing(m_btw);
  m_dtw_text = dc.formatDistance(m_dtw);
  // XTE with steer-to side: "0.12 NM ▸" (steer right) / "◂ 0.12 NM" (left).
  const QString xte_mag = dc.formatDistance(m_xte);
  m_xte_text = m_xte_dir < 0
                   ? QStringLiteral("%1 ‹ port").arg(xte_mag)
                   : QStringLiteral("%1 › stbd").arg(xte_mag);

  // VMG toward the waypoint = SOG * cos(BTW - COG). Negative = opening.
  if (std::isfinite(gSog) && std::isfinite(gCog)) {
    const double vmg = gSog * std::cos((m_btw - gCog) * M_PI / 180.0);
    m_vmg_text = dc.formatSpeed(vmg);
    // ETA from VMG (only meaningful while closing the waypoint).
    if (vmg > 0.1) {
      const double hours = m_dtw / vmg;
      const int total_min = static_cast<int>(std::lround(hours * 60.0));
      m_eta_text = total_min >= 60
                       ? QStringLiteral("%1h %2m")
                             .arg(total_min / 60)
                             .arg(total_min % 60, 2, 10, QChar('0'))
                       : QStringLiteral("%1 min").arg(total_min);
    } else {
      m_eta_text = QStringLiteral("--");
    }
  } else {
    m_vmg_text = QStringLiteral("--");
    m_eta_text = QStringLiteral("--");
  }

  // Leg "n / total": 1-based active-point index over the point count.
  const int idx = wp ? r->GetIndexOf(wp) : -1;  // 0-based, -1 if not found
  if (idx >= 0)
    m_leg_text = QStringLiteral("%1 / %2").arg(idx + 1).arg(r->GetnPoints());
  else
    m_leg_text = QString();
}

}  // namespace ocpn::qtui
