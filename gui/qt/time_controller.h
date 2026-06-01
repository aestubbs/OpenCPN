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
 * overlay (tides, currents, and -- once ported -- GRIB weather), shared between
 * C++ (the tide Layer / graph) and QML (the timeline bar) via instance() +
 * create(). It owns the display time and writes it through to the model's
 * shared `gTimeSource` (QDateTime); an *invalid* gTimeSource means "live / now".
 *
 * Time model (P3.14 phase F): a fixed read-marker with a pannable, infinite
 * axis. displayTime() is the time under the marker; the visible window is
 * derived -- [displayTime - f*W, displayTime + (1-f)*W] -- so panning the axis
 * (panPixels/panSeconds) just shifts displayTime, with no bounds. Default
 * W = 32 h, f = 0.25 (previous 8 h + next 24 h, marker a quarter across).
 * Modes: LIVE (tracks the clock; gTimeSource invalid), SCRUB (a panned time),
 * PLAY (animates forward).
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
  // Derived window around displayTime; both edges move as the axis pans.
  Q_PROPERTY(QDateTime windowStart READ windowStart NOTIFY windowChanged)
  Q_PROPERTY(QDateTime windowEnd READ windowEnd NOTIFY windowChanged)
  Q_PROPERTY(double windowSeconds READ windowSeconds NOTIFY windowChanged)   // W
  Q_PROPERTY(double markerFraction READ markerFraction NOTIFY windowChanged) // f
  // Where the live clock falls in the window, 0..1 (outside if off-window).
  Q_PROPERTY(double nowFraction READ nowFraction NOTIFY timeChanged)
  Q_PROPERTY(QString timeLabel READ timeLabel NOTIFY timeChanged)  // "HH:mm"
  Q_PROPERTY(QString dateLabel READ dateLabel NOTIFY timeChanged)  // "ddd dd MMM"

public:
  explicit TimeController(QObject* parent = nullptr);
  ~TimeController() override;

  /** The shared C++ instance -- also what QML's singleton resolves to. */
  static TimeController& instance();
  static TimeController* create(QQmlEngine*, QJSEngine*) {
    TimeController* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  QDateTime displayTime() const { return m_time; }
  bool live() const { return m_live; }
  bool playing() const { return m_playing; }
  double windowSeconds() const { return m_window_secs; }
  double markerFraction() const { return m_marker_fraction; }
  QDateTime windowStart() const {
    return m_time.addSecs(
        -static_cast<qint64>(m_marker_fraction * m_window_secs));
  }
  QDateTime windowEnd() const {
    return m_time.addSecs(
        static_cast<qint64>((1.0 - m_marker_fraction) * m_window_secs));
  }
  double nowFraction() const;
  QString timeLabel() const;
  QString dateLabel() const;

  /** Snap to the real clock and resume live tracking (gTimeSource invalid). */
  Q_INVOKABLE void goLive();
  /** Set an explicit display time (enters scrub). Invalid -> goLive(). */
  Q_INVOKABLE void setDisplayTime(const QDateTime& t);
  /** Pan the axis under the fixed marker by dx pixels over a plot widthPx (drag
   *  right -> earlier time). Infinite; enters scrub. */
  Q_INVOKABLE void panPixels(double dx, double widthPx);
  /** Pan by seconds (positive -> later). Enters scrub. */
  Q_INVOKABLE void panSeconds(double secs);
  /** Set displayTime to the window fraction x in [0,1] (click-to-read). */
  Q_INVOKABLE void setDisplayFraction(double x);
  /** Nudge the display time by +/- minutes (enters scrub). */
  Q_INVOKABLE void stepMinutes(int mins);
  /** Animate forward from the current time. */
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void togglePlay();

Q_SIGNALS:
  void timeChanged();   // displayTime / nowFraction / time+date labels
  void modeChanged();   // live / playing
  void windowChanged(); // window edges / fraction / span

private:
  void applyToGlobal();  // push m_time/m_live into model gTimeSource
  void enterScrub();     // leave live + play, reset the live cadence
  void onTick();         // 1 Hz live-track / 100 ms play advance

  QDateTime m_time;                      // time under the fixed marker
  double m_window_secs = 32.0 * 3600.0;  // 8 h back + 24 h forward
  double m_marker_fraction = 0.25;       // read-marker x fraction
  bool m_live = true;
  bool m_playing = false;
  int m_play_minutes_per_tick = 5;  // per 100 ms play tick
  QTimer* m_timer = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_TIME_CONTROLLER_H_
