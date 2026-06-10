/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "chartdldr_plugin.h"

#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>
#include <QXmlStreamReader>

ChartDldrContext::ChartDldrContext(QObject* parent) : QObject(parent) {
  m_nam = new QNetworkAccessManager(this);
  QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("chartdldr-plugin"));
  // The NOAA ENC product catalog is the classic default (the same one the
  // wx chartdldr ships with).
  m_catalog_url =
      st.value(QStringLiteral("catalogUrl"),
               QStringLiteral(
                   "https://www.charts.noaa.gov/ENCs/ENCProdCat_19115.xml"))
          .toString();
  m_target = st.value(QStringLiteral("targetFolder")).toUrl();
}

void ChartDldrContext::setCatalogUrl(const QString& url) {
  if (url == m_catalog_url) return;
  m_catalog_url = url;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("chartdldr-plugin"))
      .setValue(QStringLiteral("catalogUrl"), url);
  Q_EMIT catalogChanged();
}

void ChartDldrContext::setTargetFolder(const QUrl& url) {
  m_target = url;
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("chartdldr-plugin"))
      .setValue(QStringLiteral("targetFolder"), url);
  Q_EMIT statusChanged();
}

void ChartDldrContext::setStatus(const QString& s) {
  m_status = s;
  Q_EMIT statusChanged();
}

QVariantList ChartDldrContext::presets() const {
  auto entry = [](const char* label, const char* url) {
    QVariantMap m;
    m["label"] = QString::fromUtf8(label);
    m["url"] = QString::fromUtf8(url);
    return QVariant(m);
  };
  return {entry("NOAA ENC (vector, all)",
                "https://www.charts.noaa.gov/ENCs/ENCProdCat_19115.xml"),
          entry("NOAA RNC (raster, all)",
                "https://www.charts.noaa.gov/RNCs/RNCProdCat_19115.xml"),
          entry("Inland ENC (US Army Corps)",
                "https://ienccloud.us/ienc/products/catalog/IENCU37ProdCat_19115.xml")};
}

void ChartDldrContext::loadCatalog() {
  if (m_busy || m_catalog_url.isEmpty()) return;
  m_busy = true;
  Q_EMIT busyChanged();
  setStatus(tr("Fetching catalog…"));
  QNetworkReply* reply = m_nam->get(QNetworkRequest(QUrl(m_catalog_url)));
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    m_busy = false;
    Q_EMIT busyChanged();
    if (reply->error() != QNetworkReply::NoError) {
      setStatus(tr("Catalog fetch failed: %1").arg(reply->errorString()));
      return;
    }
    parseCatalog(reply->readAll());
  });
}

void ChartDldrContext::parseCatalog(const QByteArray& xml) {
  // The chartdldr catalog schema: a root with <title> and repeated <chart>
  // elements carrying <title> + <zipfile_location> (+ datetimes). Namespace
  // and depth-agnostic: keyed on element names, like the wx parser.
  m_charts.clear();
  m_catalog_title.clear();
  QXmlStreamReader r(xml);
  QVariantMap cur;
  bool in_chart = false;
  while (!r.atEnd()) {
    const auto tok = r.readNext();
    if (tok == QXmlStreamReader::StartElement) {
      const auto name = r.name();
      if (name == QLatin1String("chart")) {
        in_chart = true;
        cur.clear();
      } else if (in_chart && name == QLatin1String("title")) {
        cur["title"] = r.readElementText(QXmlStreamReader::SkipChildElements);
      } else if (in_chart && name == QLatin1String("zipfile_location")) {
        cur["url"] = r.readElementText();
      } else if (in_chart &&
                 name == QLatin1String("zipfile_datetime_iso8601")) {
        cur["dateText"] = r.readElementText();
      } else if (!in_chart && name == QLatin1String("title") &&
                 m_catalog_title.isEmpty()) {
        m_catalog_title = r.readElementText(
            QXmlStreamReader::SkipChildElements);
      }
    } else if (tok == QXmlStreamReader::EndElement &&
               r.name() == QLatin1String("chart")) {
      in_chart = false;
      if (!cur.value("url").toString().isEmpty()) m_charts.append(cur);
    }
  }
  setStatus(r.hasError()
                ? tr("Catalog parse error: %1").arg(r.errorString())
                : tr("%1 charts in catalog").arg(m_charts.size()));
  Q_EMIT catalogChanged();
}

bool ChartDldrContext::extractZip(const QString& zipPath,
                                  const QString& destDir) {
  struct archive* a = archive_read_new();
  archive_read_support_format_zip(a);
  if (archive_read_open_filename(a, zipPath.toLocal8Bit().constData(),
                                 65536) != ARCHIVE_OK) {
    archive_read_free(a);
    return false;
  }
  struct archive_entry* entry = nullptr;
  bool ok = true;
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const QString rel =
        QString::fromUtf8(archive_entry_pathname(entry));
    // Zip-slip guard: refuse absolute paths and parent escapes.
    if (rel.startsWith('/') || rel.contains(QLatin1String(".."))) continue;
    const QString out = destDir + QDir::separator() + rel;
    if (archive_entry_filetype(entry) == AE_IFDIR) {
      QDir().mkpath(out);
      continue;
    }
    QDir().mkpath(QFileInfo(out).absolutePath());
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly)) {
      ok = false;
      break;
    }
    const void* buff = nullptr;
    size_t size = 0;
    la_int64_t offset = 0;
    while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK)
      f.write(static_cast<const char*>(buff), static_cast<qint64>(size));
  }
  archive_read_free(a);
  return ok;
}

void ChartDldrContext::downloadChart(int index) {
  if (m_busy || index < 0 || index >= m_charts.size()) return;
  const QVariantMap chart = m_charts[index].toMap();
  const QUrl url(chart.value("url").toString());
  QString dest = m_target.isLocalFile()
                     ? m_target.toLocalFile()
                     : QStandardPaths::writableLocation(
                           QStandardPaths::DownloadLocation);
  if (dest.isEmpty()) {
    setStatus(tr("Pick a target folder first"));
    return;
  }
  m_busy = true;
  m_progress = 0;
  Q_EMIT busyChanged();
  setStatus(tr("Downloading %1…").arg(chart.value("title").toString()));

  QNetworkReply* reply = m_nam->get(QNetworkRequest(url));
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this](qint64 got, qint64 total) {
            m_progress = total > 0 ? static_cast<int>(100 * got / total) : 0;
            Q_EMIT busyChanged();
          });
  connect(reply, &QNetworkReply::finished, this, [this, reply, dest, url]() {
    reply->deleteLater();
    m_busy = false;
    Q_EMIT busyChanged();
    if (reply->error() != QNetworkReply::NoError) {
      setStatus(tr("Download failed: %1").arg(reply->errorString()));
      return;
    }
    const QString zip =
        dest + QDir::separator() + QFileInfo(url.path()).fileName();
    QFile f(zip);
    if (!f.open(QIODevice::WriteOnly)) {
      setStatus(tr("Cannot write %1").arg(zip));
      return;
    }
    f.write(reply->readAll());
    f.close();
    setStatus(tr("Extracting…"));
    const bool ok = extractZip(zip, dest);
    QFile::remove(zip);
    setStatus(ok ? tr("Done — add the folder under Options > Charts > "
                      "Chart Files if it's not there yet")
                 : tr("Extract failed — the ZIP was removed"));
  });
}

bool ChartDldrPlugin::init(const ocpn::qtui::OcpnQtPluginHost& host) {
  m_ctx = new ChartDldrContext(this);
  if (host.registerSettingsPage)
    host.registerSettingsPage(
        QStringLiteral("Chart downloader"),
        QUrl(QStringLiteral("qrc:/chartdldr_plugin/Settings.qml")), m_ctx);
  return true;
}

void ChartDldrPlugin::deinit() {
  delete m_ctx;
  m_ctx = nullptr;
}
