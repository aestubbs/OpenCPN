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
 * The example Qt plugin (P4.2 validation): contributes one HUD component
 * (a clock pill bound to a Q_PROPERTY on the plugin context) and one
 * settings page. Deliberately contributes NO Layer: scene-graph Layers
 * need the layer toolkit's symbols, which live in the executable today --
 * exporting them as a shared library is the recorded Phase-4 follow-up.
 * Build the `opencpn-qt-example-plugin` target and drop the module into
 * <AppData>/opencpn-qt/plugins-qt/ to see it under Options > Plugins.
 */

#ifndef OCPN_QT_EXAMPLE_PLUGIN_H_
#define OCPN_QT_EXAMPLE_PLUGIN_H_

#include <QObject>
#include <QString>
#include <QTimer>

#include "../ocpn_qt_plugin.h"

class ExamplePluginContext : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString clockText READ clockText NOTIFY clockChanged)
public:
  explicit ExamplePluginContext(QObject* parent = nullptr);
  QString clockText() const { return m_text; }
Q_SIGNALS:
  void clockChanged();
private:
  QString m_text;
  QTimer m_timer;
};

class ExamplePlugin : public QObject, public ocpn::qtui::OcpnQtPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID OcpnQtPlugin_iid)
  Q_INTERFACES(ocpn::qtui::OcpnQtPlugin)

public:
  QString name() const override { return QStringLiteral("Example"); }
  QString version() const override { return QStringLiteral("1.0"); }
  QString description() const override {
    return QStringLiteral("API validation: a HUD clock + a settings page.");
  }
  bool init(const ocpn::qtui::OcpnQtPluginHost& host) override;
  void deinit() override;

private:
  ExamplePluginContext* m_ctx = nullptr;
};

#endif  // OCPN_QT_EXAMPLE_PLUGIN_H_
