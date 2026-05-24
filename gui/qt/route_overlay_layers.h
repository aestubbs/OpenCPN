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
 * Static world-anchored nav overlays (P2.11b): routes, tracks, and
 * standalone waypoints. Each is a StaticNavLayer (wholesale rebuild via
 * SgBuilder) consuming the NavDataProvider snapshot -- separate Layers so
 * they toggle and persist (P2.10) independently.
 */

#ifndef OCPN_QT_ROUTE_OVERLAY_LAYERS_H_
#define OCPN_QT_ROUTE_OVERLAY_LAYERS_H_

#include "nav_layer.h"

namespace ocpn::qtui {

/** Routes: a polyline per route plus a marker at each route point. */
class RouteLayer : public StaticNavLayer {
  Q_OBJECT
public:
  RouteLayer(NavDataProvider* p, const Viewport* v, QObject* parent = nullptr)
      : StaticNavLayer(p, v, parent) {
    setOwner(QStringLiteral("core.routes"));
  }
  QString id() const override { return QStringLiteral("core.routes"); }
  QString name() const override { return QStringLiteral("Routes"); }

protected:
  void draw(SgBuilder& b, double world_per_px) override;
};

/** Tracks: a polyline per recorded track. */
class TrackLayer : public StaticNavLayer {
  Q_OBJECT
public:
  TrackLayer(NavDataProvider* p, const Viewport* v, QObject* parent = nullptr)
      : StaticNavLayer(p, v, parent) {
    setOwner(QStringLiteral("core.tracks"));
  }
  QString id() const override { return QStringLiteral("core.tracks"); }
  QString name() const override { return QStringLiteral("Tracks"); }

protected:
  void draw(SgBuilder& b, double world_per_px) override;
};

/** Standalone waypoints: a marker plus a name label each. */
class WaypointLayer : public StaticNavLayer {
  Q_OBJECT
public:
  WaypointLayer(NavDataProvider* p, const Viewport* v,
                QObject* parent = nullptr)
      : StaticNavLayer(p, v, parent) {
    setOwner(QStringLiteral("core.waypoints"));
  }
  QString id() const override { return QStringLiteral("core.waypoints"); }
  QString name() const override { return QStringLiteral("Waypoints"); }

protected:
  void draw(SgBuilder& b, double world_per_px) override;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_ROUTE_OVERLAY_LAYERS_H_
