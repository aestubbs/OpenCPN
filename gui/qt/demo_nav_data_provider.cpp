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

namespace ocpn::qtui {

namespace {
constexpr double kDegToRad = M_PI / 180.0;
constexpr double kNmPerDegLat = 60.0;  // 1 deg latitude = 60 nautical miles
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

DemoNavDataProvider::DemoNavDataProvider(QObject* parent)
    : NavDataProvider(parent) {
  // Own ship: mid-test-area, making way north-east.
  m_own = {true, 52.5, 5.0, 50.0, 8.0, 50.0};

  // A few AIS targets on assorted courses around the own ship.
  m_targets = {
      {244010001, 52.70, 4.80, 135.0, 12.0, 135.0, QStringLiteral("ALPHA")},
      {244010002, 52.30, 5.40, 300.0, 18.0, 300.0, QStringLiteral("BRAVO")},
      {244010003, 52.55, 5.30, 220.0, 6.0, 220.0, QStringLiteral("CHARLIE")},
      {244010004, 52.40, 4.70, 20.0, 22.0, 20.0, QStringLiteral("DELTA")},
  };

  // A static demo route, a few waypoints, and one track.
  NavRoute r;
  r.name = QStringLiteral("Demo Route");
  r.points = {{4.6, 52.2}, {5.0, 52.6}, {5.6, 52.7}, {6.0, 53.0}};
  m_routes = {r};

  m_waypoints = {
      {QStringLiteral("WP1"), 52.2, 4.6},
      {QStringLiteral("WP2"), 52.7, 5.6},
      {QStringLiteral("Harbour"), 53.0, 6.0},
  };

  NavTrack t;
  t.points = {{4.9, 52.35}, {4.95, 52.40}, {5.0, 52.45}, {5.02, 52.50}};
  m_tracks = {t};

  m_timer = new QTimer(this);
  m_timer->setInterval(200);  // 5 Hz
  connect(m_timer, &QTimer::timeout, this, &DemoNavDataProvider::tick);
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

  Q_EMIT changed();
}

}  // namespace ocpn::qtui
