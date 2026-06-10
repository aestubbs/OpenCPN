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
 * Implement ui_config.h.
 */

#include "ui_config.h"

#include "config_store.h"

namespace ocpn::qtui {

UIConfig& UIConfig::instance() {
  static UIConfig s;
  return s;
}

UIConfig::UIConfig() {
  ConfigStore& c = ConfigStore::instance();
  m_language = c.getInt("ui/language", m_language);
  m_status_bar = c.getBool("ui/showStatusBar", m_status_bar);
  m_chart_bar = c.getBool("ui/showChartBar", m_chart_bar);
  m_zoom_buttons = c.getBool("ui/showZoomButtons", m_zoom_buttons);
  m_auto_hide = c.getBool("ui/autoHideToolbar", m_auto_hide);
  m_auto_hide_timeout = c.getInt("ui/autoHideTimeout", m_auto_hide_timeout);
  m_toolbar_transparency =
      c.getDouble("ui/toolbarTransparency", m_toolbar_transparency);
  m_touch = c.getBool("ui/touchInterface", m_touch);
  m_ships_bells = c.getBool("ui/playShipsBells", m_ships_bells);
  m_inland_ecdis = c.getBool("ui/inlandEcdis", m_inland_ecdis);
  m_split_view = c.getBool("ui/splitView", m_split_view);
  {
    const QString hs = c.getString("ui/hudStats");
    if (!hs.isEmpty()) m_hud_stats = hs.split(',', Qt::SkipEmptyParts);
  }
  m_hud_stats_x = c.getDouble("ui/hudStatsX", m_hud_stats_x);
  m_hud_stats_y = c.getDouble("ui/hudStatsY", m_hud_stats_y);
  m_split_fraction = c.getDouble("ui/splitFraction", m_split_fraction);
  m_options_x = c.getInt("ui/optionsX", m_options_x);
  m_options_y = c.getInt("ui/optionsY", m_options_y);
  m_gui_scale = c.getInt("ui/guiScaleFactor", m_gui_scale);
  m_chart_scale = c.getInt("ui/chartObjectScaleFactor", m_chart_scale);
  m_ship_scale = c.getInt("ui/shipScaleFactor", m_ship_scale);
  m_enc_text_scale = c.getInt("ui/encTextScaleFactor", m_enc_text_scale);
  m_enc_sounding_scale =
      c.getInt("ui/encSoundingScaleFactor", m_enc_sounding_scale);

  m_anchor_sound = c.getBool("sound/anchorEnable", m_anchor_sound);
  m_anchor_file = c.getString("sound/anchorFile", m_anchor_file);
  m_ais_sound = c.getBool("sound/aisEnable", m_ais_sound);
  m_ais_file = c.getString("sound/aisFile", m_ais_file);
  m_sart_sound = c.getBool("sound/sartEnable", m_sart_sound);
  m_sart_file = c.getString("sound/sartFile", m_sart_file);
  m_dsc_sound = c.getBool("sound/dscEnable", m_dsc_sound);
  m_dsc_file = c.getString("sound/dscFile", m_dsc_file);
}

// guard, store, persist, notify.
#define OCPN_UI_SET(member, value, key, putter) \
  if ((member) == (value)) return;              \
  (member) = (value);                           \
  ConfigStore::instance().putter(key, value);   \
  Q_EMIT changed();

void UIConfig::setLanguage(int v) {
  OCPN_UI_SET(m_language, v, "ui/language", setInt)
}
void UIConfig::setShowStatusBar(bool v) {
  OCPN_UI_SET(m_status_bar, v, "ui/showStatusBar", setBool)
}
void UIConfig::setShowChartBar(bool v) {
  OCPN_UI_SET(m_chart_bar, v, "ui/showChartBar", setBool)
}
void UIConfig::setShowZoomButtons(bool v) {
  OCPN_UI_SET(m_zoom_buttons, v, "ui/showZoomButtons", setBool)
}
void UIConfig::setAutoHideToolbar(bool v) {
  OCPN_UI_SET(m_auto_hide, v, "ui/autoHideToolbar", setBool)
}
void UIConfig::setAutoHideTimeout(int v) {
  OCPN_UI_SET(m_auto_hide_timeout, v, "ui/autoHideTimeout", setInt)
}
void UIConfig::setToolbarTransparency(double v) {
  OCPN_UI_SET(m_toolbar_transparency, v, "ui/toolbarTransparency", setDouble)
}
void UIConfig::setTouchInterface(bool v) {
  OCPN_UI_SET(m_touch, v, "ui/touchInterface", setBool)
}
void UIConfig::setPlayShipsBells(bool v) {
  OCPN_UI_SET(m_ships_bells, v, "ui/playShipsBells", setBool)
}
void UIConfig::setInlandEcdis(bool v) {
  OCPN_UI_SET(m_inland_ecdis, v, "ui/inlandEcdis", setBool)
}
void UIConfig::setHudStats(const QStringList& v) {
  if (m_hud_stats == v) return;
  m_hud_stats = v;
  ConfigStore::instance().setString("ui/hudStats", v.join(','));
  Q_EMIT changed();
}
void UIConfig::setHudStatsX(double v) {
  OCPN_UI_SET(m_hud_stats_x, v, "ui/hudStatsX", setDouble)
}
void UIConfig::setHudStatsY(double v) {
  OCPN_UI_SET(m_hud_stats_y, v, "ui/hudStatsY", setDouble)
}
void UIConfig::setSplitView(bool v) {
  OCPN_UI_SET(m_split_view, v, "ui/splitView", setBool)
}
void UIConfig::setSplitFraction(double v) {
  v = qBound(0.2, v, 0.8);
  OCPN_UI_SET(m_split_fraction, v, "ui/splitFraction", setDouble)
}
void UIConfig::setOptionsX(int v) {
  OCPN_UI_SET(m_options_x, v, "ui/optionsX", setInt)
}
void UIConfig::setOptionsY(int v) {
  OCPN_UI_SET(m_options_y, v, "ui/optionsY", setInt)
}
void UIConfig::setGuiScaleFactor(int v) {
  OCPN_UI_SET(m_gui_scale, v, "ui/guiScaleFactor", setInt)
}
void UIConfig::setChartObjectScaleFactor(int v) {
  OCPN_UI_SET(m_chart_scale, v, "ui/chartObjectScaleFactor", setInt)
}
void UIConfig::setShipScaleFactor(int v) {
  OCPN_UI_SET(m_ship_scale, v, "ui/shipScaleFactor", setInt)
}
void UIConfig::setEncTextScaleFactor(int v) {
  OCPN_UI_SET(m_enc_text_scale, v, "ui/encTextScaleFactor", setInt)
}
void UIConfig::setEncSoundingScaleFactor(int v) {
  OCPN_UI_SET(m_enc_sounding_scale, v, "ui/encSoundingScaleFactor", setInt)
}

void UIConfig::setAnchorAlarmSound(bool v) {
  OCPN_UI_SET(m_anchor_sound, v, "sound/anchorEnable", setBool)
}
void UIConfig::setAnchorSoundFile(const QString& v) {
  OCPN_UI_SET(m_anchor_file, v, "sound/anchorFile", setString)
}
void UIConfig::setAisAlertSound(bool v) {
  OCPN_UI_SET(m_ais_sound, v, "sound/aisEnable", setBool)
}
void UIConfig::setAisSoundFile(const QString& v) {
  OCPN_UI_SET(m_ais_file, v, "sound/aisFile", setString)
}
void UIConfig::setSartAlertSound(bool v) {
  OCPN_UI_SET(m_sart_sound, v, "sound/sartEnable", setBool)
}
void UIConfig::setSartSoundFile(const QString& v) {
  OCPN_UI_SET(m_sart_file, v, "sound/sartFile", setString)
}
void UIConfig::setDscAlertSound(bool v) {
  OCPN_UI_SET(m_dsc_sound, v, "sound/dscEnable", setBool)
}
void UIConfig::setDscSoundFile(const QString& v) {
  OCPN_UI_SET(m_dsc_file, v, "sound/dscFile", setString)
}

#undef OCPN_UI_SET

}  // namespace ocpn::qtui
