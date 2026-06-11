/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "plugin_registry.h"

#include <QDir>
#include <QPluginLoader>
#include <QVariantMap>

#include "../config_store.h"

namespace ocpn::qtui {

PluginRegistry::PluginRegistry(std::function<void(Layer*)> registerLayer,
                               NavDataProvider* navData, QObject* navMsgTap,
                               std::function<void(const QString&)> addChartDir,
                               QObject* ochartsService, QObject* parent)
    : QObject(parent),
      m_register_layer(std::move(registerLayer)),
      m_nav_data(navData),
      m_nav_msg_tap(navMsgTap),
      m_add_chart_dir(std::move(addChartDir)),
      m_ocharts_service(ochartsService) {}

PluginRegistry::~PluginRegistry() {
  for (const Loaded& l : m_loaded) {
    if (l.iface) l.iface->deinit();
    if (l.loader) l.loader->unload();
  }
}

void PluginRegistry::loadFrom(const QString& dir) {
  const QDir pdir(dir);
  if (!pdir.exists()) {
    Q_EMIT pluginsChanged();
    return;
  }
#if defined(Q_OS_MACOS)
  const QStringList globs{QStringLiteral("*.dylib")};
#elif defined(Q_OS_WIN)
  const QStringList globs{QStringLiteral("*.dll")};
#else
  const QStringList globs{QStringLiteral("*.so")};
#endif
  ConfigStore& cfg = ConfigStore::instance();
  for (const QString& file : pdir.entryList(globs, QDir::Files)) {
    const QString path = pdir.absoluteFilePath(file);
    auto* loader = new QPluginLoader(path, this);
    QVariantMap row;
    row["name"] = file;  // refined from the instance below
    row["enabled"] = true;
    row["loaded"] = false;
    row["error"] = QString();

    OcpnQtPlugin* iface = nullptr;
    if (QObject* inst = loader->instance())
      iface = qobject_cast<OcpnQtPlugin*>(inst);
    if (!iface) {
      row["error"] = loader->errorString();
      loader->unload();
      m_rows.append(row);
      continue;
    }
    row["name"] = iface->name();
    row["version"] = iface->version();
    row["description"] = iface->description();
    const bool enabled =
        cfg.getBool(QStringLiteral("plugins/enabled_") + iface->name(), true);
    row["enabled"] = enabled;
    if (!enabled) {
      loader->unload();
      m_rows.append(row);
      continue;
    }

    OcpnQtPluginHost host;
    host.registerLayer = m_register_layer;
    host.registerHud = [this](const QUrl& component, QObject* context) {
      QVariantMap h;
      h["component"] = component;
      h["context"] = QVariant::fromValue(context);
      m_huds.append(h);
    };
    host.registerSettingsPage = [this](const QString& title,
                                       const QUrl& component,
                                       QObject* context) {
      QVariantMap p;
      p["title"] = title;
      p["component"] = component;
      p["context"] = QVariant::fromValue(context);
      m_pages.append(p);
    };
    host.navData = m_nav_data;
    host.navMsgTap = m_nav_msg_tap;
    host.addChartDirectory = m_add_chart_dir;
    host.ochartsService = m_ocharts_service;

    if (!iface->init(host)) {
      row["error"] = tr("init() failed");
      loader->unload();
      m_rows.append(row);
      continue;
    }
    row["loaded"] = true;
    m_rows.append(row);
    m_loaded.append({loader, iface});
    qInfo("PluginRegistry: loaded %s %s",
          qPrintable(iface->name()), qPrintable(iface->version()));
  }
  Q_EMIT pluginsChanged();
  Q_EMIT contributionsChanged();
}

void PluginRegistry::setPluginEnabled(const QString& name, bool enabled) {
  ConfigStore::instance().setBool(QStringLiteral("plugins/enabled_") + name,
                                  enabled);
  for (int i = 0; i < m_rows.size(); ++i) {
    QVariantMap row = m_rows[i].toMap();
    if (row.value("name").toString() == name) {
      row["enabled"] = enabled;
      m_rows[i] = row;
      break;
    }
  }
  Q_EMIT pluginsChanged();
}

}  // namespace ocpn::qtui
