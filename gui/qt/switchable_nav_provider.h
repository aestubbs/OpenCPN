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

  QList<AisTarget> aisTargets() const override {
    return current() ? current()->aisTargets() : QList<AisTarget>();
  }
  OwnShipState ownShip() const override {
    return current() ? current()->ownShip() : OwnShipState{};
  }
  QList<NavRoute> routes() const override {
    return current() ? current()->routes() : QList<NavRoute>();
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
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SWITCHABLE_NAV_PROVIDER_H_
