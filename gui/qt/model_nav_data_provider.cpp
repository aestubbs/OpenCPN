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

#include <QThread>

#include "in_memory_ais_store.h"
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"
#include "model/track.h"
#include "nav_feed_worker.h"
#include "own_ship_holder.h"

namespace ocpn::qtui {

ModelNavDataProvider::ModelNavDataProvider(const QString& log_path,
                                           QObject* parent)
    : NavDataProvider(parent),
      m_ais_store(std::make_unique<InMemoryAisTargetStore>()),
      m_own(std::make_unique<OwnShipHolder>()) {
  m_thread = new QThread(this);
  m_worker = new NavFeedWorker(m_ais_store.get(), m_own.get(), log_path);
  m_worker->moveToThread(m_thread);
  // Worker tick -> GUI-thread refresh (queued across the thread boundary).
  connect(m_worker, &NavFeedWorker::updated, this,
          &ModelNavDataProvider::onWorkerUpdated);
  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
}

ModelNavDataProvider::~ModelNavDataProvider() {
  if (m_thread) {
    QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    m_thread->quit();
    m_thread->wait();
  }
}

void ModelNavDataProvider::setRunning(bool run) {
  if (run) {
    if (!m_thread->isRunning()) m_thread->start();
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);
  } else if (m_worker) {
    QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
  }
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
  for (Track* tk : g_TrackList) {
    if (!tk) continue;
    NavTrack nt;
    const int n = tk->GetnPoints();
    for (int i = 0; i < n; ++i) {
      TrackPoint* tp = tk->GetPoint(i);
      if (tp) nt.points.append(QPointF(tp->m_lon, tp->m_lat));
    }
    if (nt.points.size() >= 2) out.append(nt);
  }
  return out;
}

void ModelNavDataProvider::onWorkerUpdated() {
  Q_EMIT dynamicChanged();

  // Cheap static change-detect: route/waypoint/track counts.
  const int sig =
      (pRouteList ? static_cast<int>(pRouteList->size()) : 0) * 73 +
      static_cast<int>(g_TrackList.size()) * 17 +
      (pWayPointMan && pWayPointMan->GetWaypointList()
           ? static_cast<int>(pWayPointMan->GetWaypointList()->size())
           : 0);
  if (sig != m_last_static_sig) {
    m_last_static_sig = sig;
    Q_EMIT staticChanged();
  }
}

}  // namespace ocpn::qtui
