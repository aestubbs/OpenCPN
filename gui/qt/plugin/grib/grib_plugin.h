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
 * The GRIB Qt plugin (P4.5, v1 of the grib_pi port): open a local GRIB
 * 1/2 file through the vendored zyGrib decode core (GribReader --
 * compiled as-is, wx-string surface and all), step its timeline, and
 * render the 10 m wind field as a world-anchored arrow Layer through the
 * opencpn_qt_toolkit -- the first Layer-contributing plugin. v1 scope:
 * wind arrows only; pressure isobars / precip / waves / the request
 * builder follow.
 */

#ifndef OCPN_QT_GRIB_PLUGIN_H_
#define OCPN_QT_GRIB_PLUGIN_H_

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "../ocpn_qt_plugin.h"

class GribReader;

namespace ocpn::qtui {
class GribWindLayer;
}

class GribContext : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString fileName READ fileName NOTIFY gribChanged)
  Q_PROPERTY(QStringList timeSteps READ timeSteps NOTIFY gribChanged)
  Q_PROPERTY(int timeIndex READ timeIndex WRITE setTimeIndex NOTIFY
                 timeChanged)
  Q_PROPERTY(bool showWind READ showWind WRITE setShowWind NOTIFY timeChanged)
  Q_PROPERTY(bool showPressure READ showPressure WRITE setShowPressure NOTIFY
                 timeChanged)
  Q_PROPERTY(QString status READ status NOTIFY gribChanged)

public:
  explicit GribContext(QObject* parent = nullptr);
  ~GribContext() override;

  QString fileName() const { return m_file; }
  QStringList timeSteps() const { return m_steps; }
  int timeIndex() const { return m_time_index; }
  void setTimeIndex(int i);
  bool showWind() const { return m_show_wind; }
  void setShowWind(bool on);
  bool showPressure() const { return m_show_pressure; }
  void setShowPressure(bool on);
  QString status() const { return m_status; }

  Q_INVOKABLE void openFile(const QUrl& url);

  /** The Layer (owned by the compositor once registered). */
  ocpn::qtui::GribWindLayer* layer() const { return m_layer; }
  void setLayer(ocpn::qtui::GribWindLayer* l) { m_layer = l; }

Q_SIGNALS:
  void gribChanged();
  void timeChanged();

private:
  void pushToLayer();

  GribReader* m_reader = nullptr;
  ocpn::qtui::GribWindLayer* m_layer = nullptr;  // compositor-owned
  QString m_file;
  QStringList m_steps;
  QList<long long> m_step_times;
  int m_time_index = 0;
  bool m_show_wind = true;
  bool m_show_pressure = true;
  QString m_status;
};

class GribPlugin : public QObject, public ocpn::qtui::OcpnQtPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID OcpnQtPlugin_iid)
  Q_INTERFACES(ocpn::qtui::OcpnQtPlugin)

public:
  QString name() const override { return QStringLiteral("GRIB"); }
  QString version() const override { return QStringLiteral("1.0"); }
  QString description() const override {
    return QStringLiteral("GRIB 1/2 weather files: wind-field overlay (v1).");
  }
  bool init(const ocpn::qtui::OcpnQtPluginHost& host) override;
  void deinit() override;

private:
  GribContext* m_ctx = nullptr;
};

#endif  // OCPN_QT_GRIB_PLUGIN_H_
