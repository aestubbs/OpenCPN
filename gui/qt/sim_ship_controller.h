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
 * SimShipController -- a "test ship": a synthetic GPS source for simulating a
 * voyage so route activation / following can be exercised without a live feed
 * (P3.16). It is placed with the mouse (right-click -> Place test ship here)
 * and steered with the cursor keys: Left/Right alter course, Up/Down alter
 * speed (0..40 kn), Space starts/stops. Once running it integrates its
 * position on a 4 Hz tick and writes the own-ship globals
 * (gLat/gLon/gCog/gHdt/gSog/bGPSValid) -- exactly the globals a real GPS feed
 * sets -- so the ModelNavDataProvider mirror, the own-ship overlay, the HUD,
 * and Routeman::UpdateProgress() all see it as the boat with no extra wiring.
 *
 * Active only in live mode (the test ship IS the live position source); enabling
 * it is the caller's cue to switch off demo mode and start model polling.
 */

#ifndef OCPN_QT_SIM_SHIP_CONTROLLER_H_
#define OCPN_QT_SIM_SHIP_CONTROLLER_H_

#include <QElapsedTimer>
#include <QObject>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class SimShipController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(double speed READ speed NOTIFY changed)   // knots, 0..kMaxSpeed
  Q_PROPERTY(double course READ course NOTIFY changed)  // deg true, 0..360
  Q_PROPERTY(bool placed READ placed NOTIFY changed)

public:
  static constexpr double kMaxSpeed = 40.0;  // knots

  explicit SimShipController(QObject* parent = nullptr);
  ~SimShipController() override;

  bool active() const { return m_active; }
  bool running() const { return m_running; }
  bool placed() const { return m_placed; }
  double speed() const { return m_speed; }
  double course() const { return m_course; }

  /** Enter/leave test-ship mode. Entering starts the 4 Hz tick and begins
   *  writing the own-ship globals; leaving stops the tick (the last fix
   *  remains). */
  Q_INVOKABLE void setActive(bool on);

  /** Drop the test ship at a position (degrees). Implies active + placed. */
  Q_INVOKABLE void place(double lat, double lon);

  /** Absolute / relative course (deg true) and speed (knots, clamped). */
  Q_INVOKABLE void setCourse(double deg);
  Q_INVOKABLE void steer(double delta_deg);
  Q_INVOKABLE void setSpeed(double knots);
  Q_INVOKABLE void throttle(double delta_knots);

  /** Start/stop making way (course/speed are retained while stopped). */
  Q_INVOKABLE void setRunning(bool on);
  Q_INVOKABLE void toggleRun() { setRunning(!m_running); }

Q_SIGNALS:
  void changed();
  /** Emitted each tick after the globals are written, so the canvas can drive
   *  the route follower + repaint in lock-step with the simulated fix. */
  void ticked();

private:
  void tick();
  void writeGlobals();  // publish state to gLat/gLon/gCog/gHdt/gSog/bGPSValid
  // Advance (lat, lon) by `knots` along `course` (deg true) for `seconds`.
  static void advance(double& lat, double& lon, double course, double knots,
                      double seconds);

  bool m_active = false;
  bool m_running = false;
  bool m_placed = false;
  double m_lat = 0.0;
  double m_lon = 0.0;
  double m_course = 0.0;  // deg true
  double m_speed = 6.0;   // knots (a sensible default cruising speed)

  QTimer* m_timer = nullptr;
  QElapsedTimer m_clock;
  qint64 m_last_ms = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SIM_SHIP_CONTROLLER_H_
