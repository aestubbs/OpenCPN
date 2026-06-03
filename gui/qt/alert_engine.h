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
 * AlertEngine -- decides WHEN to raise a navigation alert and surfaces it to
 * the QML shell (P3.15). The first wired source is the AIS CPA/TCPA danger
 * already computed by ais_cpa (AisTarget::dangerous); anchor-watch, SART/DSC
 * and ship's bells are follow-ups that feed the same engine.
 *
 * Owned by ChartCanvas (which holds the NavDataProvider). On each dynamic
 * data tick ChartCanvas calls evaluateAis() with the enriched target list and
 * evaluateAnchor() with the own-ship fix; the engine maintains per-MMSI alert
 * state (active / acknowledged / already-sounded) plus the anchor-watch state,
 * exposes the current banner state to QML (alertActive / alertText), and emits
 * soundRequested() so the QML SoundPlayer singleton plays the user-chosen file.
 * Acknowledge() silences the current alert(s); an AIS alert is held off for
 * AisConfig::ackTimeoutMin, an anchor breach until the boat returns inside the
 * watch circle.
 *
 * Anchor watch: dropAnchor() pins the watch circle at the last own-ship fix
 * (persisted via ConfigStore so it survives a restart); a fix outside
 * anchorRadiusM raises the (red) anchor alarm. The AnchorWatchLayer draws the
 * circle from this same state.
 */

#ifndef OCPN_QT_ALERT_ENGINE_H_
#define OCPN_QT_ALERT_ENGINE_H_

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include "nav_data.h"

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class AlertEngine : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool alertActive READ alertActive NOTIFY changed)
  Q_PROPERTY(QString alertText READ alertText NOTIFY changed)
  // Anchor watch (for the AnchorWatchLayer + on-chart control).
  Q_PROPERTY(bool anchorSet READ anchorSet NOTIFY anchorChanged)
  Q_PROPERTY(double anchorLat READ anchorLat NOTIFY anchorChanged)
  Q_PROPERTY(double anchorLon READ anchorLon NOTIFY anchorChanged)
  Q_PROPERTY(double anchorRadiusM READ anchorRadiusM NOTIFY anchorChanged)
  Q_PROPERTY(bool anchorBreach READ anchorBreach NOTIFY changed)

public:
  explicit AlertEngine(QObject* parent = nullptr);

  // Combined banner state -- an anchor breach (safety) outranks a route event,
  // which outranks an AIS alert.
  bool alertActive() const {
    return m_anchor_active || m_route_active || m_ais_active;
  }
  QString alertText() const {
    if (m_anchor_active) return m_anchor_text;
    if (m_route_active) return m_route_text;
    return m_ais_text;
  }

  bool anchorSet() const { return m_anchor_set; }
  double anchorLat() const { return m_anchor_lat; }
  double anchorLon() const { return m_anchor_lon; }
  double anchorRadiusM() const { return m_anchor_radius_m; }
  bool anchorBreach() const { return m_anchor_active; }

  /** Re-evaluate the AIS alert state against the current (CPA-enriched) target
   *  list. Call on each dynamic data tick. */
  void evaluateAis(const QList<AisTarget>& targets);

  /** Re-evaluate the anchor watch against the latest own-ship fix (also
   *  remembered for dropAnchor()). Call on each dynamic data tick. */
  void evaluateAnchor(const OwnShipState& own);

  /** Raise a route-following event in the banner (P3.16): a waypoint arrival
   *  or route end. Holds until acknowledge(); rings the ship's bell unless
   *  `sound` is false. */
  void noteRouteEvent(const QString& text, bool sound = true);

  /** Silence the current alert(s): an AIS alert for AisConfig::ackTimeoutMin,
   *  an anchor breach until the boat returns inside the watch circle. */
  Q_INVOKABLE void acknowledge();

  // Anchor-watch controls (persisted via ConfigStore).
  Q_INVOKABLE void dropAnchor();          // pin at the last own-ship fix
  Q_INVOKABLE void raiseAnchor();         // clear the watch
  Q_INVOKABLE void setAnchorRadiusM(double metres);

Q_SIGNALS:
  void changed();
  void anchorChanged();
  /** Request the QML SoundPlayer play this audio file (absolute path). */
  void soundRequested(const QString& file);

private:
  QString formatAlert(const AisTarget& t) const;
  void persistAnchor() const;
  // Ship's bells (P3.15): struck on each half-hour of the 4-hour watch when
  // UIConfig::playShipsBells is on, using the bundled 1bells/2bells sounds.
  void scheduleNextBell();
  void onShipsBell();
  void ringBells(int count);

  // AIS CPA/TCPA alert.
  bool m_ais_active = false;
  QString m_ais_text;
  QSet<int> m_active_mmsis;       // targets in the current (shown) alert
  QHash<int, qint64> m_acked;     // mmsi -> acknowledge time (ms since epoch)
  QSet<int> m_sounded;            // mmsi already sounded this incursion

  // Route-following event (waypoint arrival / route end). Sticky until acked.
  bool m_route_active = false;
  QString m_route_text;

  // Anchor watch.
  bool m_anchor_set = false;
  bool m_anchor_breached = false; // geometric: fix is outside the circle
  bool m_anchor_active = false;   // breached and not acknowledged (banner)
  bool m_anchor_acked = false;    // silenced until back inside the circle
  bool m_anchor_sounded = false;  // alarm played for this breach
  QString m_anchor_text;
  double m_anchor_lat = 0.0;
  double m_anchor_lon = 0.0;
  double m_anchor_radius_m = 50.0;
  OwnShipState m_last_own;        // latest fix, for dropAnchor()

  // Ship's bells.
  QTimer* m_bell_timer = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_ALERT_ENGINE_H_
