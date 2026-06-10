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
 * The Chart Downloader Qt plugin (P4.4, v1 of the chartdldr_pi port):
 * fetch a chart-catalog XML (the NOAA <chart> schema chartcatalog.cpp
 * parses), list the entries, download a selected chart ZIP and extract it
 * (libarchive) into a chosen folder -- which the user then adds under
 * Options > Charts > Chart Files. Settings-page UI only (no HUD/Layer).
 */

#ifndef OCPN_QT_CHARTDLDR_PLUGIN_H_
#define OCPN_QT_CHARTDLDR_PLUGIN_H_

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include "../ocpn_qt_plugin.h"

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
QT_END_NAMESPACE

class ChartDldrContext : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString catalogUrl READ catalogUrl WRITE setCatalogUrl NOTIFY
                 catalogChanged)
  Q_PROPERTY(QString catalogTitle READ catalogTitle NOTIFY catalogChanged)
  // {title, url, dateText} per catalog chart entry.
  Q_PROPERTY(QVariantList charts READ charts NOTIFY catalogChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(int progress READ progress NOTIFY busyChanged)  // 0-100
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QUrl targetFolder READ targetFolder WRITE setTargetFolder NOTIFY
                 statusChanged)

public:
  explicit ChartDldrContext(QObject* parent = nullptr);

  QString catalogUrl() const { return m_catalog_url; }
  void setCatalogUrl(const QString& url);
  QString catalogTitle() const { return m_catalog_title; }
  QVariantList charts() const { return m_charts; }
  bool busy() const { return m_busy; }
  int progress() const { return m_progress; }
  QString status() const { return m_status; }
  QUrl targetFolder() const { return m_target; }
  void setTargetFolder(const QUrl& url);

  /** Bundled catalog presets: {label, url} (the wx chartdldr ships a
   *  sources list; these are the NOAA entries that remain live). */
  Q_INVOKABLE QVariantList presets() const;

  Q_INVOKABLE void loadCatalog();
  Q_INVOKABLE void downloadChart(int index);
  /** Queue every catalog entry; charts whose extracted folder already
   *  exists in the target are skipped. */
  Q_INVOKABLE void downloadAll();
  Q_INVOKABLE void cancelAll();

Q_SIGNALS:
  void catalogChanged();
  void busyChanged();
  void statusChanged();

private:
  void parseCatalog(const QByteArray& xml);
  bool extractZip(const QString& zipPath, const QString& destDir);
  void setStatus(const QString& s);

  QNetworkAccessManager* m_nam = nullptr;
  QString m_catalog_url;
  QString m_catalog_title;
  QVariantList m_charts;
  bool m_busy = false;
  int m_progress = 0;
  QString m_status;
  QUrl m_target;
  QList<int> m_queue;  // pending catalog indices for downloadAll
};

class ChartDldrPlugin : public QObject, public ocpn::qtui::OcpnQtPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID OcpnQtPlugin_iid)
  Q_INTERFACES(ocpn::qtui::OcpnQtPlugin)

public:
  QString name() const override { return QStringLiteral("ChartDownloader"); }
  QString version() const override { return QStringLiteral("1.0"); }
  QString description() const override {
    return QStringLiteral(
        "Download charts from XML catalogs (NOAA schema) and extract them.");
  }
  bool init(const ocpn::qtui::OcpnQtPluginHost& host) override;
  void deinit() override;

private:
  ChartDldrContext* m_ctx = nullptr;
};

#endif  // OCPN_QT_CHARTDLDR_PLUGIN_H_
