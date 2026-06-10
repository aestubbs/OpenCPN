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
 * RouteFollower -- the Qt front end for "activate and follow a route"
 * (P3.16). It owns no navigation math of its own: the de-wx'd model Routeman
 * (g_pRouteMan) is the single source of truth. On each navigation tick the
 * follower calls Routeman::UpdateProgress() (which recomputes bearing / range
 * / XTE, advances waypoints on arrival, and emits the autopilot NMEA), then
 * snapshots the result into bound Q_PROPERTYs for the QML nav strip and emits
 * arrived()/ended() for the arrival banner + sound.
 *
 * Activation mirrors the wx Route Manager's "Activate" action: pick the best
 * start waypoint for the current position/course (FindBestActivatePoint) and
 * call ActivateRoute. Routes are addressed by their index in pRouteList, the
 * same ordering RouteListViewModel / SwitchableNavDataProvider::userRoutes()
 * expose to QML, so a drawer index maps straight through.
 *
 * Model headers stay in the .cpp so this widely-included header carries no
 * wx/model surface.
 */

#ifndef OCPN_QT_ROUTE_FOLLOWER_H_
#define OCPN_QT_ROUTE_FOLLOWER_H_

#include <QObject>
#include <QString>

namespace ocpn::qtui {

class RouteFollower : public QObject {
  Q_OBJECT
  // Live navigation solution for the active route, snapshotted from Routeman
  // on each tick. All NOTIFY changed so the QML nav strip binds declaratively.
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(QString routeName READ routeName NOTIFY changed)
  Q_PROPERTY(QString activeRouteGuid READ activeRouteGuid NOTIFY changed)
  Q_PROPERTY(QString toWaypoint READ toWaypoint NOTIFY changed)
  // Bearing-to-waypoint, distance-to-waypoint, cross-track error: raw values
  // (deg true / NM / NM) plus unit-formatted text for direct display.
  Q_PROPERTY(double btw READ btw NOTIFY changed)
  Q_PROPERTY(double dtw READ dtw NOTIFY changed)
  Q_PROPERTY(double xte READ xte NOTIFY changed)
  Q_PROPERTY(int xteDir READ xteDir NOTIFY changed)  // -1 steer left, +1 right
  Q_PROPERTY(QString btwText READ btwText NOTIFY changed)
  Q_PROPERTY(QString dtwText READ dtwText NOTIFY changed)
  Q_PROPERTY(QString xteText READ xteText NOTIFY changed)
  Q_PROPERTY(QString vmgText READ vmgText NOTIFY changed)
  Q_PROPERTY(QString etaText READ etaText NOTIFY changed)
  Q_PROPERTY(QString legText READ legText NOTIFY changed)  // "3 / 7"
  Q_PROPERTY(bool arrival READ arrival NOTIFY changed)

public:
  explicit RouteFollower(QObject* parent = nullptr);

  bool active() const { return m_active; }
  QString routeName() const { return m_route_name; }
  QString activeRouteGuid() const { return m_route_guid; }
  QString toWaypoint() const { return m_to_wp; }
  double btw() const { return m_btw; }
  double dtw() const { return m_dtw; }
  double xte() const { return m_xte; }
  int xteDir() const { return m_xte_dir; }
  QString btwText() const { return m_btw_text; }
  QString dtwText() const { return m_dtw_text; }
  QString xteText() const { return m_xte_text; }
  QString vmgText() const { return m_vmg_text; }
  QString etaText() const { return m_eta_text; }
  QString legText() const { return m_leg_text; }
  bool arrival() const { return m_arrival; }

  /** Activate the route at pRouteList index `routeIndex`, starting from the
   *  best waypoint for the current own-ship position/course. Returns true if a
   *  route was activated (or was already the active one). */
  bool activate(int routeIndex);
  /** Activate `routeIndex` starting from the waypoint at `wpIndex` (0-based).*/
  bool activateFrom(int routeIndex, int wpIndex);
  /** Stop following (user-initiated; fires OCPN_RTE_DEACTIVATED). */
  void deactivate();
  /** Manually advance to the next waypoint (skip the current one). */
  void skip();

  /** Reset the cross-track-error origin to the current position (the wx
   *  "Zero XTE" action): the active leg is re-based so XTE measures from
   *  here instead of from the original leg line. No-op when not active. */
  Q_INVOKABLE void zeroXte();

  /** Re-activate the route persisted last session, if "Persist active route"
   *  is on and the route still exists. Call once at startup after routes load. */
  void restorePersisted();

  /** Recompute the solution from the latest fix. Wire to the nav data
   *  provider's dynamicChanged() so it runs once per own-ship tick. */
  void update();

Q_SIGNALS:
  void changed();
  /** A waypoint was reached and the route advanced to the next one. */
  void arrived(const QString& waypointName);
  /** The final waypoint was reached; the route is no longer active. */
  void ended(const QString& routeName);
  /** The active route changed (activated / deactivated / route swapped), so
   *  the canvas can refresh route visibility + the overlay. */
  void activeRouteChanged();

private:
  void snapshot();  // pull the current solution from g_pRouteMan into members

  bool m_active = false;
  QString m_route_name;
  QString m_route_guid;
  QString m_to_wp;
  double m_btw = 0.0;
  double m_dtw = 0.0;
  double m_xte = 0.0;
  int m_xte_dir = 0;
  QString m_btw_text;
  QString m_dtw_text;
  QString m_xte_text;
  QString m_vmg_text;
  QString m_eta_text;
  QString m_leg_text;
  bool m_arrival = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_ROUTE_FOLLOWER_H_
