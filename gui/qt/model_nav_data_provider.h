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
 * (P2.11 live adapter). This mirrors how the wx chart canvas gets its nav
 * data: the comm/decoder pipeline feeds the model singletons, and the canvas
 * READS them -- `g_pAIS->GetTargetList()`, the own-ship globals
 * (gLat/gLon/gCog/gSog), `pRouteList`, `pWayPointMan`, `g_TrackList`. This
 * adapter just snapshots those into the Qt value types the overlay Layers
 * consume, so the renderer is identical for demo and live data.
 *
 * It polls on a timer (the model is mutated by the comm pipeline on its own
 * threads/events) and emits dynamicChanged() each tick for AIS/own-ship,
 * staticChanged() when the route/track/waypoint set changes.
 */

#ifndef OCPN_QT_MODEL_NAV_DATA_PROVIDER_H_
#define OCPN_QT_MODEL_NAV_DATA_PROVIDER_H_

#include "nav_data_provider.h"

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class ModelNavDataProvider : public NavDataProvider {
  Q_OBJECT

public:
  explicit ModelNavDataProvider(QObject* parent = nullptr);

  QList<AisTarget> aisTargets() const override;
  OwnShipState ownShip() const override;
  QList<NavRoute> routes() const override;
  QList<NavWaypoint> waypoints() const override;
  QList<NavTrack> tracks() const override;

  /** Start/stop polling the model (~4 Hz). */
  void setRunning(bool run);

private:
  void poll();  // emit dynamicChanged; staticChanged on set-size change

  QTimer* m_timer = nullptr;
  int m_last_static_sig = -1;  // cheap change-detect for routes/tracks/wpts
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_MODEL_NAV_DATA_PROVIDER_H_
