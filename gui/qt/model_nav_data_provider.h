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
QT_END_NAMESPACE

namespace ocpn::qtui {

class AisTargetStore;
class OwnShipHolder;
class NavFeedWorker;

class ModelNavDataProvider : public NavDataProvider {
  Q_OBJECT

public:
  // `log_path` is the NMEA log the worker replays (stand-in for a live
  // CommDriver, which would plug in at the same point later).
  explicit ModelNavDataProvider(const QString& log_path,
                                QObject* parent = nullptr);
  ~ModelNavDataProvider() override;

  QList<AisTarget> aisTargets() const override;
  OwnShipState ownShip() const override;
  QList<NavRoute> routes() const override;
  QList<NavWaypoint> waypoints() const override;
  QList<NavTrack> tracks() const override;

  /** Start/stop the decode worker. */
  void setRunning(bool run);

private:
  void onWorkerUpdated();  // GUI thread: re-emit dynamic/static changed

  std::unique_ptr<AisTargetStore> m_ais_store;
  std::unique_ptr<OwnShipHolder> m_own;
  QThread* m_thread = nullptr;       // owns the worker's thread of execution
  NavFeedWorker* m_worker = nullptr; // lives on m_thread
  int m_last_static_sig = -1;        // cheap change-detect for routes/wpts
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_MODEL_NAV_DATA_PROVIDER_H_
