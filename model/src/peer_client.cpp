/***************************************************************************
 *   Copyright (C) 2022 by David Register                                  *
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
 * \file
 *
 * Implement peer_client.h -- peer data sharing client non-gui abstraction
 */

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include <QByteArray>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <wx/log.h>
#include <wx/string.h>

#include "model/config_vars.h"
#include "model/ocpn_config.h"
#include "model/nav_object_database.h"
#include "model/peer_client.h"
#include "model/ocpn_utils.h"
#include "model/rest_server.h"
#include "model/semantic_vers.h"
#include "observable_confvar.h"

struct MemoryStruct {
  char* memory;
  size_t size;
  MemoryStruct() {
    memory = (char*)malloc(1);
    size = 0;
  }
  ~MemoryStruct() { free(memory); }
};

using PeerDlgPair = std::pair<PeerDlgResult, std::string>;

PeerData::PeerData(EventVar& p)
    : overwrite(false),
      activate(false),
      progress(p),
      run_status_dlg([](PeerDlg, int) { return PeerDlgResult::Cancel; }),
      run_pincode_dlg([] { return PeerDlgPair(PeerDlgResult::Cancel, ""); }) {}

/** Copy a QByteArray response into the legacy MemoryStruct buffer used by
 *  the JSON parsing helpers below. */
static void StoreReplyBody(const QByteArray& body, MemoryStruct* dest) {
  if (!dest) return;
  const size_t n = static_cast<size_t>(body.size());
  char* ptr = static_cast<char*>(realloc(dest->memory, n + 1));
  if (!ptr) {
    std::cerr << "not enough memory (realloc returned NULL)\n";
    return;
  }
  memcpy(ptr, body.constData(), n);
  ptr[n] = '\0';
  dest->memory = ptr;
  dest->size = n;
}

/** Build a QNetworkRequest with SSL peer/host verification disabled, matching
 *  the old curl CURLOPT_SSL_VERIFYPEER=0 / CURLOPT_SSL_VERIFYHOST=0. */
static QNetworkRequest MakeRequest(const std::string& url) {
  QNetworkRequest req(QUrl(QString::fromStdString(url)));
  QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
  ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
  req.setSslConfiguration(ssl);
  // Identity encoding (no gzip); QNAM does not advertise gzip unless asked,
  // but be explicit to match the old curl "identity" encoding.
  req.setRawHeader("Accept-Encoding", "identity");
  return req;
}

/** Spin a local event loop until the reply finishes or the timeout fires.
 *  Returns true if the reply finished naturally, false on timeout. */
static bool WaitForReply(QNetworkReply* reply, int timeout_ms) {
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  bool timed_out = false;
  if (timeout_ms > 0) {
    QTimer::singleShot(timeout_ms, &loop, [&loop, &timed_out]() {
      timed_out = true;
      loop.quit();
    });
  }
  loop.exec();
  if (timed_out && !reply->isFinished()) {
    reply->abort();
    return false;
  }
  return true;
}

/** Translate a QNetworkReply outcome to the old curl-style return value:
 *  positive HTTP status, or a negative sentinel on transport error. */
static long ReplyToHttpStatus(QNetworkReply* reply) {
  const QVariant status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
  if (status.isValid()) {
    bool ok = false;
    long http = status.toInt(&ok);
    if (ok && http > 0) return http;
  }
  const QNetworkReply::NetworkError err = reply->error();
  // Map any non-OK transport error to a negative sentinel (the magnitude no
  // longer matches CURLcode but no caller inspects it beyond "non-200").
  long code = err == QNetworkReply::NoError ? 1 : static_cast<long>(err);
  return -code;
}

/**
 *  Perform a POST operation on server, store possible reply in response.
 *  @return positive http status or negative sentinel on network error
 */
static long ApiPost(const std::string& url, const std::string& body,
                    PeerData& peer_data, MemoryStruct* response) {
  peer_data.progress.Notify(0, "");

  QNetworkAccessManager nam;
  QNetworkRequest req = MakeRequest(url);
  // The peer server expects the legacy curl-style POST: no explicit
  // Content-Type header (curl did not set one when only CURLOPT_COPYPOSTFIELDS
  // was used) -- leave the header unset to match.
  const QByteArray data(body.data(), static_cast<int>(body.size()));
  QNetworkReply* reply = nam.post(req, data);

  // Wire upload progress to the existing PeerData progress callback.
  QObject::connect(reply, &QNetworkReply::uploadProgress,
                   [&peer_data](qint64 sent, qint64 total) {
                     if (total <= 0) {
                       peer_data.progress.Notify(0, "");
                     } else {
                       peer_data.progress.Notify(
                           static_cast<int>(100 * sent / total), "");
                     }
                   });

  const bool finished = WaitForReply(reply, 20000);  // 20 s, matches old code.
  peer_data.progress.Notify(0, "");

  long http_status;
  if (!finished) {
    http_status = -static_cast<long>(QNetworkReply::TimeoutError);
  } else {
    if (reply->error() == QNetworkReply::NoError ||
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
      StoreReplyBody(reply->readAll(), response);
    }
    http_status = ReplyToHttpStatus(reply);
  }
  reply->deleteLater();
  return http_status;
}

/**
 * Perform a GET operation on server, store possible reply in chunk.
 * @return positive http status or negative sentinel on network error
 */
static int ApiGet(const std::string& url, const MemoryStruct* chunk,
                  int timeout = 0) {
  QNetworkAccessManager nam;
  QNetworkRequest req = MakeRequest(url);
  QNetworkReply* reply = nam.get(req);

  // Timeout in seconds (matching old curl API); 0 means "no timeout".
  const int timeout_ms = timeout > 0 ? timeout * 1000 : 0;
  const bool finished = WaitForReply(reply, timeout_ms);

  long http_status;
  if (!finished) {
    http_status = -static_cast<long>(QNetworkReply::TimeoutError);
  } else {
    // chunk is logically an output parameter but callers pass a const pointer
    // (legacy curl idiom). Cast away const, as the old WriteMemoryCallback did.
    if (reply->error() == QNetworkReply::NoError ||
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
      StoreReplyBody(reply->readAll(), const_cast<MemoryStruct*>(chunk));
    }
    http_status = ReplyToHttpStatus(reply);
  }
  reply->deleteLater();
  return static_cast<int>(http_status);
}

static std::string GetClientKey(std::string& server_name) {
  ConfigVar<std::string> server_keys("/Settings/RESTClient", "ServerKeys",
                                     TheBaseConfig());
  auto key_string = server_keys.Get("");
  auto entries = ocpn::split(key_string.c_str(), ";");
  for (const auto& entry : entries) {
    auto server_key = ocpn::split(entry.c_str(), ":");
    if (server_key.size() != 2) continue;
    if (server_key[0] == server_name) return server_key[1];
  }
  return "1";
}

static void SaveClientKey(std::string& server_name, std::string key) {
  ConfigVar<std::string> server_keys("/Settings/RESTClient", "ServerKeys",
                                     TheBaseConfig());
  auto config_server_keys = server_keys.Get("");

  auto server_keys_list = ocpn::split(config_server_keys.c_str(), ";");
  std::unordered_map<std::string, std::string> key_by_server;
  for (const auto& item : server_keys_list) {
    auto server_and_key = ocpn::split(item.c_str(), ":");
    if (server_and_key.size() != 2) continue;
    key_by_server[server_and_key[0]] = server_and_key[1];
  }
  key_by_server[server_name] = key;

  config_server_keys = "";
  for (const auto& it : key_by_server) {
    config_server_keys += it.first + ":" + it.second + ";";
  }
  server_keys.Set(config_server_keys);
  wxLog::FlushActive();
}
static RestServerResult ParseServerJson(const MemoryStruct& reply,
                                        PeerData& peer_data) {
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(
      QByteArray(reply.memory, static_cast<int>(reply.size)), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    if (err.error != QJsonParseError::NoError) {
      wxLogMessage("Json server reply parse error: %s",
                   err.errorString().toStdString().c_str());
    }
    peer_data.run_status_dlg(PeerDlg::JsonParseError, 1);
    peer_data.api_version = SemanticVersion(-1, -1);
    return RestServerResult::Void;
  }
  const QJsonObject root = doc.object();
  if (root.contains("version")) {
    auto s = root.value("version").toString().toStdString();
    peer_data.api_version = SemanticVersion::parse(s);
  }
  if (root.contains("result")) {
    return static_cast<RestServerResult>(root.value("result").toInt());
  } else {
    return RestServerResult::Void;
  }
}

bool CheckKey(const std::string& key, PeerData peer_data) {
  std::stringstream url;
  url << "https://" << peer_data.dest_ip_address << "/api/ping"
      << "?source=" << g_hostname << "&apikey=" << key;
  MemoryStruct reply;
  long status = ApiGet(url.str(), &reply, 5);
  if (status != 200) {
    peer_data.run_status_dlg(PeerDlg::InvalidHttpResponse, status);
    return false;
  }
  auto result = ParseServerJson(reply, peer_data);
  return result != RestServerResult::NewPinRequested;
}

void GetApiVersion(PeerData& peer_data) {
  if (peer_data.api_version > SemanticVersion(5, 0)) return;
  std::stringstream url;
  url << "https://" << peer_data.dest_ip_address << "/api/get-version";

  struct MemoryStruct chunk;
  std::string buf;
  long response_code = ApiGet(url.str(), &chunk, 2);

  if (response_code == 200) {
    ParseServerJson(chunk, peer_data);
  } else {
    // Return "old" version without /api/writable support
    peer_data.api_version = SemanticVersion(5, 8);
  }
}

/** Return a usable api key, possibly after user dialogs. */
static bool GetApiKey(PeerData& peer_data, std::string& key) {
  std::string api_key;
  if (peer_data.api_version == SemanticVersion(0, 0)) GetApiVersion(peer_data);

  while (true) {
    api_key = GetClientKey(peer_data.server_name);
    if (api_key.size() < 9 && peer_data.api_version >= SemanticVersion(5, 9))
      api_key = "0123456789abc";  // Long enough for being seen as 5.9+
    std::stringstream url;
    url << "https://" << peer_data.dest_ip_address << "/api/ping"
        << "?source=" << g_hostname << "&apikey=" << api_key;
    MemoryStruct chunk;
    int status = ApiGet(url.str(), &chunk, 3);
    if (status != 200) {
      auto r = peer_data.run_status_dlg(PeerDlg::InvalidHttpResponse, status);
      if (r == PeerDlgResult::Ok) continue;
      return false;
    }
    auto result = ParseServerJson(chunk, peer_data);
    switch (result) {
      case RestServerResult::NewPinRequested: {
        auto pin_result = peer_data.run_pincode_dlg();
        if (pin_result.first == PeerDlgResult::HasPincode) {
          std::string tentative_pin = ocpn::trim(pin_result.second);
          unsigned int_pin = atoi(tentative_pin.c_str());
          Pincode pincode(int_pin);
          api_key = pincode.Hash();
          GetApiVersion(peer_data);
          if (peer_data.api_version < SemanticVersion(5, 9)) {
            api_key = pincode.CompatHash();
          }
          if (!CheckKey(api_key, peer_data)) {
            auto r = peer_data.run_status_dlg(PeerDlg::BadPincode, 0);
            if (r == PeerDlgResult::Ok) continue;
            return false;
          }
          SaveClientKey(peer_data.server_name, api_key);
        } else if (pin_result.first == PeerDlgResult::Cancel) {
          return false;
        } else {
          auto r = peer_data.run_status_dlg(PeerDlg::ErrorReturn,
                                            static_cast<int>(result));
          if (r == PeerDlgResult::Ok) continue;
          return false;
        }
      } break;
      case RestServerResult::GenericError:
        // 5.8 returns GenericError for a valid key (!)
        [[fallthrough]];
      case RestServerResult::NoError:
        break;
      default:
        auto r = peer_data.run_status_dlg(PeerDlg::ErrorReturn,
                                          static_cast<int>(result));
        if (r == PeerDlgResult::Ok) continue;
        return false;
    }
    break;
  }
  key = api_key;
  return true;
}

/** Convert PeerData routes, tracks and waypoints to GPX XML format. */
static std::string PeerDataToXml(PeerData& peer_data) {
  NavObjectCollection1 gpx;
  std::ostringstream stream;
  int total = peer_data.routes.size() + peer_data.tracks.size() +
              peer_data.routepoints.size();
  int gpxgen = 0;
  for (auto r : peer_data.routes) {
    gpxgen++;
    gpx.AddGPXRoute(r);
    peer_data.progress.Notify(100 * gpxgen / total, "");
    wxYield();
  }
  for (auto r : peer_data.routepoints) {
    gpxgen++;
    gpx.AddGPXWaypoint(r);
    peer_data.progress.Notify(100 * gpxgen / total, "");
    wxYield();
  }
  for (auto r : peer_data.tracks) {
    gpxgen++;
    gpx.AddGPXTrack(r);
    peer_data.progress.Notify(100 * gpxgen / total, "");
    wxYield();
  }
  gpx.save(stream, PUGIXML_TEXT(" "));
  return stream.str();
}

/** Actually transfer body. */
static void SendObjects(std::string& body, const std::string& api_key,
                        PeerData& peer_data) {
  bool cancel = false;
  while (!cancel) {
    std::stringstream url;
    url << "https://" << peer_data.dest_ip_address << "/api/rx_object"
        << "?source=" << g_hostname << "&apikey=" << api_key;
    if (peer_data.overwrite) url << "&force=1";
    if (peer_data.activate) url << "&activate=1";

    struct MemoryStruct chunk;
    long response_code = ApiPost(url.str(), body, peer_data, &chunk);
    if (response_code == 200) {
      QJsonParseError perr;
      const QJsonDocument doc = QJsonDocument::fromJson(
          QByteArray(chunk.memory, static_cast<int>(chunk.size)), &perr);
      if (perr.error != QJsonParseError::NoError)
        wxLogDebug("SendObjects, parse error: %s",
                   perr.errorString().toStdString().c_str());
      const QJsonObject root = doc.object();
      // Capture the result
      int result = root.value("result").toInt();
      if (result > 0) {
        peer_data.run_status_dlg(PeerDlg::ErrorReturn, result);
      } else {
        peer_data.run_status_dlg(PeerDlg::TransferOk, 0);
      }
      cancel = true;
    } else {
      peer_data.run_status_dlg(PeerDlg::InvalidHttpResponse, response_code);
      cancel = true;
    }
  }
}

/** Parse json message in chunk, return "result" from server. */
static int CheckChunk(struct MemoryStruct& chunk, const std::string& guid) {
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(
      QByteArray(chunk.memory, static_cast<int>(chunk.size)), &perr);
  if (perr.error != QJsonParseError::NoError)
    wxLogDebug("CheckChunk: parse error: %s",
               perr.errorString().toStdString().c_str());
  const QJsonObject root = doc.object();
  int result = root.value("result").toInt();
  if (result != 0) {
    wxLogDebug("Server rejected guid %s, status: %d", guid.c_str(), result);
    return result;
  }
  return 0;
}

/** Return true if server accepts overwriting all peer_data objects. */
static bool CheckObjects(const std::string& api_key, PeerData& peer_data) {
  std::stringstream url;
  url << "https://" << peer_data.dest_ip_address << "/api/writable"
      << "?source=" << g_hostname << "&apikey=" << api_key << "&guid=";
  for (const auto& r : peer_data.routes) {
    std::string guid = r->GetGUID().toStdString();
    std::string full_url = url.str() + guid;
    struct MemoryStruct chunk;
    if (ApiGet(full_url, &chunk) != 200) {
      wxLogMessage("Cannot check /api/writable for route %s", guid.c_str());
      return false;
    }
    int result = CheckChunk(chunk, guid);
    if (result != 0) return false;
  }
  for (const auto& t : peer_data.tracks) {
    std::string guid = t->m_GUID.toStdString();
    std::string full_url = url.str() + guid;
    struct MemoryStruct chunk;
    if (ApiGet(full_url, &chunk) != 200) {
      wxLogMessage("Cannot check /api/writable for track %s", guid.c_str());
      return false;
    }
    int result = CheckChunk(chunk, guid);
    if (result != 0) return false;
  }
  for (const auto& rp : peer_data.routepoints) {
    std::string guid = rp->m_GUID.toStdString();
    std::string full_url = url.str() + guid;
    struct MemoryStruct chunk;
    if (ApiGet(full_url, &chunk) != 200) {
      wxLogMessage("Cannot check /api/writable for waypoint %s", guid.c_str());
      return false;
    }
    int result = CheckChunk(chunk, guid);
    if (result != 0) return false;
  }
  return true;
}

bool SendNavobjects(PeerData& peer_data) {
  if (peer_data.routes.empty() && peer_data.routepoints.empty() &&
      peer_data.tracks.empty()) {
    return true;
  }
  std::string api_key;
  bool apikey_ok = GetApiKey(peer_data, api_key);
  if (!apikey_ok) return false;
  if (peer_data.api_version < SemanticVersion(5, 9) && peer_data.activate) {
    peer_data.run_status_dlg(PeerDlg::ActivateUnsupported, 0);
    return false;
  }
  std::string body = PeerDataToXml(peer_data);
  SendObjects(body, api_key, peer_data);
  return true;
}

bool CheckNavObjects(PeerData& peer_data) {
  if (peer_data.routes.empty() && peer_data.routepoints.empty() &&
      peer_data.tracks.empty()) {
    return true;  // the server will not object to null transfers.
  }
  std::string apikey;
  bool apikey_ok = GetApiKey(peer_data, apikey);
  if (!apikey_ok) return false;
  return CheckObjects(apikey, peer_data);
}
