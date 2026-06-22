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
 * Route editing, navigation and GPX import/export for ChartCanvas.
 *
 * Part of the ChartCanvas implementation, split out of chart_canvas.cpp. See
 * chart_canvas_internal.h for the shared include block and rationale.
 */

#include "chart_canvas.h"

#include "chart_canvas_internal.h"

namespace ocpn::qtui {

// --- Route editing (#31) ---------------------------------------------------
bool ChartCanvas::hitRouteNode(const QPointF& sp, int& route, int& node) const {
  if (!m_nav_provider || !m_viewport) return false;
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  constexpr double kR = 11.0;  // px
  double best = kR * kR;
  bool found = false;
  const QList<NavRoute>& rs = m_nav_provider->userRoutes();
  for (int ri = 0; ri < rs.size(); ++ri) {
    const NavRoute& r = rs[ri];
    // Only hit routes that are actually drawn: eye on, or the selected route
    // (matches RouteLayer's draw rule) -- an invisible route must not be
    // summoned by a click where it happens to lie.
    if (!m_visible_routes.contains(r.guid) && ri != m_selected_route) continue;
    for (int pi = 0; pi < r.points.size(); ++pi) {
      const QPointF s = m.map(QPointF(
          r.points[pi].x(), Viewport::latToWorldY(r.points[pi].y())));
      const double dx = s.x() - sp.x(), dy = s.y() - sp.y();
      const double d2 = dx * dx + dy * dy;
      if (d2 < best) {
        best = d2;
        route = ri;
        node = pi;
        found = true;
      }
    }
  }
  return found;
}

bool ChartCanvas::hitRouteSegment(const QPointF& sp, int& route, int& seg,
                                  double& lat, double& lon) const {
  if (!m_nav_provider || !m_viewport) return false;
  const int w = static_cast<int>(width()), h = static_cast<int>(height());
  const QMatrix4x4 m = m_viewport->transformMatrix(w, h);
  constexpr double kR = 8.0;  // px
  double best = kR * kR;
  bool found = false;
  const QList<NavRoute>& rs = m_nav_provider->userRoutes();
  for (int ri = 0; ri < rs.size(); ++ri) {
    const NavRoute& r = rs[ri];
    // Only hit routes that are actually drawn (eye on, or selected) -- don't
    // let a click on empty water select a hidden route lying under it.
    if (!m_visible_routes.contains(r.guid) && ri != m_selected_route) continue;
    for (int si = 0; si + 1 < r.points.size(); ++si) {
      const QPointF a = m.map(QPointF(
          r.points[si].x(), Viewport::latToWorldY(r.points[si].y())));
      const QPointF bp = m.map(QPointF(
          r.points[si + 1].x(), Viewport::latToWorldY(r.points[si + 1].y())));
      const QPointF ab = bp - a;
      const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
      double t = l2 > 0.0 ? ((sp.x() - a.x()) * ab.x() +
                             (sp.y() - a.y()) * ab.y()) / l2
                          : 0.0;
      t = std::clamp(t, 0.0, 1.0);
      const QPointF proj = a + ab * t;
      const double dx = proj.x() - sp.x(), dy = proj.y() - sp.y();
      const double d2 = dx * dx + dy * dy;
      if (d2 < best) {
        best = d2;
        route = ri;
        seg = si;
        found = true;
      }
    }
  }
  if (found) m_viewport->screenToLatLon(sp.x(), sp.y(), w, h, lat, lon);
  return found;
}

void ChartCanvas::setRouteEditMode(bool on) {
  if (on == m_route_edit_mode) return;
  m_route_edit_mode = on;
  if (m_route_layer) m_route_layer->setEditing(on);  // big handles only in edit
  emit routeEditModeChanged();
  update();
}

void ChartCanvas::editRoute(int index) {
  showRoute(index);          // select + solo + zoom to extent
  setRouteEditMode(true);    // now nodes are draggable / legs insertable
}

void ChartCanvas::selectRoute(int route) {
  if (route == m_selected_route) return;
  m_selected_route = route;
  setRouteEditMode(false);  // changing the selection leaves edit mode

  if (m_route_layer) {
    QString guid;
    if (m_nav_provider && route >= 0) {
      const QList<NavRoute> rs = m_nav_provider->userRoutes();
      if (route < rs.size()) guid = rs[route].guid;
    }
    m_route_layer->setSelectedRouteGuid(guid);
  }
  emit selectedRouteChanged();
  update();
}

void ChartCanvas::clearRouteSelection() { selectRoute(-1); }

void ChartCanvas::deleteRoutePointAtMenu() {
  if (m_nav_provider && m_menu_route >= 0 && m_menu_node >= 0)
    m_nav_provider->deleteRoutePoint(m_menu_route, m_menu_node);
  m_menu_route = m_menu_node = -1;
  clearRouteSelection();  // indices may have shifted -- drop selection
}

void ChartCanvas::deleteSelectedRoute() {
  if (m_nav_provider && m_selected_route >= 0)
    m_nav_provider->deleteRoute(m_selected_route);
  clearRouteSelection();
}

void ChartCanvas::reverseRoute(int index) {
  if (m_nav_provider) m_nav_provider->reverseRoute(index);
}

void ChartCanvas::duplicateRoute(int index) {
  if (m_nav_provider) m_nav_provider->duplicateRoute(index);
}

void ChartCanvas::setRoutePointIcon(int index, const QString& icon) {
  if (m_nav_provider) m_nav_provider->setRoutePointIcon(index, icon);
  update();
}

QString ChartCanvas::routePointIcon(int index) const {
  if (!m_nav_provider) return QString();
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size()) return QString();
  return rs[index].pointIcon;
}

void ChartCanvas::showRoute(int index) {
  if (!m_nav_provider) return;
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size() || rs[index].points.isEmpty()) return;
  double n = -90, s = 90, e = -180, w = 180;
  for (const QPointF& p : rs[index].points) {  // (lon, lat)
    n = std::max(n, p.y());
    s = std::min(s, p.y());
    e = std::max(e, p.x());
    w = std::min(w, p.x());
  }
  selectRoute(index);   // highlight it
  fitBounds(n, s, e, w);  // zoom to its extent
}

void ChartCanvas::activateRoute(int index) {
  if (!m_route_follower || !m_nav_provider) return;
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size()) return;
  // Make sure the route's "eye" is on so the followed route is visible.
  if (!rs[index].guid.isEmpty() && !m_visible_routes.contains(rs[index].guid))
    setRouteVisible(index, true);
  if (!m_route_follower->activate(index)) return;
  // Zoom to the route's extent (wx ZoomtoRoute), leaving the edit selection
  // untouched (following is not editing).
  if (!rs[index].points.isEmpty()) {
    double n = -90, s = 90, e = -180, w = 180;
    for (const QPointF& p : rs[index].points) {  // (lon, lat)
      n = std::max(n, p.y());
      s = std::min(s, p.y());
      e = std::max(e, p.x());
      w = std::min(w, p.x());
    }
    fitBounds(n, s, e, w);
  }
  update();
}

void ChartCanvas::deactivateRoute() {
  if (m_route_follower) m_route_follower->deactivate();
  update();
}

void ChartCanvas::skipWaypoint() {
  if (m_route_follower) m_route_follower->skip();
  update();
}

void ChartCanvas::startGotoRoute(double lat, double lon, const QString& name) {
  if (!m_nav_provider || !m_route_follower) return;
  const OwnShipState own = m_nav_provider->ownShip();
  if (!own.valid) return;
  // Only one outstanding GOTO at a time: drop a previous, unfinished one.
  if (!m_goto_guid.isEmpty()) {
    const QList<NavRoute> routes = m_nav_provider->userRoutes();
    for (int i = 0; i < routes.size(); ++i) {
      if (routes[i].guid == m_goto_guid) {
        m_nav_provider->deleteRoute(i);
        break;
      }
    }
    m_goto_guid.clear();
  }
  const int idx = m_nav_provider->createRoute(
      name, {QPointF(own.lon, own.lat), QPointF(lon, lat)});
  if (idx < 0) return;
  m_goto_guid = m_nav_provider->userRoutes().value(idx).guid;
  // Activate directly (no zoom-to-extent: the boat and the target are both
  // already in or near the view the user is working in).
  m_route_follower->activate(idx);
  update();
}

void ChartCanvas::navigateToHere() {
  // wx ID_DEF_MENU_GOTO_HERE: a temporary 2-point route from the fix to the
  // right-click point, activated immediately, deleted on arrival.
  startGotoRoute(m_ctx_lat, m_ctx_lon, tr("Go to here"));
}

void ChartCanvas::navigateToWaypoint(const QString& guid) {
  // wx ID_WP_MENU_GOTO: a temporary route from the fix to the mark.
  if (!m_nav_provider) return;
  for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
    if (wp.guid == guid) {
      startGotoRoute(wp.lat, wp.lon,
                     wp.name.isEmpty() ? tr("Go to mark")
                                       : tr("Go to %1").arg(wp.name));
      return;
    }
  }
}

void ChartCanvas::zeroXte() {
  if (m_route_follower) m_route_follower->zeroXte();
}

void ChartCanvas::insertRoutePointAtMenu() {
  if (!m_nav_provider || m_menu_route < 0 || m_menu_seg < 0) return;
  m_nav_provider->insertRoutePoint(m_menu_route, m_menu_seg, m_menu_ins_lat,
                                   m_menu_ins_lon);
  update();
}

QVariantMap ChartCanvas::importGpx(const QUrl& url) {
  if (!m_nav_provider) return {};
  const QVariantMap counts = m_nav_provider->importGpx(url.toLocalFile());
  update();
  return counts;
}

bool ChartCanvas::exportGpxAll(const QUrl& url) const {
  return m_nav_provider && m_nav_provider->exportGpxAll(url.toLocalFile());
}

bool ChartCanvas::exportGpxRoute(int index, const QUrl& url) const {
  return m_nav_provider &&
         m_nav_provider->exportGpxRoute(index, url.toLocalFile());
}

bool ChartCanvas::exportGpxTrack(const QString& guid, const QUrl& url) const {
  return m_nav_provider &&
         m_nav_provider->exportGpxTrack(guid, url.toLocalFile());
}

bool ChartCanvas::exportGpxWaypoint(const QString& guid,
                                    const QUrl& url) const {
  return m_nav_provider &&
         m_nav_provider->exportGpxWaypoint(guid, url.toLocalFile());
}

void ChartCanvas::appendToRoute(int index) {
  if (!m_nav_provider || m_route_build_mode) return;
  if (!m_nav_provider->beginAppendRoute(index)) return;
  // Reuse the route-build mouse flow: click adds a point (addRoutePoint
  // appends to the model route in append mode), right-click finishes.
  m_route_build_mode = true;
  emit routeBuildModeChanged();
  update();
}

void ChartCanvas::splitRouteAtMenu() {
  if (!m_nav_provider || m_menu_route < 0 || m_menu_seg < 0) return;
  // A followed route can't be split under the follower's feet.
  if (m_nav_provider->userRoutes().value(m_menu_route).active)
    deactivateRoute();
  clearRouteSelection();  // indices shift: original deleted, A + B appended
  m_nav_provider->splitRoute(m_menu_route, m_menu_seg);
  update();
}


}  // namespace ocpn::qtui
