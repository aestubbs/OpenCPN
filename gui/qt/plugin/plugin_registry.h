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
 * PluginRegistry (P4.2): discover + load OcpnQtPlugin modules
 * (QPluginLoader over the app plugins directory), drive their
 * init/deinit against the host seams, and surface the catalogue +
 * per-plugin enable switches (persisted) to QML. Contributions:
 * registered Layers go to the LayerCompositor via the host callback the
 * canvas supplies; HUD / settings-page components are collected here and
 * instantiated by the QML shell.
 */

#ifndef OCPN_QT_PLUGIN_REGISTRY_H_
#define OCPN_QT_PLUGIN_REGISTRY_H_

#include <functional>

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include "ocpn_qt_plugin.h"

QT_BEGIN_NAMESPACE
class QPluginLoader;
QT_END_NAMESPACE

namespace ocpn::qtui {

class PluginRegistry : public QObject {
  Q_OBJECT
  // Catalogue rows: {name, version, description, enabled, loaded, error}.
  Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
  // HUD contributions: {component (url), context (QObject*)} for the shell
  // to instantiate above the chart.
  Q_PROPERTY(QVariantList hudComponents READ hudComponents NOTIFY
                 contributionsChanged)
  // Settings pages: {title, component (url), context (QObject*)}.
  Q_PROPERTY(QVariantList settingsPages READ settingsPages NOTIFY
                 contributionsChanged)

public:
  /** `registerLayer` forwards a plugin's Layer to the compositor (the
   *  canvas supplies it); `navData` is the snapshot provider. */
  PluginRegistry(std::function<void(Layer*)> registerLayer,
                 NavDataProvider* navData, QObject* parent = nullptr);
  ~PluginRegistry() override;

  /** Scan `dir` for plugin modules and load the enabled ones. Safe to call
   *  with a missing/empty dir (loads nothing). */
  void loadFrom(const QString& dir);

  QVariantList plugins() const { return m_rows; }
  QVariantList hudComponents() const { return m_huds; }
  QVariantList settingsPages() const { return m_pages; }

  /** Enable/disable by name (persisted). A change applies on restart --
   *  contributions cannot be detached live in v1. */
  Q_INVOKABLE void setPluginEnabled(const QString& name, bool enabled);

Q_SIGNALS:
  void pluginsChanged();
  void contributionsChanged();

private:
  struct Loaded {
    QPluginLoader* loader = nullptr;
    OcpnQtPlugin* iface = nullptr;
  };
  std::function<void(Layer*)> m_register_layer;
  NavDataProvider* m_nav_data = nullptr;
  QList<Loaded> m_loaded;
  QVariantList m_rows;
  QVariantList m_huds;
  QVariantList m_pages;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_PLUGIN_REGISTRY_H_
