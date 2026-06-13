/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "dashboard_plugin.h"

#include <cmath>

#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

#include "nav_data.h"           // OwnShipState (opencpn_qt_toolkit)
#include "nav_data_provider.h"  // NavDataProvider (opencpn_qt_toolkit)

using ocpn::qtui::NavDataProvider;
using ocpn::qtui::OwnShipState;

namespace {
QString deg(double v) {
  return QString::number(v, 'f', 0) + QChar(0x00B0);
}
QString kn(double v) { return QString::number(v, 'f', 1) + QStringLiteral(" kn"); }
QString ddm(double v, bool is_lat) {
  const char h = is_lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
  const double a = std::fabs(v);
  const int d = static_cast<int>(a);
  return QStringLiteral("%1°%2'%3")
      .arg(d)
      .arg((a - d) * 60.0, 0, 'f', 2)
      .arg(QChar(h));
}
const QStringList kAll{QStringLiteral("position"), QStringLiteral("sog"),
                       QStringLiteral("cog"),      QStringLiteral("hdg"),
                       QStringLiteral("stw"),      QStringLiteral("awa"),
                       QStringLiteral("twa"),      QStringLiteral("dpt"),
                       QStringLiteral("mtw")};
}  // namespace

DashboardContext::DashboardContext(NavDataProvider* nav, QObject* navMsgTap,
                                   QObject* parent)
    : QObject(parent), m_nav(nav) {
  // Depth/water-temp ride the raw message tap (DPT/MTW sentences); the
  // emitter's type is not part of the API, so use the string-based form.
  if (navMsgTap)
    connect(navMsgTap, SIGNAL(lineReceived(QString, QString)), this,
            SLOT(onNavMsg(QString, QString)));
  QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("dashboard-plugin"));
  m_gauges = st.value(QStringLiteral("gauges"), false).toBool();
  m_corner = st.value(QStringLiteral("corner"), QStringLiteral("tl"))
                 .toString();
  m_vertical = st.value(QStringLiteral("vertical"), false).toBool();
  m_enabled = st.value(QStringLiteral("enabled"),
                       QStringList{QStringLiteral("sog"), QStringLiteral("cog"),
                                   QStringLiteral("position")})
                  .toStringList();
  if (m_nav)
    connect(m_nav, &NavDataProvider::dynamicChanged, this,
            &DashboardContext::refresh);
  refresh();
}

QStringList DashboardContext::allInstruments() const { return kAll; }

void DashboardContext::setInstrumentEnabled(const QString& key, bool on) {
  if (on && !m_enabled.contains(key)) m_enabled.append(key);
  if (!on) m_enabled.removeAll(key);
  QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("dashboard-plugin"));
  st.setValue(QStringLiteral("enabled"), m_enabled);
  emit enabledChanged();
}

void DashboardContext::setGauges(bool on) {
  if (on == m_gauges) return;
  m_gauges = on;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("dashboard-plugin"))
      .setValue(QStringLiteral("gauges"), on);
  emit enabledChanged();
}

void DashboardContext::setCorner(const QString& c) {
  if (c == m_corner) return;
  m_corner = c;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("dashboard-plugin"))
      .setValue(QStringLiteral("corner"), c);
  emit enabledChanged();
}

void DashboardContext::setVertical(bool on) {
  if (on == m_vertical) return;
  m_vertical = on;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("dashboard-plugin"))
      .setValue(QStringLiteral("vertical"), on);
  emit enabledChanged();
}

void DashboardContext::onNavMsg(const QString& line,
                                const QString& /*source*/) {
  // $--DPT,<depth m>,<offset>  /  $--MTW,<temp>,C  (anywhere in the line --
  // the tap prefixes a timestamp).
  static const QRegularExpression dpt(
      QStringLiteral("[A-Z]{2}DPT,([0-9.+-]+)"));
  static const QRegularExpression mtw(
      QStringLiteral("[A-Z]{2}MTW,([0-9.+-]+)"));
  bool changed = false;
  if (const auto m = dpt.match(line); m.hasMatch()) {
    m_depth = m.captured(1) + QStringLiteral(" m");
    changed = true;
  }
  if (const auto m = mtw.match(line); m.hasMatch()) {
    m_wtemp = m.captured(1) + QStringLiteral(" °C");
    changed = true;
  }
  if (changed) emit navChanged();
}

void DashboardContext::refresh() {
  const OwnShipState s = m_nav ? m_nav->ownShip() : OwnShipState{};
  m_sog = s.valid ? kn(s.sog) : QStringLiteral("--");
  m_cog = s.valid ? deg(s.cog) : QStringLiteral("--");
  m_hdg = (s.hdg < 360.0) ? deg(s.hdg) : QStringLiteral("--");
  m_cog_deg = s.valid ? s.cog : -1;
  m_hdg_deg = (s.hdg < 360.0) ? s.hdg : -1;
  m_stw = (s.stw >= 0) ? kn(s.stw) : QStringLiteral("--");
  m_awa = (s.awa > -999 && s.aws >= 0)
              ? deg(s.awa) + QStringLiteral(" / ") + kn(s.aws)
              : QStringLiteral("--");
  m_twa = (s.twa > -999 && s.tws >= 0)
              ? deg(s.twa) + QStringLiteral(" / ") + kn(s.tws)
              : QStringLiteral("--");
  m_pos = s.valid ? ddm(s.lat, true) + QStringLiteral("  ") + ddm(s.lon, false)
                  : QStringLiteral("--");
  emit navChanged();
}

bool DashboardPlugin::init(const ocpn::qtui::OcpnQtPluginHost& host) {
  m_ctx = new DashboardContext(host.navData, host.navMsgTap, this);
  if (host.registerHud)
    host.registerHud(QUrl(QStringLiteral("qrc:/dashboard_plugin/Strip.qml")),
                     m_ctx);
  if (host.registerSettingsPage)
    host.registerSettingsPage(
        QStringLiteral("Dashboard"),
        QUrl(QStringLiteral("qrc:/dashboard_plugin/Settings.qml")), m_ctx);
  return true;
}

void DashboardPlugin::deinit() {
  delete m_ctx;
  m_ctx = nullptr;
}
