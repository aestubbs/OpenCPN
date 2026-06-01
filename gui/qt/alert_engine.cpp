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
 * Implement alert_engine.h.
 */

#include "alert_engine.h"

#include <QDateTime>
#include <QTime>
#include <QTimer>

#include "ais_config.h"
#include "config_store.h"
#include "model/georef.h"
#include "ui_config.h"

namespace ocpn::qtui {

AlertEngine::AlertEngine(QObject* parent) : QObject(parent) {
  // Restore a watch dropped before a restart.
  ConfigStore& cs = ConfigStore::instance();
  m_anchor_set = cs.getBool("anchor/set", false);
  m_anchor_lat = cs.getDouble("anchor/lat", 0.0);
  m_anchor_lon = cs.getDouble("anchor/lon", 0.0);
  m_anchor_radius_m = cs.getDouble("anchor/radiusM", 50.0);

  // Ship's bells: a single-shot timer re-armed to each half-hour boundary
  // (re-arming avoids interval drift); it checks UIConfig::playShipsBells when
  // it fires, so toggling the setting needs no start/stop.
  m_bell_timer = new QTimer(this);
  m_bell_timer->setSingleShot(true);
  connect(m_bell_timer, &QTimer::timeout, this, &AlertEngine::onShipsBell);
  scheduleNextBell();
}

void AlertEngine::persistAnchor() const {
  ConfigStore& cs = ConfigStore::instance();
  cs.setBool("anchor/set", m_anchor_set);
  cs.setDouble("anchor/lat", m_anchor_lat);
  cs.setDouble("anchor/lon", m_anchor_lon);
  cs.setDouble("anchor/radiusM", m_anchor_radius_m);
}

QString AlertEngine::formatAlert(const AisTarget& t) const {
  const QString who =
      t.name.isEmpty() ? QStringLiteral("MMSI %1").arg(t.mmsi) : t.name;
  QString s = QStringLiteral("AIS danger: %1").arg(who);
  if (t.cpaValid) {
    s += QStringLiteral(" — CPA %1 NM").arg(t.cpaNm, 0, 'f', 2);
    if (t.tcpaMin >= 0.0)
      s += QStringLiteral(" in %1 min").arg(t.tcpaMin, 0, 'f', 0);
  }
  return s;
}

void AlertEngine::evaluateAis(const QList<AisTarget>& targets) {
  const AisConfig& ais = AisConfig::instance();
  const UIConfig& ui = UIConfig::instance();
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 ack_ms = static_cast<qint64>(ais.ackTimeoutMin() * 60000.0);

  const auto who = [](const AisTarget& t) {
    return t.name.isEmpty() ? QStringLiteral("MMSI %1").arg(t.mmsi) : t.name;
  };
  // "Alertable" = a SART/DSC distress beacon (always), or a CPA-dangerous
  // target when CPA alerting is on (ais_cpa already gated range/CPA/TCPA/moored).
  const auto alertableOf = [&](const AisTarget& t) {
    return t.isSart || t.isDsc || (ais.cpaAlert() && t.dangerous);
  };

  QSet<int> alertable;
  alertable.reserve(targets.size());
  for (const AisTarget& t : targets)
    if (alertableOf(t)) alertable.insert(t.mmsi);

  // Drop ack / sounded state for targets that are no longer alertable, so a
  // fresh incursion alerts again.
  for (auto it = m_acked.begin(); it != m_acked.end();) {
    if (alertable.contains(it.key()))
      ++it;
    else
      it = m_acked.erase(it);
  }
  m_sounded.intersect(alertable);

  // Walk the alertable targets: collect the shown set (minus acked hold-offs),
  // pick the banner text (distress outranks CPA; CPA picks the lowest TCPA),
  // and sound each newly-raised target with its own configured file.
  QSet<int> active;
  QString distress_text, cpa_text;
  double best_tcpa = 1e18;
  for (const AisTarget& t : targets) {
    const bool distress = t.isSart || t.isDsc;
    const bool cpa = ais.cpaAlert() && t.dangerous;
    if (!distress && !cpa) continue;
    const auto ait = m_acked.constFind(t.mmsi);
    if (ait != m_acked.constEnd() && (now - ait.value()) < ack_ms)
      continue;  // acknowledged, still in hold-off
    active.insert(t.mmsi);

    if (distress) {
      if (distress_text.isEmpty())
        distress_text = (t.isSart ? QStringLiteral("SART distress: %1")
                                  : QStringLiteral("DSC distress: %1"))
                            .arg(who(t));
    } else {
      const double tc = (t.tcpaMin >= 0.0) ? t.tcpaMin : 1e17;
      if (tc < best_tcpa || cpa_text.isEmpty()) {
        best_tcpa = tc;
        cpa_text = formatAlert(t);
      }
    }

    if (!m_sounded.contains(t.mmsi)) {
      QString f;  // the right sound for this alert kind, if enabled
      if (t.isSart && ui.sartAlertSound())
        f = ui.sartSoundFile();
      else if (t.isDsc && ui.dscAlertSound())
        f = ui.dscSoundFile();
      else if (!distress && ais.cpaAlertSound())
        f = ui.aisSoundFile();
      if (!f.isEmpty()) Q_EMIT soundRequested(f);
    }
  }

  m_active_mmsis = active;
  m_sounded.unite(active);  // don't re-sound the same targets every tick

  const QString text = distress_text.isEmpty() ? cpa_text : distress_text;
  const bool active_now = !active.isEmpty();
  if (active_now != m_ais_active || text != m_ais_text) {
    m_ais_active = active_now;
    m_ais_text = text;
    Q_EMIT changed();
  }
}

void AlertEngine::evaluateAnchor(const OwnShipState& own) {
  if (own.valid) m_last_own = own;
  const bool was_active = m_anchor_active;
  const bool was_breach = m_anchor_breached;

  if (!m_anchor_set || !own.valid) {
    m_anchor_breached = false;
    m_anchor_active = false;
    if (was_active || was_breach) Q_EMIT changed();
    return;
  }

  // Great-circle distance from the anchor to the current fix.
  const double dist_m =
      DistGreatCircle(m_anchor_lat, m_anchor_lon, own.lat, own.lon) * 1852.0;
  m_anchor_breached = dist_m > m_anchor_radius_m;

  if (!m_anchor_breached) {
    // Back inside the watch circle: re-arm the alarm.
    m_anchor_acked = false;
    m_anchor_sounded = false;
    m_anchor_active = false;
  } else {
    m_anchor_active = !m_anchor_acked;  // acked -> breached but silent
    if (m_anchor_active) {
      m_anchor_text =
          QStringLiteral("Anchor drag: %1 m from anchor (watch %2 m)")
              .arg(dist_m, 0, 'f', 0)
              .arg(m_anchor_radius_m, 0, 'f', 0);
      if (!m_anchor_sounded && UIConfig::instance().anchorAlarmSound()) {
        const QString f = UIConfig::instance().anchorSoundFile();
        if (!f.isEmpty()) Q_EMIT soundRequested(f);
        m_anchor_sounded = true;
      }
    }
  }

  if (m_anchor_active != was_active || m_anchor_breached != was_breach)
    Q_EMIT changed();
}

void AlertEngine::acknowledge() {
  bool any = false;
  // AIS: hold off the currently-shown targets for the ack timeout.
  if (!m_active_mmsis.isEmpty() || m_ais_active) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int mmsi : m_active_mmsis) m_acked.insert(mmsi, now);
    m_sounded.subtract(m_active_mmsis);  // re-sound allowed after the hold-off
    m_active_mmsis.clear();
    if (m_ais_active || !m_ais_text.isEmpty()) {
      m_ais_active = false;
      m_ais_text.clear();
      any = true;
    }
  }
  // Anchor: silence until the boat returns inside the watch circle.
  if (m_anchor_active) {
    m_anchor_acked = true;
    m_anchor_active = false;
    any = true;
  }
  if (any) Q_EMIT changed();
}

void AlertEngine::dropAnchor() {
  if (!m_last_own.valid) return;  // no fix yet -- nothing to pin to
  m_anchor_lat = m_last_own.lat;
  m_anchor_lon = m_last_own.lon;
  m_anchor_set = true;
  m_anchor_acked = false;
  m_anchor_sounded = false;
  m_anchor_breached = false;
  m_anchor_active = false;
  persistAnchor();
  Q_EMIT anchorChanged();
  Q_EMIT changed();
}

void AlertEngine::raiseAnchor() {
  if (!m_anchor_set && !m_anchor_active && !m_anchor_breached) return;
  m_anchor_set = false;
  m_anchor_breached = false;
  m_anchor_active = false;
  m_anchor_acked = false;
  m_anchor_sounded = false;
  persistAnchor();
  Q_EMIT anchorChanged();
  Q_EMIT changed();
}

void AlertEngine::setAnchorRadiusM(double metres) {
  if (metres < 5.0) metres = 5.0;
  if (qFuzzyCompare(metres, m_anchor_radius_m)) return;
  m_anchor_radius_m = metres;
  persistAnchor();
  Q_EMIT anchorChanged();  // the watch circle resizes; breach re-evals next tick
}

void AlertEngine::scheduleNextBell() {
  // Milliseconds to the next :00 / :30 wall-clock boundary, +200 ms so the
  // timer fires just after it (never a hair early, which would mis-count).
  const QTime now = QTime::currentTime();
  const int sec_into_half = (now.minute() % 30) * 60 + now.second();
  int ms = (30 * 60 - sec_into_half) * 1000 - now.msec() + 200;
  if (ms < 1000) ms += 30 * 60 * 1000;  // guard against a double fire
  m_bell_timer->start(ms);
}

void AlertEngine::onShipsBell() {
  if (UIConfig::instance().playShipsBells()) {
    // Number of bells = half-hours into the 4-hour watch (1..8, 8 on the hour
    // that closes a watch). +2 s so a fire a touch before the boundary still
    // reads the correct half-hour.
    const QTime t = QTime::currentTime().addSecs(2);
    int bells = (t.hour() * 2 + (t.minute() >= 30 ? 1 : 0)) % 8;
    if (bells == 0) bells = 8;
    ringBells(bells);
  }
  scheduleNextBell();
}

void AlertEngine::ringBells(int count) {
  if (count <= 0) return;
  // Strike in pairs (the "ding-ding" 2bells sample) plus a trailing single,
  // spaced so each sample finishes before the next (the SoundPlayer plays one
  // file at a time). Two strikes per pair, one for the odd bell.
  const QString two = QStringLiteral(OCPN_QT_SOUNDS_DIR "/2bells.wav");
  const QString one = QStringLiteral(OCPN_QT_SOUNDS_DIR "/1bells.wav");
  const int pairs = count / 2;
  const bool single = (count % 2) != 0;
  int slot = 0;
  for (int i = 0; i < pairs; ++i, ++slot) {
    QTimer::singleShot(slot * 1600, this,
                       [this, two]() { Q_EMIT soundRequested(two); });
  }
  if (single) {
    QTimer::singleShot(slot * 1600, this,
                       [this, one]() { Q_EMIT soundRequested(one); });
  }
}

}  // namespace ocpn::qtui
