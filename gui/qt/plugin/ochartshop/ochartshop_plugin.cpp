/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "ochartshop_plugin.h"

#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVariantMap>
#include <QXmlStreamReader>

namespace {
// The live shop endpoint (o-charts_pi userURL + API path, verbatim).
const char kShopUrl[] =
    "https://o-charts.org/shop/index.php"
    "?fc=module&module=occharts&controller=apioesu";

QString hexEncode(const QString& clear) {
  return QString::fromLatin1(clear.toUtf8().toHex().toUpper());
}
}  // namespace

ShopContext::ShopContext(QObject* ochartsService,
                         std::function<void(const QString&)> addChartDir,
                         QObject* parent)
    : QObject(parent),
      m_ocharts(ochartsService),
      m_add_chart_dir(std::move(addChartDir)) {
  m_nam = new QNetworkAccessManager(this);
  QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("ochartshop"));
  m_user = st.value(QStringLiteral("username")).toString();
  m_key = st.value(QStringLiteral("key")).toString();
  m_system_name = st.value(QStringLiteral("systemName")).toString();
}

QString ShopContext::versionParam() const {
  // wx sends g_systemOS + plugin version; identify ourselves honestly.
  return QSysInfo::productType() + QStringLiteral("-opencpn-qt-1.0");
}

void ShopContext::setBusy(bool b, int progress) {
  m_busy = b;
  m_progress = progress;
  Q_EMIT busyChanged();
}

void ShopContext::setStatus(const QString& s) {
  m_status = s;
  Q_EMIT statusChanged();
}

void ShopContext::post(
    const QString& params,
    std::function<void(int result, const QByteArray& xml)> done) {
  QNetworkRequest req{QUrl(QString::fromLatin1(kShopUrl))};
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/x-www-form-urlencoded"));
  QNetworkReply* reply = m_nam->post(req, params.toLatin1());
  connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      setStatus(tr("Shop request failed: %1").arg(reply->errorString()));
      done(-1, {});
      return;
    }
    const QByteArray body = reply->readAll();
    // <result> is the API status code (1 = OK).
    int result = -1;
    QXmlStreamReader r(body);
    while (!r.atEnd()) {
      if (r.readNext() == QXmlStreamReader::StartElement &&
          r.name() == QLatin1String("result")) {
        result = r.readElementText().toInt();
        break;
      }
    }
    done(result, body);
  });
}

void ShopContext::login(const QString& user, const QString& password) {
  if (m_busy) return;
  m_user = user.trimmed();
  setBusy(true);
  setStatus(tr("Logging in…"));
  const QString params = QStringLiteral("taskId=login2&username=%1&password=%2&version=%3")
                             .arg(m_user, hexEncode(password), versionParam());
  post(params, [this](int result, const QByteArray& xml) {
    setBusy(false);
    if (result != 1) {
      setStatus(result == 6   ? tr("Invalid credentials")
                : result == 4 ? tr("User does not exist")
                              : tr("Login failed (code %1)").arg(result));
      return;
    }
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
      if (r.readNext() == QXmlStreamReader::StartElement &&
          r.name() == QLatin1String("key"))
        m_key = r.readElementText();
    }
    QSettings st(QStringLiteral("OpenCPN"), QStringLiteral("ochartshop"));
    st.setValue(QStringLiteral("username"), m_user);
    st.setValue(QStringLiteral("key"), m_key);
    setStatus(tr("Logged in"));
    Q_EMIT stateChanged();
    refreshList();
  });
}

void ShopContext::logout() {
  m_key.clear();
  m_charts.clear();
  QSettings(QStringLiteral("OpenCPN"), QStringLiteral("ochartshop"))
      .remove(QStringLiteral("key"));
  Q_EMIT stateChanged();
  Q_EMIT chartsChanged();
}

void ShopContext::refreshList() {
  if (m_busy || m_key.isEmpty()) return;
  setBusy(true);
  setStatus(tr("Fetching chart list…"));
  const QString params =
      QStringLiteral("taskId=getlist&username=%1&key=%2&version=%3")
          .arg(m_user, m_key, versionParam());
  post(params, [this](int result, const QByteArray& xml) {
    setBusy(false);
    if (result != 1) {
      setStatus(tr("List failed (code %1)").arg(result));
      if (result == 6) logout();  // stale key
      return;
    }
    parseList(xml);
  });
}

void ShopContext::parseList(const QByteArray& xml) {
  m_charts.clear();
  QXmlStreamReader r(xml);
  QVariantMap cur;
  QString curSlotUuid, curAssigned, curQuantityId;
  bool inChart = false, inSlot = false;
  while (!r.atEnd()) {
    const auto tok = r.readNext();
    if (tok == QXmlStreamReader::StartElement) {
      const auto n = r.name();
      if (n == QLatin1String("systemName") && !inChart) {
        const QString sn = r.readElementText();
        if (!sn.isEmpty()) {
          m_system_name = sn;
          QSettings(QStringLiteral("OpenCPN"), QStringLiteral("ochartshop"))
              .setValue(QStringLiteral("systemName"), sn);
        }
      } else if (n == QLatin1String("chart")) {
        inChart = true;
        cur.clear();
        cur["assignedHere"] = false;
        cur["slotUuid"] = QString();
      } else if (inChart && n == QLatin1String("slot")) {
        inSlot = true;
        curSlotUuid.clear();
        curAssigned.clear();
      } else if (inChart) {
        const QString text_names[] = {
            "chartName", "chartId", "order",     "edition",
            "editionDate", "purchase", "expiration", "expired",
            "chartType"};
        bool handled = false;
        for (const QString& tn : text_names) {
          if (n == tn) {
            const QString v = r.readElementText();
            if (inSlot) break;
            cur[tn] = v;
            handled = true;
            break;
          }
        }
        if (!handled) {
          if (n == QLatin1String("quantityId")) {
            curQuantityId = r.readElementText();
            cur["quantityId"] = curQuantityId;
          } else if (inSlot && n == QLatin1String("slotUuid")) {
            curSlotUuid = r.readElementText();
          } else if (inSlot && n == QLatin1String("assignedSystemName")) {
            curAssigned = r.readElementText();
          }
        }
      }
    } else if (tok == QXmlStreamReader::EndElement) {
      if (r.name() == QLatin1String("slot")) {
        inSlot = false;
        // Remember the slot bound to THIS system (or the first free one).
        if (!curAssigned.isEmpty() && curAssigned == m_system_name) {
          cur["assignedHere"] = true;
          cur["slotUuid"] = curSlotUuid;
        } else if (curAssigned.isEmpty() &&
                   cur.value("slotUuid").toString().isEmpty()) {
          cur["freeSlotUuid"] = curSlotUuid;
        }
      } else if (r.name() == QLatin1String("chart")) {
        inChart = false;
        cur["supported"] =
            cur.value("chartType").toString().contains(
                QLatin1String("SENC"), Qt::CaseInsensitive);
        m_charts.append(cur);
      }
    }
  }
  setStatus(tr("%1 chart sets — system: %2")
                .arg(m_charts.size())
                .arg(m_system_name.isEmpty() ? tr("(not identified)")
                                             : m_system_name));
  Q_EMIT stateChanged();
  Q_EMIT chartsChanged();
}

void ShopContext::withFingerprintHex(
    std::function<void(const QString& hex, const QString& name)> done) {
  if (!m_ocharts) {
    setStatus(tr("o-charts daemon not available"));
    done({}, {});
    return;
  }
  const QString existing =
      m_ocharts->property("fingerprintFile").toString();
  auto finish = [done](const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      done({}, {});
      return;
    }
    done(QString::fromLatin1(f.readAll().toHex().toUpper()),
         QFileInfo(path).fileName());
  };
  if (!existing.isEmpty() && QFile::exists(existing)) {
    finish(existing);
    return;
  }
  setStatus(tr("Generating system fingerprint…"));
  // One-shot wait on the service's changed() signal: a throwaway watcher
  // object self-destructs on the first emission, then we read the result.
  auto* watcher = new QObject(this);
  connect(m_ocharts, SIGNAL(changed()), watcher, SLOT(deleteLater()));
  connect(watcher, &QObject::destroyed, this, [this, finish]() {
    const QString p = m_ocharts->property("fingerprintFile").toString();
    if (!p.isEmpty())
      finish(p);
    else
      setStatus(tr("Fingerprint generation failed — is oexserverd present?"));
  });
  QMetaObject::invokeMethod(m_ocharts, "generateFingerprint");
}

void ShopContext::identifySystem() {
  if (m_busy || m_key.isEmpty()) return;
  setBusy(true);
  withFingerprintHex([this](const QString& hex, const QString& name) {
    if (hex.isEmpty()) {
      setBusy(false);
      setStatus(tr("Fingerprint unavailable"));
      return;
    }
    const QString params =
        QStringLiteral(
            "taskId=identifySystem&username=%1&key=%2&xfpr=%3&xfprName=%4"
            "&version=%5")
            .arg(m_user, m_key, hex, name, versionParam());
    post(params, [this](int result, const QByteArray&) {
      setBusy(false);
      if (result != 1) {
        setStatus(tr("Identify failed (code %1)").arg(result));
        return;
      }
      setStatus(tr("System registered"));
      refreshList();
    });
  });
}

void ShopContext::installChart(int index) {
  if (m_busy || index < 0 || index >= m_charts.size()) return;
  const QVariantMap chart = m_charts[index].toMap();
  if (!chart.value("supported").toBool()) {
    setStatus(tr("Raster (oeRNC) sets are not supported yet"));
    return;
  }
  if (chart.value("assignedHere").toBool()) {
    requestDownload(chart);
    return;
  }
  if (m_system_name.isEmpty()) {
    setStatus(tr("Identify this system first"));
    return;
  }
  // Assign a slot to this system, then re-list and download.
  setBusy(true);
  setStatus(tr("Assigning to %1…").arg(m_system_name));
  const QString params =
      QStringLiteral(
          "taskId=assign&username=%1&key=%2&systemName=%3&order=%4"
          "&chartid=%5&quantityId=%6&version=%7")
          .arg(m_user, m_key, m_system_name,
               chart.value("order").toString(),
               chart.value("chartId").toString(),
               chart.value("quantityId").toString(), versionParam());
  post(params, [this, chart](int result, const QByteArray& xml) {
    if (result != 1) {
      setBusy(false);
      setStatus(result == 20 ? tr("Already assigned to this machine")
                             : tr("Assign failed (code %1)").arg(result));
      if (result == 20) refreshList();
      return;
    }
    // The assign response carries the slotUuid to use.
    QString slot;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
      if (r.readNext() == QXmlStreamReader::StartElement &&
          r.name() == QLatin1String("slotUuid"))
        slot = r.readElementText();
    }
    QVariantMap c = chart;
    c["slotUuid"] = slot;
    c["assignedHere"] = true;
    setBusy(false);
    requestDownload(c);
  });
}

void ShopContext::requestDownload(const QVariantMap& chart) {
  setBusy(true);
  setStatus(tr("Requesting %1…").arg(chart.value("chartName").toString()));
  const QString params =
      QStringLiteral(
          "taskId=request&username=%1&key=%2&assignedSystemName=%3"
          "&slotUuid=%4&requestedFile=base&requestedVersion=%5"
          "&currentVersion=&version=%6")
          .arg(m_user, m_key, m_system_name,
               chart.value("slotUuid").toString(),
               chart.value("edition").toString(), versionParam());
  post(params, [this, chart](int result, const QByteArray& xml) {
    if (result != 1) {
      setBusy(false);
      setStatus(tr("Request failed (code %1)").arg(result));
      return;
    }
    QString link, keysLink;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
      if (r.readNext() == QXmlStreamReader::StartElement) {
        if (r.name() == QLatin1String("link"))
          link = r.readElementText();
        else if (r.name() == QLatin1String("chartKeysLink"))
          keysLink = r.readElementText();
      }
    }
    if (link.isEmpty()) {
      setBusy(false);
      setStatus(tr("No download link in response"));
      return;
    }
    downloadAndInstall(link, keysLink, chart.value("chartName").toString());
  });
}

void ShopContext::downloadAndInstall(const QString& link,
                                     const QString& keysLink,
                                     const QString& chartName) {
  setStatus(tr("Downloading %1…").arg(chartName));
  QNetworkReply* reply = m_nam->get(QNetworkRequest(QUrl(link)));
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this](qint64 got, qint64 total) {
            m_progress = total > 0 ? static_cast<int>(100 * got / total) : 0;
            Q_EMIT busyChanged();
          });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, keysLink, chartName]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      setBusy(false);
      setStatus(tr("Download failed: %1").arg(reply->errorString()));
      return;
    }
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/o-charts");
    QDir().mkpath(base);
    const QString zip = base + QStringLiteral("/dl.zip");
    {
      QFile f(zip);
      if (!f.open(QIODevice::WriteOnly)) {
        setBusy(false);
        setStatus(tr("Cannot write %1").arg(zip));
        return;
      }
      f.write(reply->readAll());
    }
    setStatus(tr("Installing…"));
    QString rootDir;
    const bool ok = extractZip(zip, base, &rootDir);
    QFile::remove(zip);
    if (!ok) {
      setBusy(false);
      setStatus(tr("Extract failed"));
      return;
    }
    const QString chartDir =
        rootDir.isEmpty() ? base : base + QDir::separator() + rootDir;
    // Keys file into the chart directory (the key loader scans *.XML).
    if (keysLink.isEmpty()) {
      setBusy(false);
      if (m_add_chart_dir) m_add_chart_dir(chartDir);
      setStatus(tr("%1 installed").arg(chartName));
      return;
    }
    QNetworkReply* kr = m_nam->get(QNetworkRequest(QUrl(keysLink)));
    connect(kr, &QNetworkReply::finished, this,
            [this, kr, keysLink, chartDir, chartName]() {
      kr->deleteLater();
      if (kr->error() == QNetworkReply::NoError) {
        QString kname = QFileInfo(QUrl(keysLink).path()).fileName();
        if (kname.isEmpty()) kname = QStringLiteral("keyList.XML");
        QFile f(chartDir + QDir::separator() + kname);
        if (f.open(QIODevice::WriteOnly)) f.write(kr->readAll());
      }
      setBusy(false);
      if (m_add_chart_dir) m_add_chart_dir(chartDir);
      setStatus(tr("%1 installed and added to the chart library")
                    .arg(chartName));
    });
  });
}

bool ShopContext::extractZip(const QString& zipPath, const QString& destDir,
                             QString* rootDir) {
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
    const QString rel = QString::fromUtf8(archive_entry_pathname(entry));
    if (rel.startsWith('/') || rel.contains(QLatin1String(".."))) continue;
    if (rootDir && rootDir->isEmpty() && rel.contains('/'))
      *rootDir = rel.section('/', 0, 0);
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

bool OchartShopPlugin::init(const ocpn::qtui::OcpnQtPluginHost& host) {
  m_ctx = new ShopContext(host.ochartsService, host.addChartDirectory, this);
  if (host.registerSettingsPage)
    host.registerSettingsPage(
        QStringLiteral("o-charts shop"),
        QUrl(QStringLiteral("qrc:/ochartshop_plugin/Settings.qml")), m_ctx);
  return true;
}

void OchartShopPlugin::deinit() {
  delete m_ctx;
  m_ctx = nullptr;
}
