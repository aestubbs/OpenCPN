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
 * Implement route_list_view_model.h.
 */

#include "route_list_view_model.h"

#include <limits>

#include <QVariantMap>

#include "nav_data_provider.h"

namespace ocpn::qtui {

RouteListViewModel::RouteListViewModel(NavDataProvider* provider,
                                       QObject* parent)
    : QObject(parent), m_provider(provider) {
  if (m_provider) {
    connect(m_provider, &NavDataProvider::staticChanged, this,
            &RouteListViewModel::refresh);
    refresh();
  }
}

void RouteListViewModel::refresh() {
  m_routes.clear();
  m_waypoints.clear();
  if (!m_provider) {
    Q_EMIT changed();
    return;
  }

  int idx = 0;
  for (const NavRoute& r : m_provider->routes()) {
    double n = -90, s = 90, e = -180, w = 180;
    for (const QPointF& p : r.points) {  // (lon, lat)
      n = std::max(n, p.y());
      s = std::min(s, p.y());
      e = std::max(e, p.x());
      w = std::min(w, p.x());
    }
    QVariantMap m;
    m["name"] = r.name.isEmpty() ? QStringLiteral("Route %1").arg(idx + 1)
                                 : r.name;
    m["points"] = static_cast<int>(r.points.size());
    m["north"] = n;
    m["south"] = s;
    m["east"] = e;
    m["west"] = w;
    m_routes.append(m);
    ++idx;
  }

  for (const NavWaypoint& wp : m_provider->waypoints()) {
    QVariantMap m;
    m["name"] = wp.name.isEmpty() ? QStringLiteral("Waypoint") : wp.name;
    m["lat"] = wp.lat;
    m["lon"] = wp.lon;
    m_waypoints.append(m);
  }

  m_track_count = static_cast<int>(m_provider->tracks().size());
  Q_EMIT changed();
}

}  // namespace ocpn::qtui
