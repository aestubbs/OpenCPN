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
 * SwitchableNavDataProvider -- a NavDataProvider that forwards to one of two
 * backing providers (demo vs live), so the overlay Layers bind to a single
 * stable provider and the demo/live switch is a runtime flag rather than a
 * layer rebuild. Re-emits whichever backing provider's signals are currently
 * active, and fires both on a switch so everything refreshes.
 */

#ifndef OCPN_QT_SWITCHABLE_NAV_PROVIDER_H_
#define OCPN_QT_SWITCHABLE_NAV_PROVIDER_H_

#include "nav_data_provider.h"

namespace ocpn::qtui {

class SwitchableNavDataProvider : public NavDataProvider {
  Q_OBJECT

public:
  // Non-owning; both backing providers must outlive this.
  SwitchableNavDataProvider(NavDataProvider* demo, NavDataProvider* live,
                            QObject* parent = nullptr)
      : NavDataProvider(parent), m_demo(demo), m_live(live) {
    for (NavDataProvider* p : {m_demo, m_live}) {
      if (!p) continue;
      connect(p, &NavDataProvider::dynamicChanged, this, [this, p]() {
        if (current() == p) Q_EMIT dynamicChanged();
      });
      connect(p, &NavDataProvider::staticChanged, this, [this, p]() {
        if (current() == p) Q_EMIT staticChanged();
      });
    }
  }

  /** Select the live backing provider (true) or the demo one (false). */
  void setLive(bool live) {
    if (live == m_use_live) return;
    m_use_live = live;
    Q_EMIT dynamicChanged();
    Q_EMIT staticChanged();
  }
  bool isLive() const { return m_use_live; }

  // --- Interactive route building (Create Route, #28) ------------------
  // The draft route plus committed user routes are merged into routes() on
  // top of the active provider's, so they show in both demo and live mode
  // and appear in the route manager (which reads the same provider).
  bool buildingRoute() const { return m_building; }

  /** Start a new draft route (clears any prior draft). */
  void beginRoute() {
    m_building = true;
    m_has_rubber = false;
    m_draft = NavRoute{};
    m_draft.name = QStringLiteral("Route %1").arg(++m_route_seq);
    Q_EMIT staticChanged();
  }
  /** Append a vertex (degrees) to the draft route. */
  void addRoutePoint(double lat, double lon) {
    if (!m_building) return;
    m_draft.points.append(QPointF(lon, lat));
    Q_EMIT staticChanged();
  }
  /** Live "rubber band" segment from the last vertex to the cursor. */
  void setRouteRubberband(double lat, double lon) {
    if (!m_building) return;
    m_rubber = QPointF(lon, lat);
    m_has_rubber = true;
    Q_EMIT staticChanged();
  }
  /** Commit the draft (>=2 points) as a user route. Returns true if kept. */
  bool finishRoute() {
    if (!m_building) return false;
    const bool ok = m_draft.points.size() >= 2;
    if (ok) m_user_routes.append(m_draft);
    m_building = false;
    m_has_rubber = false;
    m_draft = NavRoute{};
    Q_EMIT staticChanged();
    return ok;
  }
  /** Discard the draft without committing. */
  void cancelRoute() {
    if (!m_building) return;
    m_building = false;
    m_has_rubber = false;
    m_draft = NavRoute{};
    Q_EMIT staticChanged();
  }

  QList<AisTarget> aisTargets() const override {
    return current() ? current()->aisTargets() : QList<AisTarget>();
  }
  OwnShipState ownShip() const override {
    return current() ? current()->ownShip() : OwnShipState{};
  }
  QList<NavRoute> routes() const override {
    QList<NavRoute> r = current() ? current()->routes() : QList<NavRoute>();
    r += m_user_routes;
    if (m_building && !m_draft.points.isEmpty()) {
      NavRoute d = m_draft;
      if (m_has_rubber) d.points.append(m_rubber);  // live segment to cursor
      r.append(d);
    }
    return r;
  }
  QList<NavWaypoint> waypoints() const override {
    return current() ? current()->waypoints() : QList<NavWaypoint>();
  }
  QList<NavTrack> tracks() const override {
    return current() ? current()->tracks() : QList<NavTrack>();
  }

private:
  NavDataProvider* current() const { return m_use_live ? m_live : m_demo; }

  NavDataProvider* m_demo;
  NavDataProvider* m_live;
  bool m_use_live = false;

  // Route-building state.
  QList<NavRoute> m_user_routes;  // committed user routes
  NavRoute m_draft;               // route currently being drawn
  QPointF m_rubber;               // cursor end-point for the live segment
  bool m_building = false;
  bool m_has_rubber = false;
  int m_route_seq = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SWITCHABLE_NAV_PROVIDER_H_
