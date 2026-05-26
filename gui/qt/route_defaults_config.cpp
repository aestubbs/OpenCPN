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
  m_waypoint_icon = c.getString("routes/waypointIcon", m_waypoint_icon);
  m_routepoint_icon = c.getString("routes/routepointIcon", m_routepoint_icon);
  m_arrival_nm = c.getDouble("routes/arrivalNm", m_arrival_nm);
  m_scamin_min = c.getInt("routes/scaminMin", m_scamin_min);
  m_scamin_max = c.getInt("routes/scaminMax", m_scamin_max);
  m_track_auto_daily = c.getInt("routes/trackAutoDaily", m_track_auto_daily);
  m_track_highlight = c.getBool("routes/trackHighlight", m_track_highlight);
  const QString tc = c.getString("routes/trackColor");
  if (!tc.isEmpty()) m_track_color = QColor(tc);
  m_tracking_precision =
      c.getInt("routes/trackingPrecision", m_tracking_precision);
}

// guard, store, persist, notify.
#define OCPN_RT_SET(member, value, key, putter) \
  if ((member) == (value)) return;              \
  (member) = (value);                           \
  ConfigStore::instance().putter(key, value);   \
  Q_EMIT changed();

void RouteDefaultsConfig::setRouteColor(const QColor& v) {
  if (m_route_color == v) return;
  m_route_color = v;
  ConfigStore::instance().setString("routes/routeColor", v.name());
  Q_EMIT changed();
}
void RouteDefaultsConfig::setRouteStyle(int v) {
  OCPN_RT_SET(m_route_style, v, "routes/routeStyle", setInt)
}
void RouteDefaultsConfig::setPersistActiveRoute(bool v) {
  OCPN_RT_SET(m_persist_active, v, "routes/persistActive", setBool)
}
void RouteDefaultsConfig::setWaypointIcon(const QString& v) {
  OCPN_RT_SET(m_waypoint_icon, v, "routes/waypointIcon", setString)
}
void RouteDefaultsConfig::setRoutepointIcon(const QString& v) {
  OCPN_RT_SET(m_routepoint_icon, v, "routes/routepointIcon", setString)
}
void RouteDefaultsConfig::setArrivalCircleNm(double v) {
  OCPN_RT_SET(m_arrival_nm, v, "routes/arrivalNm", setDouble)
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
  OCPN_RT_SET(m_track_highlight, v, "routes/trackHighlight", setBool)
}
void RouteDefaultsConfig::setTrackColor(const QColor& v) {
  if (m_track_color == v) return;
  m_track_color = v;
  ConfigStore::instance().setString("routes/trackColor", v.name());
  Q_EMIT changed();
}
void RouteDefaultsConfig::setTrackingPrecision(int v) {
  OCPN_RT_SET(m_tracking_precision, v, "routes/trackingPrecision", setInt)
}

#undef OCPN_RT_SET

}  // namespace ocpn::qtui
