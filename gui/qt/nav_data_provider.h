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
 * NavDataProvider -- the pluggable source of live navigation data for the
 * overlay Layers (P2.11): AIS targets, own ship, routes, tracks, waypoints.
 *
 * The overlay Layers (AisLayer, OwnShipLayer, RouteLayer) render purely from
 * this interface and rebuild their subtree when it emits changed(). Two
 * implementations are envisaged:
 *   - DemoNavDataProvider -- synthetic moving data ("demo mode"), used now
 *     while opencpn-qt has no comm stack.
 *   - a future adapter over the real model (g_pAIS's info_update signal,
 *     pRouteList / pWayPointMan / g_TrackList) -- same interface, so the
 *     Layers and the demo/live switch in ChartCanvas need no change.
 *
 * Accessors return snapshots by value (cheap Qt implicitly-shared lists);
 * the Layers copy what they need at build time and never hold provider
 * internals.
 */

#ifndef OCPN_QT_NAV_DATA_PROVIDER_H_
#define OCPN_QT_NAV_DATA_PROVIDER_H_

#include <QObject>

#include "nav_data.h"

namespace ocpn::qtui {

class NavDataProvider : public QObject {
  Q_OBJECT

public:
  explicit NavDataProvider(QObject* parent = nullptr) : QObject(parent) {}
  ~NavDataProvider() override = default;

  virtual QList<AisTarget> aisTargets() const = 0;
  virtual OwnShipState ownShip() const = 0;
  virtual QList<NavRoute> routes() const = 0;
  virtual QList<NavWaypoint> waypoints() const = 0;
  virtual QList<NavTrack> tracks() const = 0;

Q_SIGNALS:
  /** Emitted when any of the above has changed and the overlay Layers should
   *  rebuild. */
  void changed();
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NAV_DATA_PROVIDER_H_
