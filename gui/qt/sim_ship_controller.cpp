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
 * Implement sim_ship_controller.h.
 */

#include "sim_ship_controller.h"

#include <algorithm>
#include <cmath>

#include <QTimer>

#include "model/comm_bridge.h"  // pet the watchdogs so our globals aren't nulled
#include "model/own_ship.h"  // gLat/gLon/gCog/gHdt/gSog/bGPSValid

namespace ocpn::qtui {

namespace {
constexpr double kDegToRad = M_PI / 180.0;
constexpr double kNmPerDegLat = 60.0;
constexpr int kTickMs = 250;  // 4 Hz, matching the model-poll mirror

double wrap360(double deg) {
  deg = std::fmod(deg, 360.0);
  if (deg < 0.0) deg += 360.0;
  return deg;
}
}  // namespace

SimShipController::SimShipController(QObject* parent) : QObject(parent) {
  m_timer = new QTimer(this);
  m_timer->setInterval(kTickMs);
  connect(m_timer, &QTimer::timeout, this, &SimShipController::tick);
}

SimShipController::~SimShipController() = default;

void SimShipController::advance(double& lat, double& lon, double course,
                                double knots, double seconds) {
  if (knots <= 0.0 || seconds <= 0.0) return;
  const double dist_nm = knots * (seconds / 3600.0);
  const double dist_deg_lat = dist_nm / kNmPerDegLat;
  const double th = course * kDegToRad;  // 0 = north, clockwise
  lat += dist_deg_lat * std::cos(th);
  const double coslat = std::cos(lat * kDegToRad);
  lon += (coslat > 1e-6) ? dist_deg_lat * std::sin(th) / coslat : 0.0;
}

void SimShipController::writeGlobals() {
  gLat = m_lat;
  gLon = m_lon;
  gCog = m_course;
  gHdt = m_course;  // simulated heading == course made good
  gSog = m_running ? m_speed : 0.0;
  bGPSValid = true;
  // We wrote the globals directly, bypassing the comm message pipeline; pet the
  // watchdogs so CommBridge's ~1 Hz timer doesn't null gCog/gSog/gHdt back out
  // from under us (which made the own-ship icon snap to north once a second).
  CommBridge::GetInstance().PetWatchdogs();
}

void SimShipController::setActive(bool on) {
  if (on == m_active) return;
  m_active = on;
  if (on) {
    m_last_ms = 0;
    m_clock.start();
    if (m_placed) writeGlobals();  // show the boat immediately where placed
    m_timer->start();
  } else {
    m_timer->stop();
    m_running = false;
  }
  emit changed();
}

void SimShipController::place(double lat, double lon) {
  m_lat = lat;
  m_lon = lon;
  m_placed = true;
  if (!m_active) setActive(true);  // placing implies we want the test ship live
  writeGlobals();
  emit changed();
  emit ticked();  // let the canvas recentre / repaint on the new fix
}

void SimShipController::setCourse(double deg) {
  m_course = wrap360(deg);
  if (m_active) writeGlobals();
  emit changed();
}

void SimShipController::steer(double delta_deg) { setCourse(m_course + delta_deg); }

void SimShipController::setSpeed(double knots) {
  m_speed = std::clamp(knots, 0.0, kMaxSpeed);
  if (m_active) writeGlobals();
  emit changed();
}

void SimShipController::throttle(double delta_knots) {
  setSpeed(m_speed + delta_knots);
}

void SimShipController::setRunning(bool on) {
  if (on == m_running) return;
  m_running = on && m_placed;  // can't make way before being placed
  // Reset the integration clock so a long pause doesn't jump the position.
  m_last_ms = m_clock.isValid() ? m_clock.elapsed() : 0;
  if (m_active) writeGlobals();
  emit changed();
}

void SimShipController::tick() {
  const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
  double dt = (now - m_last_ms) / 1000.0;
  m_last_ms = now;
  if (dt <= 0.0 || dt > 5.0) dt = kTickMs / 1000.0;  // clamp clock glitches

  if (m_running && m_placed)
    advance(m_lat, m_lon, m_course, m_speed, dt);

  writeGlobals();
  emit ticked();
  if (m_running) emit changed();  // position/HUD moved
}

}  // namespace ocpn::qtui
