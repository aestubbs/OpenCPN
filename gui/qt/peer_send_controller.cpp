/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "peer_send_controller.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include "model/mdns_cache.h"
#include "model/mdns_query.h"
#include "model/peer_client.h"
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"  // pRouteList, pWayPointMan
#include "model/track.h"     // g_TrackList

namespace ocpn::qtui {

PeerSendController::PeerSendController(QObject* parent) : QObject(parent) {
  refreshPeers();  // whatever an earlier scan already cached
}

void PeerSendController::refreshPeers() {
  m_peers.clear();
  for (const MdnsCache::Entry& e : MdnsCache::GetInstance().GetCache()) {
    QVariantMap row;
    row["name"] = QString::fromStdString(e.hostname);
    row["ip"] = QString::fromStdString(e.ip);
    row["port"] = QString::fromStdString(e.port);
    m_peers.append(row);
  }
  Q_EMIT peersChanged();
}

void PeerSendController::scan() {
  if (m_scanning) return;
  m_scanning = true;
  Q_EMIT scanningChanged();
  // FindAllOCPNServers blocks for the timeout; keep the UI live by running
  // it on a throwaway thread, then refresh the list back on this thread.
  QThread* t = QThread::create([] { FindAllOCPNServers(2); });
  connect(t, &QThread::finished, this, [this, t]() {
    t->deleteLater();
    m_scanning = false;
    Q_EMIT scanningChanged();
    refreshPeers();
  });
  t->start();
}

void PeerSendController::providePin(const QString& pin) {
  m_pin = pin;
  m_pin_ok = true;
  if (m_pin_loop) m_pin_loop->quit();
}

void PeerSendController::cancelPin() {
  m_pin_ok = false;
  if (m_pin_loop) m_pin_loop->quit();
}

bool PeerSendController::sendObjects(const QString& ip, bool activate,
                                     void* route, void* mark, void* track) {
  if (m_sending) return false;
  m_sending = true;
  Q_EMIT sendingChanged();
  m_status.clear();
  Q_EMIT statusChanged();

  PeerData peer(m_progress);
  peer.dest_ip_address = ip.toStdString();
  // server_name keys the saved API key; the hostname half of "host:port".
  peer.server_name = ip.section(':', 0, 0).toStdString();
  peer.overwrite = false;
  peer.activate = activate;
  if (route) peer.routes.push_back(static_cast<Route*>(route));
  if (mark) peer.routepoints.push_back(static_cast<RoutePoint*>(mark));
  if (track) peer.tracks.push_back(static_cast<Track*>(track));

  // Synchronous PIN prompt: ask QML, then spin until it answers.
  peer.run_pincode_dlg = [this]() -> std::pair<PeerDlgResult, std::string> {
    m_pin_ok = false;
    Q_EMIT pinRequested();
    QEventLoop loop;
    m_pin_loop = &loop;
    loop.exec();
    m_pin_loop = nullptr;
    if (m_pin_ok)
      return {PeerDlgResult::HasPincode, m_pin.toStdString()};
    return {PeerDlgResult::Cancel, ""};
  };
  // Status reports: surface the message; never auto-retry (the model
  // retries while we return Ok -- the user can just press Send again).
  peer.run_status_dlg = [this](PeerDlg dlg, int code) -> PeerDlgResult {
    switch (dlg) {
      case PeerDlg::TransferOk:
        m_status = tr("Sent ✓");
        break;
      case PeerDlg::BadPincode:
        m_status = tr("Wrong PIN — not accepted by the peer");
        break;
      case PeerDlg::InvalidHttpResponse:
        m_status = tr("Peer not responding (HTTP %1)").arg(code);
        break;
      case PeerDlg::ActivateUnsupported:
        m_status = tr("Peer is too old to activate the route");
        break;
      default:
        m_status = tr("Transfer failed (code %1)").arg(code);
        break;
    }
    Q_EMIT statusChanged();
    return PeerDlgResult::Cancel;
  };

  const bool ok = SendNavobjects(peer);
  if (ok && m_status.isEmpty()) m_status = tr("Sent ✓");
  if (!ok && m_status.isEmpty()) m_status = tr("Transfer failed");
  m_sending = false;
  Q_EMIT sendingChanged();
  Q_EMIT statusChanged();
  return ok;
}

bool PeerSendController::sendRoute(int routeIndex, const QString& ip,
                                   bool activate) {
  if (!pRouteList || routeIndex < 0 ||
      routeIndex >= static_cast<int>(pRouteList->size()))
    return false;
  Route* r = (*pRouteList)[routeIndex];
  if (!r) return false;
  return sendObjects(ip, activate, r, nullptr, nullptr);
}

bool PeerSendController::sendMark(const QString& guid, const QString& ip) {
  RoutePoint* wp =
      pWayPointMan ? pWayPointMan->FindRoutePointByGUID(guid) : nullptr;
  if (!wp) return false;
  return sendObjects(ip, false, nullptr, wp, nullptr);
}

bool PeerSendController::sendTrack(const QString& guid, const QString& ip) {
  for (Track* t : g_TrackList)
    if (t && t->m_GUID == guid)
      return sendObjects(ip, false, nullptr, nullptr, t);
  return false;
}

}  // namespace ocpn::qtui
