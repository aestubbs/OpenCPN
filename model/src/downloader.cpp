/***************************************************************************
 *   Copyright (C) 2019 Alec Leamas                                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

/**
 * \file
 *
 * Implement downloader.h -- handle downloading of files from remote urls.
 */

#include <fstream>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QStandardPaths>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <wx/log.h>

#include "config.h"
#include "model/downloader.h"
#include "model/ocpn_utils.h"

static std::string GetUserAgent() {
  std::string ua = "Mozilla/5.0 (@abi@; @abi_version@) OpenCPN/@o_version@";
  ua += " Qt/@qt_version@";
  ocpn::replace(ua, "@o_version@", VERSION_FULL);
  ocpn::replace(ua, "@abi@", PKG_TARGET);
  ocpn::replace(ua, "@abi_version@", PKG_TARGET_VERSION);
  ocpn::replace(ua, "@qt_version@", QT_VERSION_STR);
  return ua;
}

/** Build a QNetworkRequest with the User-Agent and SSL settings the old
 *  curl-based code used (peer verification disabled, follow redirects). */
static QNetworkRequest MakeRequest(const std::string& url) {
  QNetworkRequest req(QUrl(QString::fromStdString(url)));
  req.setHeader(QNetworkRequest::UserAgentHeader,
                QString::fromStdString(GetUserAgent()));
  // Mirror CURLOPT_SSL_VERIFYPEER=0 -- "FIXME: add correct certificates".
  QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
  ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
  req.setSslConfiguration(ssl);
  // Mirror CURLOPT_FOLLOWLOCATION=1.
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  return req;
}

/** Run a synchronous QNetworkReply by spinning a local QEventLoop on
 *  the reply's finished signal. The caller owns the returned reply and
 *  must deleteLater() it. */
static void WaitForReply(QNetworkReply* reply) {
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();
}

Downloader::Downloader(std::string url_)
    : url(url_), stream(), error_msg(""), errorcode(0) {};

int Downloader::last_errorcode() { return errorcode; }

std::string Downloader::last_error() { return error_msg; }

void Downloader::on_chunk(const char* buff, unsigned bytes) {
  stream->write(buff, bytes);
}

bool Downloader::download(std::ostream* out_stream) {
  this->stream = out_stream;

  QNetworkAccessManager nam;
  QNetworkRequest req = MakeRequest(url);
  QNetworkReply* reply = nam.get(req);
  WaitForReply(reply);

  const QNetworkReply::NetworkError net_err = reply->error();
  const int http_status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  // CURLOPT_FAILONERROR caused curl to fail on HTTP >= 400. Mirror that.
  if (net_err != QNetworkReply::NoError || http_status >= 400) {
    const std::string err_str = reply->errorString().toStdString();
    wxLogWarning("Failed to get '%s' [%s]\n", url, err_str);
    errorcode = static_cast<int>(net_err);
    error_msg = err_str;
    reply->deleteLater();
    return false;
  }

  const QByteArray body = reply->readAll();
  if (!body.isEmpty()) {
    on_chunk(body.constData(), static_cast<unsigned>(body.size()));
  }
  reply->deleteLater();
  return true;
}

bool Downloader::download(std::string& path) {
  if (path == "") {
    path =
        (QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
         QDir::separator() + "ocpn_dl_" +
         QString::number(QCoreApplication::applicationPid()) + "_" +
         QString::number(QDateTime::currentMSecsSinceEpoch()))
            .toStdString();
  }
  std::ofstream out_stream;
  out_stream.open(path.c_str(),
                  std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out_stream.is_open()) {
    // Generic non-zero error code; callers only inspect last_error() text.
    errorcode = -1;
    error_msg = std::string("Cannot open temporary file ") + path;
    return false;
  }
  bool ok = download(&out_stream);
  out_stream.close();
  return ok;
}

long Downloader::get_filesize() {
  QNetworkAccessManager nam;
  QNetworkRequest req = MakeRequest(url);
  QNetworkReply* reply = nam.head(req);
  WaitForReply(reply);

  long filesize = 0;
  const QNetworkReply::NetworkError net_err = reply->error();
  if (net_err == QNetworkReply::NoError) {
    const QVariant cl =
        reply->header(QNetworkRequest::ContentLengthHeader);
    if (cl.isValid()) filesize = cl.toLongLong();
  } else {
    errorcode = static_cast<int>(net_err);
    error_msg = reply->errorString().toStdString();
  }
  reply->deleteLater();
  wxLogMessage("filesize %s: %d bytes\n", url.c_str(), (int)filesize);
  return filesize;
}
