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
 * ModelNavDataProvider -- a live NavDataProvider that reads the real model
 * (P2.11 live adapter), mirroring how the wx canvas gets nav data: the
 * comm/decoder pipeline feeds the model and the canvas READS it.
 *
 * Threading: AIS decode runs on a NavFeedWorker on its own QThread, writing
 * the thread-safe AisTargetStore + OwnShipHolder. This object lives on the
 * GUI thread; it owns the worker/thread/store/holder and, on the worker's
 * coalesced updated() signal (queued), re-emits dynamicChanged() so the
 * overlay Layers snapshot the store and re-render on the GUI thread. The
 * per-sentence CPU work never touches the GUI thread.
 *
 * Routes / tracks / waypoints are low-frequency and read straight from the
 * model managers on the GUI thread.
 */

#ifndef OCPN_QT_MODEL_NAV_DATA_PROVIDER_H_
#define OCPN_QT_MODEL_NAV_DATA_PROVIDER_H_

#include <memory>

#include "nav_data_provider.h"

QT_BEGIN_NAMESPACE
class QThread;
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class AisTargetStore;
class OwnShipHolder;
class NavFeedWorker;

class ModelNavDataProvider : public NavDataProvider {
  Q_OBJECT

public:
  // Two live sources, chosen at construction:
  //   - if `net_host` is non-empty, a real TCP NMEA-0183 CommDriver feeds the
  //     model via the comm framework (NavMsgBus -> AisDecoder + CommBridge),
  //     event-driven on the GUI thread; this provider mirrors it on a timer.
  //   - otherwise the NavFeedWorker replays `log_path` on its own thread.
  // persist_ais: back the AIS store with SQLite (live source) so targets
  // survive restarts; false uses the in-memory store (demo/replay).
  ModelNavDataProvider(const QString& log_path, const QString& net_host,
                       int net_port, bool persist_ais = false,
                       QObject* parent = nullptr);
  ~ModelNavDataProvider() override;

  QList<AisTarget> aisTargets() const override;
  QVector<AisTrackPoint> aisTrack(int mmsi, qint64 since_ms) const override;
  OwnShipState ownShip() const override;
  QList<NavRoute> routes() const override;
  QList<NavWaypoint> waypoints() const override;
  QList<NavTrack> tracks() const override;

  /** Start/stop the decode worker. */
  void setRunning(bool run);

  /** Poll the model (g_pAIS + own-ship globals) on a timer without creating
   *  any driver -- used when data is fed by user-configured connections
   *  (ConnectionsViewModel -> MakeCommDriver), #34. */
  void setModelPolling(bool on);

private:
  void onWorkerUpdated();  // replay path: publish own-ship globals + re-emit
  void pollNetwork();      // network path (GUI thread): mirror model + re-emit
  void ensureNetDriver();  // create the TCP driver + CommBridge once
  void mirrorTargets();    // g_pAIS targets -> store (+ prune)
  void emitChanges();      // dynamicChanged + staticChanged on set-size change

  std::unique_ptr<AisTargetStore> m_ais_store;
  std::unique_ptr<OwnShipHolder> m_own;

  // Replay source (used when m_net_host is empty).
  QThread* m_thread = nullptr;       // owns the worker's thread of execution
  NavFeedWorker* m_worker = nullptr; // lives on m_thread

  // Network source (used when m_net_host is non-empty).
  QString m_net_host;
  int m_net_port = 0;
  bool m_use_network = false;
  bool m_driver_made = false;
  QTimer* m_net_timer = nullptr;     // GUI-thread mirror tick
  QTimer* m_poll_timer = nullptr;    // model-poll tick for configured drivers

  int m_last_static_sig = -1;        // cheap change-detect for routes/wpts
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_MODEL_NAV_DATA_PROVIDER_H_
