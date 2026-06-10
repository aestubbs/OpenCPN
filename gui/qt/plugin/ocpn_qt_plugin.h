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
 * OcpnQtPlugin -- the DRAFT Qt plugin interface (P4.1). A clean break from
 * the frozen wx plugin ABI (`include/ocpn_plugin.h`): plugins are
 * QPluginLoader modules implementing this pure-Qt interface, contributing
 *   - world- or display-anchored chart **Layers** (the same `Layer`
 *     contract every built-in overlay uses -- scene-graph subtrees,
 *     visible/zOrder/opacity managed by the LayerCompositor), and
 *   - **QML HUD components** (a module URL instantiated by the shell, with
 *     the plugin's QObject context exposed), and
 *   - an optional **settings page** (QML component shown under Options >
 *     Plugins), and
 *   - **navigation data subscriptions** (decoded NavMsg stream + the
 *     NavDataProvider snapshot accessors), replacing the wx ABI's
 *     SetPositionFixEx / plugin-message string surface.
 *
 * Status: DRAFT pending the Phase-4 "day-1 plugins" decision (P3.21 item
 * 5). Not yet loaded by the app; the loader (P4.2) lands once the
 * interface settles. Versioned via the IID string -- bump on any breaking
 * change.
 */

#ifndef OCPN_QT_PLUGIN_H_
#define OCPN_QT_PLUGIN_H_

#include <functional>

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

namespace ocpn::qtui {

class Layer;             // gui/qt/layer.h -- the scene-graph Layer contract
class NavDataProvider;   // gui/qt/nav_data_provider.h -- nav snapshots

/** What the host hands a plugin at init: the contribution registry plus
 *  the data seams it may consume. All pointers are host-owned and outlive
 *  the plugin. */
struct OcpnQtPluginHost {
  /** Register a Layer (host takes ownership; it joins the compositor with
   *  per-layer visible/zOrder/opacity persistence like built-ins). */
  std::function<void(Layer*)> registerLayer;
  /** Register a QML HUD component (a qrc:/ or module URL). The shell
   *  instantiates it above the chart with `plugin` as a context object. */
  std::function<void(const QUrl& component, QObject* context)> registerHud;
  /** Register a settings page shown under Options > Plugins. */
  std::function<void(const QString& title, const QUrl& component,
                     QObject* context)> registerSettingsPage;
  /** Add a directory to the chart library (Options > Charts > Chart
   *  Files) and rescan -- e.g. after a chart downloader installs cells.
   *  Idempotent for already-listed directories. */
  std::function<void(const QString& dir)> addChartDirectory;
  /** Live navigation snapshots (own ship, AIS, routes...). */
  NavDataProvider* navData = nullptr;
  /** The decoded message tap: a QObject emitting
   *  `lineReceived(QString line, QString source)` per decoded NMEA-0183
   *  sentence / N2K PGN / SignalK message ("HH:mm:ss  <payload>" + the
   *  connection tag). Connect with the string-based SIGNAL() form -- the
   *  emitter's concrete type is not part of the plugin API. */
  QObject* navMsgTap = nullptr;
};

/** The plugin interface proper. Implementations are QObject-derived and
 *  declare Q_PLUGIN_METADATA(IID OcpnQtPlugin_iid). */
class OcpnQtPlugin {
public:
  virtual ~OcpnQtPlugin() = default;

  /** Identity for the plugin manager UI. */
  virtual QString name() const = 0;
  virtual QString version() const = 0;
  virtual QString description() const = 0;

  /** Called once after load, on the GUI thread, with the host seams.
   *  Register contributions here. False = abort load (host unloads). */
  virtual bool init(const OcpnQtPluginHost& host) = 0;

  /** Called once before unload; contributions are already detached. */
  virtual void deinit() = 0;
};

}  // namespace ocpn::qtui

#define OcpnQtPlugin_iid "org.opencpn.qt.plugin/1.0"
Q_DECLARE_INTERFACE(ocpn::qtui::OcpnQtPlugin, OcpnQtPlugin_iid)

#endif  // OCPN_QT_PLUGIN_H_
