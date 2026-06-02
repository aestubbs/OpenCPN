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

#include <QSet>
#include <QString>

#include "nav_layer.h"

namespace ocpn::qtui {

/** Routes: a polyline per route plus a marker at each route point. */
class RouteLayer : public StaticNavLayer {
  Q_OBJECT
public:
  RouteLayer(NavDataProvider* p, const Viewport* v, QObject* parent = nullptr)
      : StaticNavLayer(p, v, parent) {
    setOwner(QStringLiteral("core.routes"));
    // Also rebuild on interactive route edits (draft rubber-band / drag) --
    // the cheap route-only path that doesn't relabel waypoints (see
    // NavDataProvider::editChanged).
    connectData(&NavDataProvider::editChanged);
  }
  QString id() const override { return QStringLiteral("core.routes"); }
  QString name() const override { return QStringLiteral("Routes"); }

  /** Display colour scheme (0=day,1=dusk,2=night): graphite grey route lines
   *  in day, lighter grey at dusk/night so they read on the dimmed chart. */
  void setColorScheme(int scheme) {
    if (scheme == m_scheme) return;
    m_scheme = scheme;
    Q_EMIT dirty();
  }

  /** GUID of the route selected for editing; drawn emphasised with larger
   *  draggable node handles. Empty = none. */
  void setSelectedRouteGuid(const QString& guid) {
    if (guid == m_selected) return;
    m_selected = guid;
    Q_EMIT dirty();
  }

  /** GUIDs whose visibility "eye" is on. A route is drawn when its eye is on
   *  OR it is the selected route -- visibility and selection are independent
   *  (P3.7). Default empty: only the selected route shows. */
  void setVisibleRouteGuids(const QSet<QString>& guids) {
    m_visible = guids;
    Q_EMIT dirty();
  }

  /** Whether the selected route is in edit mode. Only then does it get the
   *  large draggable node handles; a merely-selected route gets a subtle
   *  selection accent (so selection and edit look distinct). */
  void setEditing(bool editing) {
    if (editing == m_editing) return;
    m_editing = editing;
    Q_EMIT dirty();
  }

protected:
  void draw(SgBuilder& b, double world_per_px) override;

private:
  int m_scheme = 0;
  QString m_selected;
  QSet<QString> m_visible;
  bool m_editing = false;
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

  /** GUID of the selected track (drawn emphasised). Empty = none. */
  void setSelectedTrackGuid(const QString& guid) {
    if (guid == m_selected) return;
    m_selected = guid;
    Q_EMIT dirty();
  }

protected:
  void draw(SgBuilder& b, double world_per_px) override;

private:
  QString m_selected;
};

/** Standalone waypoints (free marks): a marker plus a name label each. Honours
 *  the per-mark visibility flag (the eye); the selected mark is emphasised. */
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

  /** GUID of the selected mark (drawn emphasised). Empty = none. */
  void setSelectedWaypointGuid(const QString& guid) {
    if (guid == m_selected) return;
    m_selected = guid;
    Q_EMIT dirty();
  }

protected:
  void draw(SgBuilder& b, double world_per_px) override;

private:
  QString m_selected;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_ROUTE_OVERLAY_LAYERS_H_
