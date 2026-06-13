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
 * Implement demo_nav_data_provider.h.
 */

#include "demo_nav_data_provider.h"

#include <cmath>

#include <QTimer>

#include "viewport.h"

namespace ocpn::qtui {

namespace {
constexpr double kDegToRad = M_PI / 180.0;
constexpr double kNmPerDegLat = 60.0;  // 1 deg latitude = 60 nautical miles

// Synthetic fleet layout, as fractions of the view span (so it scales with
// zoom). dLat/dLon are offsets from the seed centre; course/speed in deg/kn.
struct TargetSeed {
  double dlat, dlon, cog, sog;
  int mmsi;
  const char* name;
};
constexpr TargetSeed kTargetSeeds[] = {
    {+0.25, -0.25, 135.0, 12.0, 244010001, "ALPHA"},
    {-0.25, +0.45, 300.0, 18.0, 244010002, "BRAVO"},
    {+0.08, +0.35, 220.0, 6.0, 244010003, "CHARLIE"},
    {-0.12, -0.35, 20.0, 22.0, 244010004, "DELTA"},
};
}  // namespace

void DemoNavDataProvider::advance(double& lat, double& lon, double cog,
                                  double sog, double seconds) {
  if (sog <= 0.0 || seconds <= 0.0) return;
  const double dist_nm = sog * (seconds / 3600.0);  // knots * hours
  const double dist_deg_lat = dist_nm / kNmPerDegLat;
  const double th = cog * kDegToRad;  // 0 = north, clockwise
  lat += dist_deg_lat * std::cos(th);
  const double coslat = std::cos(lat * kDegToRad);
  lon += (coslat > 1e-6) ? dist_deg_lat * std::sin(th) / coslat : 0.0;
}

DemoNavDataProvider::DemoNavDataProvider(const Viewport* viewport,
                                         QObject* parent)
    : NavDataProvider(parent), m_viewport(viewport) {
  // Seed an initial fleet around the viewport's current centre (or a sane
  // default). ChartCanvas re-seeds via seedAround() once the charts fit /
  // when demo mode is toggled or "drop demo here" is invoked.
  const double lat = m_viewport ? m_viewport->centerLat() : 52.5;
  const double lon = m_viewport ? m_viewport->centerLon() : 5.0;
  seedAround(lat, lon);

  m_timer = new QTimer(this);
  m_timer->setInterval(200);  // 5 Hz
  connect(m_timer, &QTimer::timeout, this, &DemoNavDataProvider::tick);
}

void DemoNavDataProvider::seedAround(double lat, double lon) {
  // Span (degrees) across a typical view at the current zoom; the layout is
  // placed as fractions of this so it fills the view at any scale.
  const double s = m_viewport ? m_viewport->scale() : 0.0;
  const double span = (s > 0.0) ? 700.0 / s : 8.0;

  m_own = {true, lat, lon, 50.0, 8.0, 50.0};  // own ship dead centre

  m_targets.clear();
  for (const TargetSeed& ts : kTargetSeeds) {
    AisTarget t;
    t.mmsi = ts.mmsi;
    t.lat = lat + ts.dlat * span;
    t.lon = lon + ts.dlon * span;
    t.cog = ts.cog;
    t.sog = ts.sog;
    t.hdg = ts.cog;
    t.name = QString::fromLatin1(ts.name);
    m_targets.append(t);
  }

  // Route / waypoints / track, as (lon, lat) offsets * span.
  const auto P = [&](double dlon, double dlat) {
    return QPointF(lon + dlon * span, lat + dlat * span);
  };
  NavRoute r;
  r.name = QStringLiteral("Demo Route");
  r.points = {P(-0.45, -0.35), P(0.0, 0.10), P(0.45, 0.25), P(0.70, 0.45)};
  m_routes = {r};

  const auto WP = [](const QString& nm, double la, double lo) {
    NavWaypoint w;
    w.name = nm;
    w.lat = la;
    w.lon = lo;
    return w;
  };
  m_waypoints = {
      WP(QStringLiteral("WP1"), lat - 0.35 * span, lon - 0.45 * span),
      WP(QStringLiteral("WP2"), lat + 0.25 * span, lon + 0.50 * span),
      WP(QStringLiteral("Harbour"), lat + 0.50 * span, lon + 0.70 * span),
  };

  NavTrack t;
  t.points = {P(-0.10, -0.15), P(-0.05, -0.10), P(0.0, -0.05), P(0.02, 0.0)};
  m_tracks = {t};

  emit dynamicChanged();
  emit staticChanged();
}

void DemoNavDataProvider::setRunning(bool run) {
  if (run == running()) return;
  if (run) {
    m_clock.start();
    m_last_ms = 0;
    m_timer->start();
  } else {
    m_timer->stop();
  }
}

bool DemoNavDataProvider::running() const {
  return m_timer && m_timer->isActive();
}

void DemoNavDataProvider::tick() {
  const qint64 now = m_clock.elapsed();
  double dt = (now - m_last_ms) / 1000.0;
  m_last_ms = now;
  if (dt <= 0.0 || dt > 5.0) dt = m_timer->interval() / 1000.0;

  advance(m_own.lat, m_own.lon, m_own.cog, m_own.sog, dt);
  for (AisTarget& tgt : m_targets)
    advance(tgt.lat, tgt.lon, tgt.cog, tgt.sog, dt);

  // Only the dynamic data moves; the demo route/track/waypoints are static,
  // so we never emit staticChanged() (those layers build once).
  emit dynamicChanged();
}

}  // namespace ocpn::qtui
