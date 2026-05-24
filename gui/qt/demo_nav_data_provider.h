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
 * DemoNavDataProvider -- synthetic, animated NavDataProvider for "demo mode"
 * (P2.11). Drives a handful of AIS targets and an own ship moving along
 * their courses, plus a static demo route / track / waypoints, all near the
 * North Sea test-chart area. A timer advances positions a few times a second
 * and emits changed() so the overlay Layers re-render and the scene
 * animates -- exercising the reactive overlay path with no comm stack.
 *
 * Swap this out for a real model adapter (same interface) to show live data.
 */

#ifndef OCPN_QT_DEMO_NAV_DATA_PROVIDER_H_
#define OCPN_QT_DEMO_NAV_DATA_PROVIDER_H_

#include <QElapsedTimer>

#include "nav_data_provider.h"

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class Viewport;

class DemoNavDataProvider : public NavDataProvider {
  Q_OBJECT

public:
  // `viewport` (optional) lets the synthetic layout scale to the current
  // zoom so the fleet fills the view wherever it's dropped.
  explicit DemoNavDataProvider(const Viewport* viewport = nullptr,
                               QObject* parent = nullptr);

  QList<AisTarget> aisTargets() const override { return m_targets; }
  OwnShipState ownShip() const override { return m_own; }
  QList<NavRoute> routes() const override { return m_routes; }
  QList<NavWaypoint> waypoints() const override { return m_waypoints; }
  QList<NavTrack> tracks() const override { return m_tracks; }

  /** Start/stop the animation timer. Stopped providers still return their
   *  current snapshot (so toggling demo mode off freezes rather than
   *  clears). */
  void setRunning(bool run);
  bool running() const;

  /** (Re)place the synthetic fleet + route/track/waypoints centred on
   *  (lat, lon), scaled to the current viewport zoom so it fills the view.
   *  Emits dynamicChanged() + staticChanged(). */
  void seedAround(double lat, double lon);

private:
  void tick();  // advance positions by the elapsed interval, emit changed()

  // Advance a (lat, lon) by `sog` knots along `cog` for `seconds`.
  static void advance(double& lat, double& lon, double cog, double sog,
                      double seconds);

  const Viewport* m_viewport = nullptr;  // for zoom-scaled layout (non-owning)
  QTimer* m_timer = nullptr;
  QElapsedTimer m_clock;        // wall-clock between ticks
  qint64 m_last_ms = 0;

  QList<AisTarget> m_targets;
  OwnShipState m_own;
  QList<NavRoute> m_routes;
  QList<NavWaypoint> m_waypoints;
  QList<NavTrack> m_tracks;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_DEMO_NAV_DATA_PROVIDER_H_
