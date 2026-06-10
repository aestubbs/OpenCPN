/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "example_plugin.h"

#include <QTime>
#include <QUrl>

ExamplePluginContext::ExamplePluginContext(QObject* parent)
    : QObject(parent) {
  m_text = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
  m_timer.setInterval(1000);
  connect(&m_timer, &QTimer::timeout, this, [this]() {
    m_text = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    Q_EMIT clockChanged();
  });
  m_timer.start();
}

bool ExamplePlugin::init(const ocpn::qtui::OcpnQtPluginHost& host) {
  m_ctx = new ExamplePluginContext(this);
  if (host.registerHud)
    host.registerHud(QUrl(QStringLiteral("qrc:/example_plugin/Hud.qml")),
                     m_ctx);
  if (host.registerSettingsPage)
    host.registerSettingsPage(
        QStringLiteral("Example plugin"),
        QUrl(QStringLiteral("qrc:/example_plugin/Settings.qml")), m_ctx);
  return true;
}

void ExamplePlugin::deinit() {
  delete m_ctx;
  m_ctx = nullptr;
}
