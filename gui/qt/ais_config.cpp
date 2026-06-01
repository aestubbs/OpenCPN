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
 * Implement ais_config.h.
 */

#include "ais_config.h"

#include "config_store.h"

namespace ocpn::qtui {

AisConfig& AisConfig::instance() {
  static AisConfig s;
  return s;
}

AisConfig::AisConfig() {
  ConfigStore& c = ConfigStore::instance();
  m_cpa_max_range = c.getDouble("ais/cpaMaxRange", m_cpa_max_range);
  m_cpa_warn = c.getDouble("ais/cpaWarn", m_cpa_warn);
  m_tcpa_warn = c.getDouble("ais/tcpaWarn", m_tcpa_warn);
  m_mark_lost = c.getDouble("ais/markLost", m_mark_lost);
  m_remove_lost = c.getDouble("ais/removeLost", m_remove_lost);
  m_track_retention_days =
      c.getInt("ais/trackRetentionDays", m_track_retention_days);
  m_predictor_min = c.getDouble("ais/predictorMin", m_predictor_min);
  m_sync_predictor = c.getBool("ais/syncPredictor", m_sync_predictor);
  m_tracks_len = c.getDouble("ais/tracksLen", m_tracks_len);
  m_suppress_anchored = c.getDouble("ais/suppressAnchored", m_suppress_anchored);
  m_realtime_pred_min = c.getDouble("ais/realtimePredMin", m_realtime_pred_min);
  m_attenuation = c.getInt("ais/attenuation", m_attenuation);
  m_area_notices = c.getBool("ais/areaNotices", m_area_notices);
  m_real_size = c.getBool("ais/realSize", m_real_size);
  m_show_names = c.getBool("ais/showNames", m_show_names);
  m_handle_wpl = c.getBool("ais/handleWpl", m_handle_wpl);
  m_rollover_class = c.getBool("ais/rolloverClass", m_rollover_class);
  m_rollover_cogsog = c.getBool("ais/rolloverCogSog", m_rollover_cogsog);
  m_rollover_cpatcpa = c.getBool("ais/rolloverCpaTcpa", m_rollover_cpatcpa);
  m_cpa_alert = c.getBool("ais/cpaAlert", m_cpa_alert);
  m_cpa_alert_sound = c.getBool("ais/cpaAlertSound", m_cpa_alert_sound);
  m_suppress_moored = c.getBool("ais/suppressMoored", m_suppress_moored);
  m_ack_timeout = c.getDouble("ais/ackTimeout", m_ack_timeout);
}

// guard, store, persist, notify.
#define OCPN_AIS_SET(member, value, key, putter) \
  if ((member) == (value)) return;               \
  (member) = (value);                            \
  ConfigStore::instance().putter(key, value);    \
  Q_EMIT changed();

void AisConfig::setCpaMaxRangeNm(double v) {
  OCPN_AIS_SET(m_cpa_max_range, v, "ais/cpaMaxRange", setDouble)
}
void AisConfig::setCpaWarnNm(double v) {
  OCPN_AIS_SET(m_cpa_warn, v, "ais/cpaWarn", setDouble)
}
void AisConfig::setTcpaWarnMin(double v) {
  OCPN_AIS_SET(m_tcpa_warn, v, "ais/tcpaWarn", setDouble)
}
void AisConfig::setMarkLostMin(double v) {
  OCPN_AIS_SET(m_mark_lost, v, "ais/markLost", setDouble)
}
void AisConfig::setRemoveLostMin(double v) {
  OCPN_AIS_SET(m_remove_lost, v, "ais/removeLost", setDouble)
}
void AisConfig::setTrackRetentionDays(int v) {
  if (v < 0) v = 0;
  OCPN_AIS_SET(m_track_retention_days, v, "ais/trackRetentionDays", setInt)
}
void AisConfig::setPredictorMinutes(double v) {
  OCPN_AIS_SET(m_predictor_min, v, "ais/predictorMin", setDouble)
}
void AisConfig::setSyncPredictorWithOwnShip(bool v) {
  OCPN_AIS_SET(m_sync_predictor, v, "ais/syncPredictor", setBool)
}
void AisConfig::setTracksLengthMin(double v) {
  OCPN_AIS_SET(m_tracks_len, v, "ais/tracksLen", setDouble)
}
void AisConfig::setSuppressAnchoredSpeedMax(double v) {
  OCPN_AIS_SET(m_suppress_anchored, v, "ais/suppressAnchored", setDouble)
}
void AisConfig::setRealtimePredSpeedMin(double v) {
  OCPN_AIS_SET(m_realtime_pred_min, v, "ais/realtimePredMin", setDouble)
}
void AisConfig::setAttenuationThreshold(int v) {
  OCPN_AIS_SET(m_attenuation, v, "ais/attenuation", setInt)
}
void AisConfig::setShowAreaNotices(bool v) {
  OCPN_AIS_SET(m_area_notices, v, "ais/areaNotices", setBool)
}
void AisConfig::setShowRealSize(bool v) {
  OCPN_AIS_SET(m_real_size, v, "ais/realSize", setBool)
}
void AisConfig::setShowNames(bool v) {
  OCPN_AIS_SET(m_show_names, v, "ais/showNames", setBool)
}
void AisConfig::setHandleWplMessages(bool v) {
  OCPN_AIS_SET(m_handle_wpl, v, "ais/handleWpl", setBool)
}
void AisConfig::setRolloverClass(bool v) {
  OCPN_AIS_SET(m_rollover_class, v, "ais/rolloverClass", setBool)
}
void AisConfig::setRolloverCogSog(bool v) {
  OCPN_AIS_SET(m_rollover_cogsog, v, "ais/rolloverCogSog", setBool)
}
void AisConfig::setRolloverCpaTcpa(bool v) {
  OCPN_AIS_SET(m_rollover_cpatcpa, v, "ais/rolloverCpaTcpa", setBool)
}
void AisConfig::setCpaAlert(bool v) {
  OCPN_AIS_SET(m_cpa_alert, v, "ais/cpaAlert", setBool)
}
void AisConfig::setCpaAlertSound(bool v) {
  OCPN_AIS_SET(m_cpa_alert_sound, v, "ais/cpaAlertSound", setBool)
}
void AisConfig::setSuppressMooredAlerts(bool v) {
  OCPN_AIS_SET(m_suppress_moored, v, "ais/suppressMoored", setBool)
}
void AisConfig::setAckTimeoutMin(double v) {
  OCPN_AIS_SET(m_ack_timeout, v, "ais/ackTimeout", setDouble)
}

#undef OCPN_AIS_SET

}  // namespace ocpn::qtui
