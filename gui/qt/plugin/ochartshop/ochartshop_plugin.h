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
 * The o-charts SHOP Qt plugin (P4.7, day-1 per user decision): the
 * chart-purchase/installation flows of the wx o-charts_pi against the
 * live o-charts.org API (endpoints + parameters extracted from
 * ochartShop.cpp): login2 / getlist / identifySystem / assign / request,
 * fingerprint via the host's o-charts daemon service (oexserverd -g),
 * ZIP download + libarchive install + keyList placement, then
 * host.addChartDirectory. Rendering/decryption is already native (P2).
 */

#ifndef OCPN_QT_OCHARTSHOP_PLUGIN_H_
#define OCPN_QT_OCHARTSHOP_PLUGIN_H_

#include <functional>

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include "../ocpn_qt_plugin.h"

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

class ShopContext : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString username READ username NOTIFY stateChanged)
  Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY stateChanged)
  Q_PROPERTY(QString systemName READ systemName NOTIFY stateChanged)
  // {chartName, chartId, order, edition, editionDate, purchase, expiration,
  //  expired, chartType, supported, assignedHere, slotUuid, quantityId,
  //  installed} per purchased chart set.
  Q_PROPERTY(QVariantList charts READ charts NOTIFY chartsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(int progress READ progress NOTIFY busyChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
  ShopContext(QObject* ochartsService,
              std::function<void(const QString&)> addChartDir,
              QObject* parent = nullptr);

  QString username() const { return m_user; }
  bool loggedIn() const { return !m_key.isEmpty(); }
  QString systemName() const { return m_system_name; }
  QVariantList charts() const { return m_charts; }
  bool busy() const { return m_busy; }
  int progress() const { return m_progress; }
  QString status() const { return m_status; }

  Q_INVOKABLE void login(const QString& user, const QString& password);
  Q_INVOKABLE void logout();
  Q_INVOKABLE void refreshList();
  /** Register THIS machine with the account (taskId=identifySystem with
   *  the oexserverd fingerprint); needed once per machine. */
  Q_INVOKABLE void identifySystem();
  /** Assign (if needed) + download + install the chart set at `index`. */
  Q_INVOKABLE void installChart(int index);

Q_SIGNALS:
  void stateChanged();
  void chartsChanged();
  void busyChanged();
  void statusChanged();

private:
  void post(const QString& params,
            std::function<void(int result, const QByteArray& xml)> done);
  void parseList(const QByteArray& xml);
  void withFingerprintHex(
      std::function<void(const QString& hex, const QString& name)> done);
  void requestDownload(const QVariantMap& chart);
  void downloadAndInstall(const QString& link, const QString& keysLink,
                          const QString& chartName);
  bool extractZip(const QString& zipPath, const QString& destDir,
                  QString* rootDir);
  void setBusy(bool b, int progress = 0);
  void setStatus(const QString& s);
  QString versionParam() const;

  QNetworkAccessManager* m_nam = nullptr;
  QObject* m_ocharts = nullptr;  // host daemon service (by-name access)
  std::function<void(const QString&)> m_add_chart_dir;
  QString m_user, m_key, m_system_name;
  QVariantList m_charts;
  bool m_busy = false;
  int m_progress = 0;
  QString m_status;
};

class OchartShopPlugin : public QObject, public ocpn::qtui::OcpnQtPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID OcpnQtPlugin_iid)
  Q_INTERFACES(ocpn::qtui::OcpnQtPlugin)

public:
  QString name() const override { return QStringLiteral("o-charts Shop"); }
  QString version() const override { return QStringLiteral("1.0"); }
  QString description() const override {
    return QStringLiteral(
        "Purchased o-charts: account login, system registration, chart "
        "download and installation.");
  }
  bool init(const ocpn::qtui::OcpnQtPluginHost& host) override;
  void deinit() override;

private:
  ShopContext* m_ctx = nullptr;
};

#endif  // OCPN_QT_OCHARTSHOP_PLUGIN_H_
