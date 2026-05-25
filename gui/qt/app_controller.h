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
 * AppController -- a tiny shared-state bridge between QML and native code,
 * exposed to QML as the context property "app". Currently just the
 * vessel-data drawer state: QML binds to hudExpanded, and the native macOS
 * title-bar accessory button (macos_titlebar.mm) toggles it from outside the
 * QML scene. titlebarToggle is set true once the native button is installed,
 * so QML can hide the floating-toolbar fallback toggle.
 */

#ifndef OCPN_QT_APP_CONTROLLER_H_
#define OCPN_QT_APP_CONTROLLER_H_

#include <QObject>

namespace ocpn::qtui {

class AppController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool hudExpanded READ hudExpanded WRITE setHudExpanded NOTIFY
                 hudExpandedChanged)
  Q_PROPERTY(bool titlebarToggle READ titlebarToggle WRITE setTitlebarToggle
                 NOTIFY titlebarToggleChanged)

public:
  explicit AppController(QObject* parent = nullptr) : QObject(parent) {}

  bool hudExpanded() const { return m_hud_expanded; }
  void setHudExpanded(bool on) {
    if (on == m_hud_expanded) return;
    m_hud_expanded = on;
    Q_EMIT hudExpandedChanged();
  }
  Q_INVOKABLE void toggleHud() { setHudExpanded(!m_hud_expanded); }

  bool titlebarToggle() const { return m_titlebar_toggle; }
  void setTitlebarToggle(bool on) {
    if (on == m_titlebar_toggle) return;
    m_titlebar_toggle = on;
    Q_EMIT titlebarToggleChanged();
  }

Q_SIGNALS:
  void hudExpandedChanged();
  void titlebarToggleChanged();

private:
  bool m_hud_expanded = false;
  bool m_titlebar_toggle = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_APP_CONTROLLER_H_
