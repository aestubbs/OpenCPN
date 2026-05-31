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
 * Implement time_controller.h.
 */

#include "time_controller.h"

#include <algorithm>

#include <QTimer>

#include "model/gui_vars.h"  // gTimeSource (shared display-time spine)

namespace ocpn::qtui {

namespace {
constexpr double kDaySecs = 24.0 * 3600.0;
}

TimeController::TimeController(QObject* parent) : QObject(parent) {
  m_time = QDateTime::currentDateTime();
  recenterWindow();
  m_timer = new QTimer(this);
  m_timer->setInterval(1000);  // 1 Hz: advance the live clock / play animation
  connect(m_timer, &QTimer::timeout, this, &TimeController::onTick);
  m_timer->start();
  applyToGlobal();
}

TimeController::~TimeController() = default;

void TimeController::recenterWindow() {
  const QDate d =
      (m_time.isValid() ? m_time : QDateTime::currentDateTime()).date();
  m_window_start = d.startOfDay();  // local midnight of the shown day
  Q_EMIT windowChanged();
}

double TimeController::position() const {
  const qint64 a = m_window_start.toSecsSinceEpoch();
  return std::clamp(
      static_cast<double>(m_time.toSecsSinceEpoch() - a) / kDaySecs, 0.0, 1.0);
}

double TimeController::nowPosition() const {
  const qint64 a = m_window_start.toSecsSinceEpoch();
  return static_cast<double>(
             QDateTime::currentDateTime().toSecsSinceEpoch() - a) /
         kDaySecs;
}

QString TimeController::dateLabel() const {
  return m_window_start.toString(QStringLiteral("ddd dd MMM"));
}

QString TimeController::timeLabel() const {
  return m_time.toString(QStringLiteral("HH:mm"));
}

void TimeController::applyToGlobal() {
  // Renderers read gTimeSource: an invalid value means "live / now".
  gTimeSource = m_live ? QDateTime() : m_time;
}

void TimeController::goLive() {
  m_playing = false;
  m_live = true;
  m_time = QDateTime::currentDateTime();
  recenterWindow();
  applyToGlobal();
  Q_EMIT modeChanged();
  Q_EMIT timeChanged();
}

void TimeController::setDisplayTime(const QDateTime& t) {
  if (!t.isValid()) {
    goLive();
    return;
  }
  m_live = false;
  m_time = t;
  // Keep the playhead in view: follow the window to the day we landed on.
  if (m_time.date() != m_window_start.date()) recenterWindow();
  applyToGlobal();
  Q_EMIT modeChanged();
  Q_EMIT timeChanged();
}

void TimeController::setPosition(double p) {
  p = std::clamp(p, 0.0, 1.0);
  const qint64 a = m_window_start.toSecsSinceEpoch();
  setDisplayTime(
      QDateTime::fromSecsSinceEpoch(a + static_cast<qint64>(p * kDaySecs)));
}

void TimeController::stepMinutes(int mins) {
  setDisplayTime(m_time.addSecs(static_cast<qint64>(mins) * 60));
}

void TimeController::stepDays(int days) {
  setDisplayTime(m_time.addDays(days));  // recenterWindow() follows the date
}

void TimeController::play() {
  m_live = false;  // animate from the current display time
  m_playing = true;
  applyToGlobal();
  Q_EMIT modeChanged();
}

void TimeController::pause() {
  if (!m_playing) return;
  m_playing = false;
  Q_EMIT modeChanged();
}

void TimeController::togglePlay() { m_playing ? pause() : play(); }

void TimeController::onTick() {
  if (m_playing) {
    m_time = m_time.addSecs(static_cast<qint64>(m_play_minutes_per_tick) * 60);
    if (m_time >= windowEnd()) recenterWindow();  // roll into the next day
    applyToGlobal();
    Q_EMIT timeChanged();
  } else if (m_live) {
    m_time = QDateTime::currentDateTime();
    if (m_time.date() != m_window_start.date()) recenterWindow();  // past midnight
    Q_EMIT timeChanged();  // gTimeSource stays invalid while live
  }
}

}  // namespace ocpn::qtui
