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
 * TimeController -- the single "display time" clock for every time-aware
 * overlay (tides, currents, and -- once ported -- GRIB weather). It owns the
 * display time and writes it through to the model's shared `gTimeSource`
 * (QDateTime), which renderers read; an *invalid* gTimeSource means "live /
 * now". The bottom timeline HUD binds to this singleton, and the (future)
 * tide/current Layer + graph sample at displayTime().
 *
 * The timeline shows ONE day at a time: the window is the local-midnight day
 * containing displayTime(), 24 h wide. position() is displayTime()'s 0..1 spot
 * across that day; stepDays() pages to adjacent days. Modes: LIVE (tracks the
 * real clock; gTimeSource invalid), SCRUB (a user-set time; gTimeSource = that
 * time), PLAY (animates forward, rolling into the next day). P3.14 phase C.
 */

#ifndef OCPN_QT_TIME_CONTROLLER_H_
#define OCPN_QT_TIME_CONTROLLER_H_

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QString>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class TimeController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QDateTime displayTime READ displayTime NOTIFY timeChanged)
  Q_PROPERTY(bool live READ live NOTIFY modeChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY modeChanged)
  Q_PROPERTY(QDateTime windowStart READ windowStart NOTIFY windowChanged)
  Q_PROPERTY(QDateTime windowEnd READ windowEnd NOTIFY windowChanged)
  // displayTime as a 0..1 fraction across the 24 h day window. WRITE scrubs.
  Q_PROPERTY(double position READ position WRITE setPosition NOTIFY timeChanged)
  // The live clock's 0..1 spot in the window (for the "now" marker); outside
  // [0,1] when the shown day is not today.
  Q_PROPERTY(double nowPosition READ nowPosition NOTIFY timeChanged)
  Q_PROPERTY(QString dateLabel READ dateLabel NOTIFY windowChanged)  // "Sat 31 May"
  Q_PROPERTY(QString timeLabel READ timeLabel NOTIFY timeChanged)    // "14:37"

public:
  explicit TimeController(QObject* parent = nullptr);
  ~TimeController() override;

  QDateTime displayTime() const { return m_time; }
  bool live() const { return m_live; }
  bool playing() const { return m_playing; }
  QDateTime windowStart() const { return m_window_start; }
  QDateTime windowEnd() const { return m_window_start.addDays(1); }
  double position() const;
  double nowPosition() const;
  QString dateLabel() const;
  QString timeLabel() const;

  /** Snap to the real clock and resume live tracking (gTimeSource invalid). */
  Q_INVOKABLE void goLive();
  /** Set an explicit display time (enters scrub). Invalid -> goLive(). */
  Q_INVOKABLE void setDisplayTime(const QDateTime& t);
  /** Nudge the display time by +/- minutes (enters scrub). */
  Q_INVOKABLE void stepMinutes(int mins);
  /** Page the window to an adjacent day, keeping the time of day (enters scrub). */
  Q_INVOKABLE void stepDays(int days);
  /** Animate forward from the current time. */
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void togglePlay();

  /** Property WRITE: 0..1 across the day -> scrub. (Use `position = x` in QML.) */
  void setPosition(double p);

Q_SIGNALS:
  void timeChanged();   // displayTime / position / nowPosition / timeLabel
  void modeChanged();   // live / playing
  void windowChanged(); // the day window moved (dateLabel)

private:
  void applyToGlobal();   // push m_time/m_live into model gTimeSource
  void recenterWindow();  // window = local-midnight day of displayTime
  void onTick();          // 1 Hz live-track / play advance

  QDateTime m_time;          // current display time
  QDateTime m_window_start;  // local midnight of the shown day
  bool m_live = true;
  bool m_playing = false;
  int m_play_minutes_per_tick = 20;  // animation speed
  QTimer* m_timer = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_TIME_CONTROLLER_H_
