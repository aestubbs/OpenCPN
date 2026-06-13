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
#include <cmath>

#include <QTimer>

#include "model/gui_vars.h"  // gTimeSource (shared display-time spine)

namespace ocpn::qtui {

TimeController::TimeController(QObject* parent) : QObject(parent) {
  m_time = QDateTime::currentDateTime();
  m_timer = new QTimer(this);
  m_timer->setInterval(1000);  // 1 Hz live tick (100 ms while playing)
  connect(m_timer, &QTimer::timeout, this, &TimeController::onTick);
  m_timer->start();
  applyToGlobal();
}

TimeController::~TimeController() = default;

TimeController& TimeController::instance() {
  static TimeController s;
  return s;
}

double TimeController::nowFraction() const {
  const qint64 a = windowStart().toSecsSinceEpoch();
  const double now =
      static_cast<double>(QDateTime::currentDateTime().toSecsSinceEpoch());
  return (now - static_cast<double>(a)) / m_window_secs;
}

QString TimeController::timeLabel() const {
  return m_time.toString(QStringLiteral("HH:mm"));
}

QString TimeController::dateLabel() const {
  return m_time.toString(QStringLiteral("ddd dd MMM"));
}

void TimeController::applyToGlobal() {
  // Renderers read gTimeSource: an invalid value means "live / now".
  gTimeSource = m_live ? QDateTime() : m_time;
}

void TimeController::enterScrub() {
  if (m_playing) {
    m_playing = false;
    if (m_timer) m_timer->setInterval(1000);
  }
  const bool was_live = m_live;
  m_live = false;
  if (was_live) emit modeChanged();  // playing already cleared above
}

void TimeController::goLive() {
  m_playing = false;
  if (m_timer) m_timer->setInterval(1000);
  m_live = true;
  m_time = QDateTime::currentDateTime();
  applyToGlobal();
  emit modeChanged();
  emit timeChanged();
  emit windowChanged();
}

void TimeController::setDisplayTime(const QDateTime& t) {
  if (!t.isValid()) {
    goLive();
    return;
  }
  enterScrub();
  m_time = t;
  applyToGlobal();
  emit timeChanged();
  emit windowChanged();
}

void TimeController::panSeconds(double secs) {
  enterScrub();
  m_time = m_time.addSecs(static_cast<qint64>(std::llround(secs)));
  applyToGlobal();
  emit timeChanged();
  emit windowChanged();
}

void TimeController::panPixels(double dx, double widthPx) {
  if (widthPx <= 0.0) return;
  panSeconds(-dx * (m_window_secs / widthPx));  // drag right -> earlier
}

void TimeController::setDisplayFraction(double x) {
  x = std::clamp(x, 0.0, 1.0);
  const qint64 a = windowStart().toSecsSinceEpoch();
  setDisplayTime(QDateTime::fromSecsSinceEpoch(
      a + static_cast<qint64>(x * m_window_secs)));
}

void TimeController::stepMinutes(int mins) {
  setDisplayTime(m_time.addSecs(static_cast<qint64>(mins) * 60));
}

void TimeController::play() {
  m_live = false;
  m_playing = true;
  if (m_timer) m_timer->setInterval(100);  // smooth animation while playing
  applyToGlobal();
  emit modeChanged();
}

void TimeController::pause() {
  if (!m_playing) return;
  m_playing = false;
  if (m_timer) m_timer->setInterval(1000);  // back to the 1 Hz live tick
  emit modeChanged();
}

void TimeController::togglePlay() { m_playing ? pause() : play(); }

void TimeController::onTick() {
  if (m_playing) {
    m_time = m_time.addSecs(static_cast<qint64>(m_play_minutes_per_tick) * 60);
    applyToGlobal();
    emit timeChanged();
    emit windowChanged();  // window scrolls forward with displayTime
  } else if (m_live) {
    m_time = QDateTime::currentDateTime();
    emit timeChanged();
    emit windowChanged();  // gTimeSource stays invalid while live
  }
}

}  // namespace ocpn::qtui
