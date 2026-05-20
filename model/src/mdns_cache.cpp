/**************************************************************************
 *   Copyright (C) 2024 Alec Leamas                                        *
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
 **************************************************************************/

/**
 *  \file
 *
 *  Implement mdns_cache.h -- mDNS host lookups cache.
 */

#include <algorithm>

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QTimer>
#include <QUrl>

#include "model/logger.h"
#include "model/mdns_cache.h"

/**
 * Check if we can connect to given host/port, does not
 * care if we cannot receive data.
 *
 * The pre-Qt code used curl with a 2 s timeout and treated both CURLE_OK and
 * CURLE_RECV_ERROR as "host is alive". With QNetworkAccessManager the
 * equivalent is: any reply (even an HTTP/protocol-level error) means we
 * established a connection; only transport-level failures (host unreachable,
 * timed out, connection refused) count as "down".
 */
static bool Ping(const std::string& url, int port = 8443) {
  QUrl q_url(QString::fromStdString(url));
  if (port > 0) q_url.setPort(port);

  QNetworkAccessManager nam;
  QNetworkRequest req(q_url);
  QNetworkReply* reply = nam.get(req);

  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  // 2 second timeout, matching the old CURLOPT_TIMEOUT_MS=2000.
  QTimer::singleShot(2000, &loop, &QEventLoop::quit);
  loop.exec();

  bool ok;
  if (!reply->isFinished()) {
    // Timed out -- treat as unreachable.
    reply->abort();
    ok = false;
  } else {
    const QNetworkReply::NetworkError err = reply->error();
    // Treat transport-level failures as down; any HTTP/content error from
    // a server that did respond is still "reachable".
    ok = !(err == QNetworkReply::ConnectionRefusedError ||
           err == QNetworkReply::HostNotFoundError ||
           err == QNetworkReply::TimeoutError ||
           err == QNetworkReply::NetworkSessionFailedError ||
           err == QNetworkReply::TemporaryNetworkFailureError ||
           err == QNetworkReply::UnknownNetworkError);
  }

  const QString err_str = reply->errorString();
  reply->deleteLater();
  DEBUG_LOG << "Checked mdns host: " << url << ": "
            << (ok ? "ok" : err_str.toStdString());
  return ok;
}

MdnsCache& MdnsCache::GetInstance() {
  static MdnsCache mdns_cache;
  return mdns_cache;
}

bool MdnsCache::Add(const Entry& entry) {
  std::unique_lock lock(m_mutex);
  auto found = std::find_if(m_cache.begin(), m_cache.end(),
                            [entry](Entry& e) { return e.ip == entry.ip; });
  DEBUG_LOG << "Added mdns cache entry, ip: " << entry.ip
            << ", status: " << (found == m_cache.end() ? "true" : "false");
  if (found != m_cache.end()) return false;
  m_cache.push_back(entry);
  return true;
}

bool MdnsCache::Add(const std::string& service, const std::string& host,
                    const std::string& _ip, const std::string& _port) {
  return Add(Entry(service, host, _ip, _port));
}

bool MdnsCache::Add(const std::string& _ip, const std::string& _port) {
  return Add(Entry("opencpn", "unknown", _ip, _port));
}

void MdnsCache::Validate() {
  std::unique_lock lock(m_mutex);
  for (auto it = m_cache.begin(); it != m_cache.end();) {
    if (!Ping(it->ip)) {
      m_cache.erase(it);
    } else {
      it++;
    }
  }
}
