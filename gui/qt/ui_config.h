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
 * UIConfig -- the user-interface preferences, mirroring the wx Options > User
 * Interface page (General Options + Sounds). A QML_SINGLETON, persisted via
 * ConfigStore, referenced in QML as `UIConfig`.
 *
 * Many of these ARE wired live to the QML shell: status-bar / chart-bar /
 * zoom-button visibility, toolbar transparency + auto-hide, and the UI scale
 * factor (which drives the touch-target size). The harder items -- language
 * (awaits Qt Linguist i18n, P3.10), per-element fonts (a FontMgr equivalent),
 * the chart/ship/ENC scale factors (await renderer scaling), ship's bells and
 * the per-event alert sounds (await a sound engine) -- are persisted now and
 * marked pending in the P3.6 inventory.
 *
 * Compass-window visibility and mouse-wheel zoom sensitivity are deliberately
 * NOT duplicated here -- the UI page binds the existing DisplayConfig
 * properties, so the Display and UI pages stay in sync.
 */

#ifndef OCPN_QT_UI_CONFIG_H_
#define OCPN_QT_UI_CONFIG_H_

#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace ocpn::qtui {

class UIConfig : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  // --- General Options -----------------------------------------------------
  // Language index (0 = system default). Persisted; pending Qt Linguist i18n.
  Q_PROPERTY(int language READ language WRITE setLanguage NOTIFY changed)
  Q_PROPERTY(bool showStatusBar READ showStatusBar WRITE setShowStatusBar
                 NOTIFY changed)
  Q_PROPERTY(bool showChartBar READ showChartBar WRITE setShowChartBar NOTIFY
                 changed)
  Q_PROPERTY(bool showZoomButtons READ showZoomButtons WRITE setShowZoomButtons
                 NOTIFY changed)
  Q_PROPERTY(bool autoHideToolbar READ autoHideToolbar WRITE setAutoHideToolbar
                 NOTIFY changed)
  Q_PROPERTY(int autoHideTimeout READ autoHideTimeout WRITE setAutoHideTimeout
                 NOTIFY changed)
  // 0.0 (opaque) .. 0.9 (nearly invisible) -- floating toolbar transparency.
  Q_PROPERTY(double toolbarTransparency READ toolbarTransparency WRITE
                 setToolbarTransparency NOTIFY changed)
  Q_PROPERTY(bool touchInterface READ touchInterface WRITE setTouchInterface
                 NOTIFY changed)
  Q_PROPERTY(bool playShipsBells READ playShipsBells WRITE setPlayShipsBells
                 NOTIFY changed)
  // Experimental split view (P6.1): a second, independent chart pane.
  // Takes effect on restart (the pane's decode engine is created at
  // startup).
  Q_PROPERTY(bool splitView READ splitView WRITE setSplitView NOTIFY changed)
  // Options-window position persistence (-1 = unset; size is fixed).
  Q_PROPERTY(int optionsX READ optionsX WRITE setOptionsX NOTIFY changed)
  Q_PROPERTY(int optionsY READ optionsY WRITE setOptionsY NOTIFY changed)
  Q_PROPERTY(bool inlandEcdis READ inlandEcdis WRITE setInlandEcdis NOTIFY
                 changed)
  // Scale factors, -5 .. +5 (0 = none). guiScaleFactor is consumed live (touch
  // target size); the rest are persisted pending renderer scaling.
  Q_PROPERTY(int guiScaleFactor READ guiScaleFactor WRITE setGuiScaleFactor
                 NOTIFY changed)
  Q_PROPERTY(int chartObjectScaleFactor READ chartObjectScaleFactor WRITE
                 setChartObjectScaleFactor NOTIFY changed)
  Q_PROPERTY(int shipScaleFactor READ shipScaleFactor WRITE setShipScaleFactor
                 NOTIFY changed)
  Q_PROPERTY(int encTextScaleFactor READ encTextScaleFactor WRITE
                 setEncTextScaleFactor NOTIFY changed)
  Q_PROPERTY(int encSoundingScaleFactor READ encSoundingScaleFactor WRITE
                 setEncSoundingScaleFactor NOTIFY changed)

  // --- Sounds (persisted; pending the sound engine) ------------------------
  Q_PROPERTY(bool anchorAlarmSound READ anchorAlarmSound WRITE
                 setAnchorAlarmSound NOTIFY changed)
  Q_PROPERTY(QString anchorSoundFile READ anchorSoundFile WRITE
                 setAnchorSoundFile NOTIFY changed)
  Q_PROPERTY(bool aisAlertSound READ aisAlertSound WRITE setAisAlertSound
                 NOTIFY changed)
  Q_PROPERTY(QString aisSoundFile READ aisSoundFile WRITE setAisSoundFile
                 NOTIFY changed)
  Q_PROPERTY(bool sartAlertSound READ sartAlertSound WRITE setSartAlertSound
                 NOTIFY changed)
  Q_PROPERTY(QString sartSoundFile READ sartSoundFile WRITE setSartSoundFile
                 NOTIFY changed)
  Q_PROPERTY(bool dscAlertSound READ dscAlertSound WRITE setDscAlertSound
                 NOTIFY changed)
  Q_PROPERTY(QString dscSoundFile READ dscSoundFile WRITE setDscSoundFile
                 NOTIFY changed)

public:
  static UIConfig& instance();
  static UIConfig* create(QQmlEngine*, QJSEngine*) {
    UIConfig* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  int language() const { return m_language; }
  void setLanguage(int v);
  bool showStatusBar() const { return m_status_bar; }
  void setShowStatusBar(bool v);
  bool showChartBar() const { return m_chart_bar; }
  void setShowChartBar(bool v);
  bool showZoomButtons() const { return m_zoom_buttons; }
  void setShowZoomButtons(bool v);
  bool autoHideToolbar() const { return m_auto_hide; }
  void setAutoHideToolbar(bool v);
  int autoHideTimeout() const { return m_auto_hide_timeout; }
  void setAutoHideTimeout(int v);
  double toolbarTransparency() const { return m_toolbar_transparency; }
  void setToolbarTransparency(double v);
  bool touchInterface() const { return m_touch; }
  void setTouchInterface(bool v);
  bool playShipsBells() const { return m_ships_bells; }
  void setPlayShipsBells(bool v);
  bool splitView() const { return m_split_view; }
  void setSplitView(bool v);
  int optionsX() const { return m_options_x; }
  void setOptionsX(int v);
  int optionsY() const { return m_options_y; }
  void setOptionsY(int v);
  bool inlandEcdis() const { return m_inland_ecdis; }
  void setInlandEcdis(bool v);
  int guiScaleFactor() const { return m_gui_scale; }
  void setGuiScaleFactor(int v);
  int chartObjectScaleFactor() const { return m_chart_scale; }
  void setChartObjectScaleFactor(int v);
  int shipScaleFactor() const { return m_ship_scale; }
  void setShipScaleFactor(int v);
  int encTextScaleFactor() const { return m_enc_text_scale; }
  void setEncTextScaleFactor(int v);
  int encSoundingScaleFactor() const { return m_enc_sounding_scale; }
  void setEncSoundingScaleFactor(int v);

  bool anchorAlarmSound() const { return m_anchor_sound; }
  void setAnchorAlarmSound(bool v);
  QString anchorSoundFile() const { return m_anchor_file; }
  void setAnchorSoundFile(const QString& v);
  bool aisAlertSound() const { return m_ais_sound; }
  void setAisAlertSound(bool v);
  QString aisSoundFile() const { return m_ais_file; }
  void setAisSoundFile(const QString& v);
  bool sartAlertSound() const { return m_sart_sound; }
  void setSartAlertSound(bool v);
  QString sartSoundFile() const { return m_sart_file; }
  void setSartSoundFile(const QString& v);
  bool dscAlertSound() const { return m_dsc_sound; }
  void setDscAlertSound(bool v);
  QString dscSoundFile() const { return m_dsc_file; }
  void setDscSoundFile(const QString& v);

Q_SIGNALS:
  void changed();

private:
  UIConfig();  // loads from the config store

  int m_language = 0;
  bool m_status_bar = true;
  bool m_chart_bar = true;
  bool m_zoom_buttons = true;
  bool m_auto_hide = false;
  int m_auto_hide_timeout = 5;
  double m_toolbar_transparency = 0.0;
  bool m_touch = false;
  bool m_ships_bells = false;
  bool m_split_view = false;
  int m_options_x = -1;
  int m_options_y = -1;
  bool m_inland_ecdis = false;
  int m_gui_scale = 0;
  int m_chart_scale = 0;
  int m_ship_scale = 0;
  int m_enc_text_scale = 0;
  int m_enc_sounding_scale = 0;

  bool m_anchor_sound = false;
  QString m_anchor_file;
  bool m_ais_sound = false;
  QString m_ais_file;
  bool m_sart_sound = false;
  QString m_sart_file;
  bool m_dsc_sound = false;
  QString m_dsc_file;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_UI_CONFIG_H_
