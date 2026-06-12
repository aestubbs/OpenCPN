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
 * AisConfig -- AIS-target preferences, mirroring the wx Options > Ships > AIS
 * Targets sub-panel (CPA/TCPA, lost-target timeouts, display filtering,
 * rollover content, alerts). A process-wide singleton, persisted via
 * ConfigStore, exposed to QML as the context property "ais".
 *
 * All settings are persisted today; the Qt build has no CPA/TCPA computation
 * or alert engine yet, so they are stored against the day that processing
 * lands (see the P3.6 inventory in docs/QT_MIGRATION_TASKS.md). The AIS layer
 * already draws targets + COG predictors; the predictor length here is wired
 * once the layer reads it.
 */

#ifndef OCPN_QT_AIS_CONFIG_H_
#define OCPN_QT_AIS_CONFIG_H_

#include <QObject>
#include <QQmlEngine>

namespace ocpn::qtui {

class AisConfig : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  // --- CPA / TCPA ----------------------------------------------------------
  Q_PROPERTY(double cpaMaxRangeNm READ cpaMaxRangeNm WRITE setCpaMaxRangeNm
                 NOTIFY changed)
  Q_PROPERTY(double cpaWarnNm READ cpaWarnNm WRITE setCpaWarnNm NOTIFY changed)
  Q_PROPERTY(double tcpaWarnMin READ tcpaWarnMin WRITE setTcpaWarnMin NOTIFY
                 changed)

  // --- Lost targets --------------------------------------------------------
  Q_PROPERTY(double markLostMin READ markLostMin WRITE setMarkLostMin NOTIFY
                 changed)
  Q_PROPERTY(double removeLostMin READ removeLostMin WRITE setRemoveLostMin
                 NOTIFY changed)

  // --- Trail history -------------------------------------------------------
  // Days of recorded AIS position history to keep in SQLite (global record;
  // selected vessels' trails are drawn from it). 0 = keep nothing.
  Q_PROPERTY(int trackRetentionDays READ trackRetentionDays WRITE
                 setTrackRetentionDays NOTIFY changed)

  // --- Display -------------------------------------------------------------
  Q_PROPERTY(double predictorMinutes READ predictorMinutes WRITE
                 setPredictorMinutes NOTIFY changed)
  Q_PROPERTY(bool syncPredictorWithOwnShip READ syncPredictorWithOwnShip WRITE
                 setSyncPredictorWithOwnShip NOTIFY changed)
  Q_PROPERTY(double tracksLengthMin READ tracksLengthMin WRITE
                 setTracksLengthMin NOTIFY changed)
  Q_PROPERTY(double suppressAnchoredSpeedMax READ suppressAnchoredSpeedMax
                 WRITE setSuppressAnchoredSpeedMax NOTIFY changed)
  Q_PROPERTY(double realtimePredSpeedMin READ realtimePredSpeedMin WRITE
                 setRealtimePredSpeedMin NOTIFY changed)
  Q_PROPERTY(int attenuationThreshold READ attenuationThreshold WRITE
                 setAttenuationThreshold NOTIFY changed)
  Q_PROPERTY(bool showAreaNotices READ showAreaNotices WRITE setShowAreaNotices
                 NOTIFY changed)
  Q_PROPERTY(bool showRealSize READ showRealSize WRITE setShowRealSize NOTIFY
                 changed)
  Q_PROPERTY(bool showNames READ showNames WRITE setShowNames NOTIFY changed)
  Q_PROPERTY(bool handleWplMessages READ handleWplMessages WRITE
                 setHandleWplMessages NOTIFY changed)

  // --- Rollover info block -------------------------------------------------
  Q_PROPERTY(bool rolloverClass READ rolloverClass WRITE setRolloverClass
                 NOTIFY changed)
  Q_PROPERTY(bool rolloverCogSog READ rolloverCogSog WRITE setRolloverCogSog
                 NOTIFY changed)
  Q_PROPERTY(bool rolloverCpaTcpa READ rolloverCpaTcpa WRITE setRolloverCpaTcpa
                 NOTIFY changed)

  // --- Alerts --------------------------------------------------------------
  Q_PROPERTY(bool cpaAlert READ cpaAlert WRITE setCpaAlert NOTIFY changed)
  Q_PROPERTY(bool cpaAlertSound READ cpaAlertSound WRITE setCpaAlertSound
                 NOTIFY changed)
  Q_PROPERTY(bool suppressMooredAlerts READ suppressMooredAlerts WRITE
                 setSuppressMooredAlerts NOTIFY changed)
  Q_PROPERTY(double ackTimeoutMin READ ackTimeoutMin WRITE setAckTimeoutMin
                 NOTIFY changed)

public:
  static AisConfig& instance();
  static AisConfig* create(QQmlEngine*, QJSEngine*) {
    AisConfig* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  double cpaMaxRangeNm() const { return m_cpa_max_range; }
  void setCpaMaxRangeNm(double v);
  double cpaWarnNm() const { return m_cpa_warn; }
  void setCpaWarnNm(double v);
  double tcpaWarnMin() const { return m_tcpa_warn; }
  void setTcpaWarnMin(double v);

  double markLostMin() const { return m_mark_lost; }
  void setMarkLostMin(double v);
  double removeLostMin() const { return m_remove_lost; }
  void setRemoveLostMin(double v);
  int trackRetentionDays() const { return m_track_retention_days; }
  void setTrackRetentionDays(int v);

  double predictorMinutes() const { return m_predictor_min; }
  void setPredictorMinutes(double v);
  bool syncPredictorWithOwnShip() const { return m_sync_predictor; }
  void setSyncPredictorWithOwnShip(bool v);
  double tracksLengthMin() const { return m_tracks_len; }
  void setTracksLengthMin(double v);
  double suppressAnchoredSpeedMax() const { return m_suppress_anchored; }
  void setSuppressAnchoredSpeedMax(double v);
  double realtimePredSpeedMin() const { return m_realtime_pred_min; }
  void setRealtimePredSpeedMin(double v);
  int attenuationThreshold() const { return m_attenuation; }
  void setAttenuationThreshold(int v);
  bool showAreaNotices() const { return m_area_notices; }
  void setShowAreaNotices(bool v);
  bool showRealSize() const { return m_real_size; }
  void setShowRealSize(bool v);
  bool showNames() const { return m_show_names; }
  void setShowNames(bool v);
  bool handleWplMessages() const { return m_handle_wpl; }
  void setHandleWplMessages(bool v);

  bool rolloverClass() const { return m_rollover_class; }
  void setRolloverClass(bool v);
  bool rolloverCogSog() const { return m_rollover_cogsog; }
  void setRolloverCogSog(bool v);
  bool rolloverCpaTcpa() const { return m_rollover_cpatcpa; }
  void setRolloverCpaTcpa(bool v);

  bool cpaAlert() const { return m_cpa_alert; }
  void setCpaAlert(bool v);
  bool cpaAlertSound() const { return m_cpa_alert_sound; }
  void setCpaAlertSound(bool v);
  bool suppressMooredAlerts() const { return m_suppress_moored; }
  void setSuppressMooredAlerts(bool v);
  double ackTimeoutMin() const { return m_ack_timeout; }
  void setAckTimeoutMin(double v);

Q_SIGNALS:
  void changed();

private:
  // Constructor is PRIVATE so the QML engine cannot default-
  // construct a second instance: Qt picks the Constructor mode
  // over the create() factory whenever the type is default-
  // constructible (qqmlprivate.h singletonConstructionMode),
  // which split every singleton into a C++ brain and a QML
  // brain. Private ctor => Factory mode => create() => the
  // ONE shared instance().
  AisConfig();  // loads from the config store


  double m_cpa_max_range = 10.0;
  double m_cpa_warn = 0.5;
  double m_tcpa_warn = 30.0;
  double m_mark_lost = 8.0;
  double m_remove_lost = 10.0;
  int m_track_retention_days = 7;

  double m_predictor_min = 6.0;
  bool m_sync_predictor = true;
  double m_tracks_len = 0.0;
  double m_suppress_anchored = 0.0;
  double m_realtime_pred_min = 0.0;
  int m_attenuation = 0;
  bool m_area_notices = false;
  bool m_real_size = false;
  bool m_show_names = true;
  bool m_handle_wpl = false;

  bool m_rollover_class = true;
  bool m_rollover_cogsog = true;
  bool m_rollover_cpatcpa = true;

  bool m_cpa_alert = true;
  bool m_cpa_alert_sound = false;
  bool m_suppress_moored = true;
  double m_ack_timeout = 10.0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_AIS_CONFIG_H_
