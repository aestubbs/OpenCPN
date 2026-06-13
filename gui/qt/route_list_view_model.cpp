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

#include <algorithm>
#include <cmath>
#include <limits>

#include <QVariantMap>

#include "nav_data_provider.h"

namespace ocpn::qtui {

namespace {
// Great-circle distance in nautical miles between two (lat, lon) points.
double haversineNm(double lat1, double lon1, double lat2, double lon2) {
  const double d2r = M_PI / 180.0;
  const double dlat = (lat2 - lat1) * d2r, dlon = (lon2 - lon1) * d2r;
  const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                   std::cos(lat1 * d2r) * std::cos(lat2 * d2r) *
                       std::sin(dlon / 2) * std::sin(dlon / 2);
  return 3440.065 * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}
}  // namespace

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
    emit changed();
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
    double length_nm = 0.0;
    for (int i = 1; i < r.points.size(); ++i)
      length_nm += haversineNm(r.points[i - 1].y(), r.points[i - 1].x(),
                               r.points[i].y(), r.points[i].x());
    QVariantMap m;
    m["name"] = r.name.isEmpty() ? QStringLiteral("Route %1").arg(idx + 1)
                                 : r.name;
    m["guid"] = r.guid;
    m["points"] = static_cast<int>(r.points.size());
    m["lengthNm"] = length_nm;
    m["north"] = n;
    m["south"] = s;
    m["east"] = e;
    m["west"] = w;
    m["active"] = r.active;  // currently being followed (P3.16)
    m_routes.append(m);
    ++idx;
  }

  const OwnShipState own = m_provider->ownShip();
  for (const NavWaypoint& wp : m_provider->waypoints()) {
    QVariantMap m;
    m["name"] = wp.name.isEmpty() ? QStringLiteral("Waypoint") : wp.name;
    m["guid"] = wp.guid;
    m["comment"] = wp.comment;
    m["icon"] = wp.iconName;
    m["lat"] = wp.lat;
    m["lon"] = wp.lon;
    m["visible"] = wp.visible;
    m["createTimeMs"] = wp.createTimeMs;
    m["rangeNm"] = own.valid ? haversineNm(own.lat, own.lon, wp.lat, wp.lon)
                             : -1.0;
    // Per-mark range rings + SCAMIN override (P3.6) for the mark editor.
    m["showRings"] = wp.showRings;
    m["ringCount"] = wp.ringCount;
    m["ringStep"] = wp.ringStep;
    m["ringUnits"] = wp.ringUnits;
    m["scamin"] = static_cast<qlonglong>(wp.scamin);
    m_waypoints.append(m);
  }
  // Sort: most-recent-first (createTimeMs desc) or nearest-first (rangeNm asc;
  // unknown range sinks to the end).
  std::sort(m_waypoints.begin(), m_waypoints.end(),
            [this](const QVariant& a, const QVariant& b) {
              const QVariantMap ma = a.toMap(), mb = b.toMap();
              if (m_mark_sort == 1) {
                const double ra = ma.value("rangeNm").toDouble();
                const double rb = mb.value("rangeNm").toDouble();
                const double ka = ra < 0 ? 1e18 : ra;
                const double kb = rb < 0 ? 1e18 : rb;
                return ka < kb;
              }
              return ma.value("createTimeMs").toLongLong() >
                     mb.value("createTimeMs").toLongLong();
            });

  // Tracks: newest-first (by start time). The active recording sorts to the top.
  m_tracks.clear();
  const QList<NavTrack> tks = m_provider->tracks();
  for (const NavTrack& t : tks) {
    QVariantMap m;
    m["name"] = t.name;
    m["guid"] = t.guid;
    m["lengthNm"] = t.lengthNm;
    m["startTimeMs"] = t.startTimeMs;
    m["visible"] = t.visible;
    m["active"] = t.active;
    m_tracks.append(m);
  }
  std::sort(m_tracks.begin(), m_tracks.end(),
            [](const QVariant& a, const QVariant& b) {
              const QVariantMap ma = a.toMap(), mb = b.toMap();
              if (ma.value("active").toBool() != mb.value("active").toBool())
                return ma.value("active").toBool();  // active first
              return ma.value("startTimeMs").toLongLong() >
                     mb.value("startTimeMs").toLongLong();
            });
  m_track_count = static_cast<int>(tks.size());
  emit changed();
}

void RouteListViewModel::setMarkSortMode(int mode) {
  if (mode == m_mark_sort) return;
  m_mark_sort = mode;
  refresh();  // re-sort + republish
}

}  // namespace ocpn::qtui
