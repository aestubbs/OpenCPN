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
 * SwitchableNavDataProvider -- a NavDataProvider that forwards AIS / own-ship
 * to one of two backing providers (demo vs live), while routes, tracks and
 * waypoints are read straight from the MODEL (pRouteList / g_TrackList /
 * pWayPointMan) and persisted to the SQLite navobj DB (NavObj_dB) -- the
 * single source of truth (#32). So a user route is the same object whether
 * shown in demo or live mode, survives restarts, and the in-app route editor
 * (#31) mutates the model objects directly.
 *
 * The in-progress "draft" route (Create Route, #28) stays a lightweight Qt
 * overlay for a responsive rubber-band; on finish it is committed to a model
 * Route and inserted into the DB.
 *
 * Model-touching methods are implemented in switchable_nav_provider.cpp so
 * the model/wx headers stay out of this widely-included header.
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
      // The live provider's staticChanged means the model routes/tracks
      // changed (e.g. a fresh fix grew the active track) -- forward it in
      // both modes so the model-backed overlays refresh.
      connect(p, &NavDataProvider::staticChanged, this,
              [this]() { Q_EMIT staticChanged(); });
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
  // The draft route is a Qt-side overlay merged into routes(); on finish it
  // becomes a model Route persisted to the DB.
  bool buildingRoute() const { return m_building; }

  /** Start a new draft route (clears any prior draft). */
  void beginRoute() {
    m_building = true;
    m_has_rubber = false;
    m_draft = NavRoute{};
    m_draft.name = QStringLiteral("Route %1").arg(++m_route_seq);
    Q_EMIT editChanged();
  }
  /** Append a vertex (degrees) to the draft route. */
  void addRoutePoint(double lat, double lon) {
    if (!m_building) return;
    m_draft.points.append(QPointF(lon, lat));
    Q_EMIT editChanged();
  }
  /** Live "rubber band" segment from the last vertex to the cursor. */
  void setRouteRubberband(double lat, double lon) {
    if (!m_building) return;
    m_rubber = QPointF(lon, lat);
    m_has_rubber = true;
    Q_EMIT editChanged();  // cheap route-only redraw, no waypoint relabel
  }
  /** Commit the draft (>=2 points) as a model Route + persist. True if kept. */
  bool finishRoute();
  /** Discard the draft without committing. */
  void cancelRoute() {
    if (!m_building) return;
    m_building = false;
    m_has_rubber = false;
    m_draft = NavRoute{};
    Q_EMIT editChanged();
  }

  // --- Route editing (#31): operates on the model routes (pRouteList), in
  //     pRouteList order so an index from userRoutes() maps straight back.
  //     Each committed change is persisted via NavObj_dB. -----------------
  QList<NavRoute> userRoutes() const;            // model routes, in order
  void moveRoutePoint(int route, int pt, double lat, double lon);  // drag
  void commitRouteEdit();                         // drag release -> persist
  void insertRoutePoint(int route, int seg, double lat, double lon);
  void deleteRoutePoint(int route, int pt);
  void deleteRoute(int route);
  void reverseRoute(int route);                       // flip course direction
  void duplicateRoute(int route);                     // clone -> "<name> copy"
  void renameRoute(int route, const QString& name);   // set + persist name

  // --- Marks (free waypoints), all persisted via NavObj_dB ---
  void dropMark(double lat, double lon, const QString& name,
                const QString& comment, const QString& icon);
  void renameWaypoint(const QString& guid, const QString& name);
  void setWaypointComment(const QString& guid, const QString& comment);
  void setWaypointIcon(const QString& guid, const QString& icon);
  void setWaypointVisible(const QString& guid, bool visible);
  void deleteWaypoint(const QString& guid);

  // --- Tracks (own-vessel recording), persisted via NavObj_dB ---
  void startTrack();   // new dated ActiveTrack -> g_TrackList, begins recording
  void stopTrack();    // finalize (discard if < 2 points)
  void resetTrack();   // stop + start: a fresh dated track tile
  void renameTrack(const QString& guid, const QString& name);
  void setTrackVisible(const QString& guid, bool visible);
  void deleteTrack(const QString& guid);

  // --- Own-ship track recording (#29): the model ActiveTrack records off
  //     the own-ship fix on its own timer and persists to the DB. ----------
  bool recordingTrack() const { return m_recording; }
  void setRecordingTrack(bool on);

  QList<AisTarget> aisTargets() const override {
    return current() ? current()->aisTargets() : QList<AisTarget>();
  }
  QVector<AisTrackPoint> aisTrack(int mmsi, qint64 since_ms) const override {
    return current() ? current()->aisTrack(mmsi, since_ms)
                     : QVector<AisTrackPoint>();
  }
  OwnShipState ownShip() const override {
    return current() ? current()->ownShip() : OwnShipState{};
  }
  QList<NavRoute> routes() const override;     // model routes + draft
  QList<NavWaypoint> waypoints() const override;
  QList<NavTrack> tracks() const override;

private:
  NavDataProvider* current() const { return m_use_live ? m_live : m_demo; }
  QString makeTrackName() const;  // dated, #n-suffixed for same-day tracks

  NavDataProvider* m_demo;
  NavDataProvider* m_live;
  bool m_use_live = false;

  // Draft (in-progress creation) state.
  NavRoute m_draft;     // route currently being drawn
  QPointF m_rubber;     // cursor end-point for the live segment
  bool m_building = false;
  bool m_has_rubber = false;
  int m_route_seq = 0;

  bool m_recording = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SWITCHABLE_NAV_PROVIDER_H_
