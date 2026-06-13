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
 * Implement route_defaults_config.h.
 */

#include "route_defaults_config.h"

#include "config_store.h"
#include "model/gui_vars.h"  // g_bAdvanceRouteWaypointOnArrivalOnly
#include "model/config_vars.h"  // route/track model globals (see below)
#include "model/wx_qt_string.h"  // QString_to_wxString (icon name globals)

namespace ocpn::qtui {

RouteDefaultsConfig& RouteDefaultsConfig::instance() {
  static RouteDefaultsConfig s;
  return s;
}

RouteDefaultsConfig::RouteDefaultsConfig() {
  ConfigStore& c = ConfigStore::instance();
  const QString rc = c.getString("routes/routeColor");
  if (!rc.isEmpty()) m_route_color = QColor(rc);
  m_route_style = c.getInt("routes/routeStyle", m_route_style);
  m_persist_active = c.getBool("routes/persistActive", m_persist_active);
  m_auto_anchor_mark = c.getBool("routes/autoAnchorMark", m_auto_anchor_mark);
  m_waypoint_icon = c.getString("routes/waypointIcon", m_waypoint_icon);
  m_routepoint_icon = c.getString("routes/routepointIcon", m_routepoint_icon);
  m_arrival_nm = c.getDouble("routes/arrivalNm", m_arrival_nm);
  // Mirror into the model global that Routeman::UpdateProgress() reads for
  // arrival detection (it gates on radius > 0). Without this it stays 0.0 and
  // no waypoint ever "arrives". RoutePoint::GetWaypointArrivalRadius() heals
  // any 0-radius points to this value, so existing routes pick it up too.
  g_n_arrival_circle_radius = m_arrival_nm;
  m_scamin_min = c.getInt("routes/scaminMin", m_scamin_min);
  m_scamin_max = c.getInt("routes/scaminMax", m_scamin_max);
  m_track_auto_daily = c.getInt("routes/trackAutoDaily", m_track_auto_daily);
  m_track_highlight = c.getBool("routes/trackHighlight", m_track_highlight);
  const QString tc = c.getString("routes/trackColor");
  if (!tc.isEmpty()) m_track_color = QColor(tc);
  m_tracking_precision =
      c.getInt("routes/trackingPrecision", m_tracking_precision);
  m_confirm_delete = c.getBool("routes/confirmDelete", m_confirm_delete);
  m_advance_arrival_only =
      c.getBool("routes/advanceArrivalOnly", m_advance_arrival_only);
  g_bAdvanceRouteWaypointOnArrivalOnly = m_advance_arrival_only;

  // Mirror the persisted defaults into the model globals the nav / track code
  // reads, so the dialog actually drives behaviour (not just persistence).
  g_persist_active_route = m_persist_active;
  g_nTrackPrecision = m_tracking_precision;
  g_bHighliteTracks = m_track_highlight;
  g_default_wp_icon = QString_to_wxString(m_waypoint_icon);
  g_default_routepoint_icon = QString_to_wxString(m_routepoint_icon);
}

// guard, store, persist, notify.
#define OCPN_RT_SET(member, value, key, putter) \
  if ((member) == (value)) return;              \
  (member) = (value);                           \
  ConfigStore::instance().putter(key, value);   \
  emit changed();

void RouteDefaultsConfig::setRouteColor(const QColor& v) {
  if (m_route_color == v) return;
  m_route_color = v;
  ConfigStore::instance().setString("routes/routeColor", v.name());
  emit changed();
}
void RouteDefaultsConfig::setRouteStyle(int v) {
  OCPN_RT_SET(m_route_style, v, "routes/routeStyle", setInt)
}
void RouteDefaultsConfig::setPersistActiveRoute(bool v) {
  if (m_persist_active == v) return;
  m_persist_active = v;
  g_persist_active_route = v;
  ConfigStore::instance().setBool("routes/persistActive", v);
  emit changed();
}
void RouteDefaultsConfig::setAutoAnchorMark(bool v) {
  if (m_auto_anchor_mark == v) return;
  m_auto_anchor_mark = v;
  ConfigStore::instance().setBool("routes/autoAnchorMark", v);
  emit changed();
}
void RouteDefaultsConfig::setWaypointIcon(const QString& v) {
  if (m_waypoint_icon == v) return;
  m_waypoint_icon = v;
  g_default_wp_icon = QString_to_wxString(v);
  ConfigStore::instance().setString("routes/waypointIcon", v);
  emit changed();
}
void RouteDefaultsConfig::setRoutepointIcon(const QString& v) {
  if (m_routepoint_icon == v) return;
  m_routepoint_icon = v;
  g_default_routepoint_icon = QString_to_wxString(v);
  ConfigStore::instance().setString("routes/routepointIcon", v);
  emit changed();
}
void RouteDefaultsConfig::setArrivalCircleNm(double v) {
  if (m_arrival_nm == v) return;
  m_arrival_nm = v;
  g_n_arrival_circle_radius = v;  // keep the model's arrival radius in step
  ConfigStore::instance().setDouble("routes/arrivalNm", v);
  emit changed();
}
void RouteDefaultsConfig::setScaminMin(int v) {
  OCPN_RT_SET(m_scamin_min, v, "routes/scaminMin", setInt)
}
void RouteDefaultsConfig::setScaminMax(int v) {
  OCPN_RT_SET(m_scamin_max, v, "routes/scaminMax", setInt)
}
void RouteDefaultsConfig::setTrackAutoDaily(int v) {
  OCPN_RT_SET(m_track_auto_daily, v, "routes/trackAutoDaily", setInt)
}
void RouteDefaultsConfig::setTrackHighlight(bool v) {
  if (m_track_highlight == v) return;
  m_track_highlight = v;
  g_bHighliteTracks = v;
  ConfigStore::instance().setBool("routes/trackHighlight", v);
  emit changed();
}
void RouteDefaultsConfig::setTrackColor(const QColor& v) {
  if (m_track_color == v) return;
  m_track_color = v;
  ConfigStore::instance().setString("routes/trackColor", v.name());
  emit changed();
}
void RouteDefaultsConfig::setTrackingPrecision(int v) {
  if (m_tracking_precision == v) return;
  m_tracking_precision = v;
  g_nTrackPrecision = v;  // the model ActiveTrack reads this on Start()
  ConfigStore::instance().setInt("routes/trackingPrecision", v);
  emit changed();
}

void RouteDefaultsConfig::setConfirmObjectDelete(bool v) {
  if (m_confirm_delete == v) return;
  m_confirm_delete = v;
  ConfigStore::instance().setBool("routes/confirmDelete", v);
  emit changed();
}

void RouteDefaultsConfig::setAdvanceOnArrivalOnly(bool v) {
  if (m_advance_arrival_only == v) return;
  m_advance_arrival_only = v;
  g_bAdvanceRouteWaypointOnArrivalOnly = v;  // Routeman reads this per tick
  ConfigStore::instance().setBool("routes/advanceArrivalOnly", v);
  emit changed();
}

#undef OCPN_RT_SET

}  // namespace ocpn::qtui
