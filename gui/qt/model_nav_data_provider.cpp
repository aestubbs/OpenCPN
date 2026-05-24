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
 * Implement model_nav_data_provider.h.
 */

#include "model_nav_data_provider.h"

#include <QDateTime>
#include <QThread>
#include <QTimer>

#include "in_memory_ais_store.h"
#include "model/ais_decoder.h"
#include "model/ais_target_data.h"
#include "model/comm_bridge.h"
#include "model/comm_drv_factory.h"
#include "model/conn_params.h"
#include "model/own_ship.h"
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"
#include "model/track.h"
#include "nav_feed_worker.h"
#include "own_ship_holder.h"
#include "wx/string.h"

namespace ocpn::qtui {

namespace {
constexpr qint64 kStaleMs = 10 * 60 * 1000;  // drop targets unseen 10 min
inline bool finitePos(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90.0 &&
         std::abs(lon) <= 360.0 && !(lat == 0.0 && lon == 0.0);
}
}  // namespace

ModelNavDataProvider::ModelNavDataProvider(const QString& log_path,
                                           const QString& net_host,
                                           int net_port, QObject* parent)
    : NavDataProvider(parent),
      m_ais_store(std::make_unique<InMemoryAisTargetStore>()),
      m_own(std::make_unique<OwnShipHolder>()),
      m_net_host(net_host),
      m_net_port(net_port),
      m_use_network(!net_host.isEmpty()) {
  if (m_use_network) {
    // Real TCP feed: the comm framework decodes on the GUI thread (async
    // QTcpSocket); we just mirror the model into the store on a timer.
    m_net_timer = new QTimer(this);
    m_net_timer->setInterval(250);  // 4 Hz mirror
    connect(m_net_timer, &QTimer::timeout, this,
            &ModelNavDataProvider::pollNetwork);
  } else {
    // Log-replay source: decode on a worker thread.
    m_thread = new QThread(this);
    m_worker = new NavFeedWorker(m_ais_store.get(), m_own.get(), log_path);
    m_worker->moveToThread(m_thread);
    connect(m_worker, &NavFeedWorker::updated, this,
            &ModelNavDataProvider::onWorkerUpdated);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
  }
}

ModelNavDataProvider::~ModelNavDataProvider() {
  if (m_thread) {
    QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    m_thread->quit();
    m_thread->wait();
  }
}

void ModelNavDataProvider::ensureNetDriver() {
  if (m_driver_made) return;
  m_driver_made = true;
  // CommBridge subscribes to NavMsgBus and updates the own-ship globals from
  // position sentences; AisDecoder's own listeners decode VDM into targets.
  CommBridge::GetInstance();
  ConnectionParams params;
  params.Type = NETWORK;
  params.NetProtocol = TCP;
  params.NetworkAddress = wxString(m_net_host.toUtf8().constData());
  params.NetworkPort = m_net_port;
  params.Protocol = PROTO_NMEA0183;
  params.IOSelect = DS_TYPE_INPUT;
  params.bEnabled = true;
  MakeCommDriver(&params);  // creates + registers + starts (async, retries)
}

void ModelNavDataProvider::setRunning(bool run) {
  if (m_use_network) {
    if (run) {
      ensureNetDriver();
      m_net_timer->start();
    } else {
      m_net_timer->stop();
    }
    return;
  }
  if (run) {
    if (!m_thread->isRunning()) m_thread->start();
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);
  } else if (m_worker) {
    QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
  }
}

void ModelNavDataProvider::mirrorTargets() {
  if (!g_pAIS) return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  for (const auto& [mmsi, td] : g_pAIS->GetTargetList()) {
    if (!td || td->b_lost || td->b_removed) continue;
    if (!finitePos(td->Lat, td->Lon)) continue;
    AisTarget t;
    t.mmsi = td->MMSI;
    t.lat = td->Lat;
    t.lon = td->Lon;
    t.cog = std::isfinite(td->COG) ? td->COG : 0.0;
    t.sog = std::isfinite(td->SOG) ? td->SOG : 0.0;
    t.hdg = td->HDG;
    t.name = td->GetFullName().trimmed();
    m_ais_store->upsert(t, now);
  }
  m_ais_store->prune(now, kStaleMs);
}

void ModelNavDataProvider::pollNetwork() {
  // GUI thread: the framework already decoded into g_pAIS + the own-ship
  // globals (CommBridge). Mirror targets into the store and snapshot own ship.
  mirrorTargets();
  OwnShipState s;
  if (finitePos(gLat, gLon)) {
    s.valid = true;
    s.lat = gLat;
    s.lon = gLon;
    s.cog = std::isfinite(gCog) ? gCog : 0.0;
    s.sog = std::isfinite(gSog) ? gSog : 0.0;
    s.hdg = std::isfinite(gHdt) ? gHdt : kHeadingUnavailable;
  }
  m_own->set(s);
  emitChanges();
}

QList<AisTarget> ModelNavDataProvider::aisTargets() const {
  return m_ais_store->snapshot();  // thread-safe read
}

OwnShipState ModelNavDataProvider::ownShip() const {
  return m_own->get();  // thread-safe read
}

QList<NavRoute> ModelNavDataProvider::routes() const {
  QList<NavRoute> out;
  if (!pRouteList) return out;
  for (Route* r : *pRouteList) {
    if (!r || !r->pRoutePointList) continue;
    NavRoute nr;
    nr.name = r->GetName();
    for (RoutePoint* wp : *r->pRoutePointList)
      if (wp) nr.points.append(QPointF(wp->m_lon, wp->m_lat));
    if (!nr.points.isEmpty()) out.append(nr);
  }
  return out;
}

QList<NavWaypoint> ModelNavDataProvider::waypoints() const {
  QList<NavWaypoint> out;
  if (!pWayPointMan) return out;
  const RoutePointList* list = pWayPointMan->GetWaypointList();
  if (!list) return out;
  for (RoutePoint* wp : *list) {
    if (!wp || wp->m_bIsInRoute) continue;  // route points draw via routes()
    NavWaypoint nw;
    nw.name = wp->GetName();
    nw.lat = wp->m_lat;
    nw.lon = wp->m_lon;
    out.append(nw);
  }
  return out;
}

QList<NavTrack> ModelNavDataProvider::tracks() const {
  QList<NavTrack> out;
  const auto appendTrack = [&](Track* tk, const QColor& color) {
    if (!tk) return;
    NavTrack nt;
    nt.color = color;
    const int n = tk->GetnPoints();
    for (int i = 0; i < n; ++i) {
      TrackPoint* tp = tk->GetPoint(i);
      if (tp) nt.points.append(QPointF(tp->m_lon, tp->m_lat));
    }
    if (nt.points.size() >= 2) out.append(nt);
  };
  for (Track* tk : g_TrackList) appendTrack(tk, QColor(60, 60, 60));
  // The live own-ship track being recorded (P2.11), distinct colour.
  if (g_pActiveTrack && g_pActiveTrack->IsRunning())
    appendTrack(g_pActiveTrack, QColor(200, 0, 0));
  return out;
}

void ModelNavDataProvider::onWorkerUpdated() {
  // Replay path: the worker (other thread) wrote the holder; publish the
  // own-ship fix to the model globals here on the GUI thread, so consumers
  // that read them directly (ActiveTrack's recorder timer) see a value
  // written from a single thread. (Network path: CommBridge already sets the
  // globals on the GUI thread.)
  const OwnShipState s = m_own->get();
  if (s.valid) {
    gLat = s.lat;
    gLon = s.lon;
    gCog = s.cog;
    gSog = s.sog;
  }
  emitChanges();
}

void ModelNavDataProvider::emitChanges() {
  Q_EMIT dynamicChanged();

  // Cheap static change-detect: route/waypoint/track counts (+ the live
  // own-ship track length, so it re-renders as it grows a point at a time).
  const int sig =
      (pRouteList ? static_cast<int>(pRouteList->size()) : 0) * 73 +
      static_cast<int>(g_TrackList.size()) * 17 +
      (pWayPointMan && pWayPointMan->GetWaypointList()
           ? static_cast<int>(pWayPointMan->GetWaypointList()->size())
           : 0) +
      (g_pActiveTrack ? g_pActiveTrack->GetnPoints() : 0);
  if (sig != m_last_static_sig) {
    m_last_static_sig = sig;
    Q_EMIT staticChanged();
  }
}

}  // namespace ocpn::qtui
