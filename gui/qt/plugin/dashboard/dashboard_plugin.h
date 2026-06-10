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
 * The Dashboard Qt plugin (P4.3): a configurable instrument strip over
 * the chart, fed from the host's NavDataProvider (linked via the
 * opencpn_qt_toolkit shared library -- the first plugin to consume nav
 * data). v1 instruments: SOG, COG, HDG, STW, apparent + true wind,
 * position. Per-instrument visibility persists in the plugin's own
 * QSettings; the settings page toggles them live.
 */

#ifndef OCPN_QT_DASHBOARD_PLUGIN_H_
#define OCPN_QT_DASHBOARD_PLUGIN_H_

#include <QObject>
#include <QString>
#include <QStringList>

#include "../ocpn_qt_plugin.h"

namespace ocpn::qtui {
class NavDataProvider;
}

class DashboardContext : public QObject {
  Q_OBJECT
  // Formatted instrument readouts ("--" when unavailable).
  Q_PROPERTY(QString sog READ sog NOTIFY navChanged)
  Q_PROPERTY(QString cog READ cog NOTIFY navChanged)
  Q_PROPERTY(QString hdg READ hdg NOTIFY navChanged)
  Q_PROPERTY(QString stw READ stw NOTIFY navChanged)
  Q_PROPERTY(QString awaAws READ awaAws NOTIFY navChanged)
  Q_PROPERTY(QString twaTws READ twaTws NOTIFY navChanged)
  Q_PROPERTY(QString position READ position NOTIFY navChanged)
  Q_PROPERTY(QString depth READ depth NOTIFY navChanged)
  // Numeric headings for the gauge dials (-1 = unavailable).
  Q_PROPERTY(double cogDeg READ cogDeg NOTIFY navChanged)
  Q_PROPERTY(double hdgDeg READ hdgDeg NOTIFY navChanged)
  Q_PROPERTY(bool gauges READ gauges WRITE setGauges NOTIFY enabledChanged)
  // "tl" / "tr" / "bl" / "br" corner + vertical stacking.
  Q_PROPERTY(QString corner READ corner WRITE setCorner NOTIFY enabledChanged)
  Q_PROPERTY(bool vertical READ vertical WRITE setVertical NOTIFY
                 enabledChanged)
  Q_PROPERTY(QString waterTemp READ waterTemp NOTIFY navChanged)
  // Which instruments show, by key (persisted plugin-side).
  Q_PROPERTY(QStringList enabled READ enabled NOTIFY enabledChanged)

public:
  explicit DashboardContext(ocpn::qtui::NavDataProvider* nav,
                            QObject* navMsgTap, QObject* parent = nullptr);

  QString sog() const { return m_sog; }
  QString cog() const { return m_cog; }
  QString hdg() const { return m_hdg; }
  QString stw() const { return m_stw; }
  QString awaAws() const { return m_awa; }
  QString twaTws() const { return m_twa; }
  QString position() const { return m_pos; }
  QString depth() const { return m_depth; }
  double cogDeg() const { return m_cog_deg; }
  double hdgDeg() const { return m_hdg_deg; }
  bool gauges() const { return m_gauges; }
  void setGauges(bool on);
  QString corner() const { return m_corner; }
  void setCorner(const QString& c);
  bool vertical() const { return m_vertical; }
  void setVertical(bool on);
  QString waterTemp() const { return m_wtemp; }
  QStringList enabled() const { return m_enabled; }

  /** All instrument keys, in display order. */
  Q_INVOKABLE QStringList allInstruments() const;
  Q_INVOKABLE void setInstrumentEnabled(const QString& key, bool on);

Q_SIGNALS:
  void navChanged();
  void enabledChanged();

private Q_SLOTS:
  void onNavMsg(const QString& line, const QString& source);

private:
  void refresh();
  ocpn::qtui::NavDataProvider* m_nav;
  QString m_sog, m_cog, m_hdg, m_stw, m_awa, m_twa, m_pos;
  QString m_depth = QStringLiteral("--");
  double m_cog_deg = -1;
  double m_hdg_deg = -1;
  bool m_gauges = false;
  QString m_corner = QStringLiteral("tl");
  bool m_vertical = false;
  QString m_wtemp = QStringLiteral("--");
  QStringList m_enabled;
};

class DashboardPlugin : public QObject, public ocpn::qtui::OcpnQtPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID OcpnQtPlugin_iid)
  Q_INTERFACES(ocpn::qtui::OcpnQtPlugin)

public:
  QString name() const override { return QStringLiteral("Dashboard"); }
  QString version() const override { return QStringLiteral("1.0"); }
  QString description() const override {
    return QStringLiteral(
        "Configurable instrument strip (SOG/COG/HDG/STW/wind/position).");
  }
  bool init(const ocpn::qtui::OcpnQtPluginHost& host) override;
  void deinit() override;

private:
  DashboardContext* m_ctx = nullptr;
};

#endif  // OCPN_QT_DASHBOARD_PLUGIN_H_
