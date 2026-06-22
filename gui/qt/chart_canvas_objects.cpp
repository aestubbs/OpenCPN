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
 * AIS targets, marks, tracks, MMSI properties, KML clipboard and screen hit-testing for ChartCanvas.
 *
 * Part of the ChartCanvas implementation, split out of chart_canvas.cpp. See
 * chart_canvas_internal.h for the shared include block and rationale.
 */

#include "chart_canvas.h"

#include "chart_canvas_internal.h"

namespace ocpn::qtui {

bool ChartCanvas::hitAisAt(const QPointF& sp, int* mmsi,
                           QString* name) const {
  if (!m_nav_provider || !m_viewport) return false;
  constexpr double kPickRadiusPx = 14.0;
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  double best = kPickRadiusPx * kPickRadiusPx;
  bool found = false;
  for (const AisTarget& t : m_nav_provider->aisTargets()) {
    const QPointF s = m.map(QPointF(t.lon, Viewport::latToWorldY(t.lat)));
    const double dx = s.x() - sp.x(), dy = s.y() - sp.y();
    const double d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      if (mmsi) *mmsi = t.mmsi;
      if (name) *name = t.name;
      found = true;
    }
  }
  return found;
}

void ChartCanvas::selectAisTarget(int mmsi) {
  if (!m_ais_selection || !m_nav_provider) return;
  for (const AisTarget& t : m_nav_provider->aisTargets()) {
    if (t.mmsi == mmsi) {
      m_ais_selection->select(t);
      return;
    }
  }
}

void ChartCanvas::centerOnAis(int mmsi) {
  if (!m_viewport || !m_nav_provider) return;
  for (const AisTarget& t : m_nav_provider->aisTargets()) {
    if (t.mmsi == mmsi) {
      m_viewport->setCenter(t.lat, t.lon);
      emit viewChanged();
      update();
      return;
    }
  }
}

void ChartCanvas::copyToClipboard(const QString& text) const {
  if (QClipboard* cb = QGuiApplication::clipboard()) cb->setText(text);
}

bool ChartCanvas::copyRouteAsKml(int index) const {
  if (!m_nav_provider) return false;
  const QString kml = m_nav_provider->routeAsKml(index);
  if (kml.isEmpty()) return false;
  copyToClipboard(kml);
  return true;
}

bool ChartCanvas::copyTrackAsKml(const QString& guid) const {
  if (!m_nav_provider) return false;
  const QString kml = m_nav_provider->trackAsKml(guid);
  if (kml.isEmpty()) return false;
  copyToClipboard(kml);
  return true;
}

QVariantMap ChartCanvas::pasteKmlFromClipboard() {
  QClipboard* cb = QGuiApplication::clipboard();
  if (!cb || !m_nav_provider) return {};
  const QVariantMap counts = m_nav_provider->pasteKml(cb->text());
  if (!counts.isEmpty()) update();
  return counts;
}

bool ChartCanvas::copyMarkAsKml(const QString& guid) const {
  if (!m_nav_provider) return false;
  const QString kml = m_nav_provider->waypointAsKml(guid);
  if (kml.isEmpty()) return false;
  copyToClipboard(kml);
  return true;
}

QVariantList ChartCanvas::aisTargetSnapshot() const {
  QVariantList out;
  if (!m_nav_provider) return out;
  QList<AisTarget> targets = m_nav_provider->aisTargets();
  // Nearest first; targets without a range solution sort to the end.
  std::sort(targets.begin(), targets.end(),
            [](const AisTarget& a, const AisTarget& b) {
              const double ra = a.rangeNm >= 0 ? a.rangeNm : 1e9;
              const double rb = b.rangeNm >= 0 ? b.rangeNm : 1e9;
              return ra < rb;
            });
  DisplayConfig& dc = DisplayConfig::instance();
  for (const AisTarget& t : targets) {
    QVariantMap row;
    row["mmsi"] = t.mmsi;
    row["name"] = t.name.isEmpty() ? QString::number(t.mmsi) : t.name;
    row["rangeText"] = t.rangeNm >= 0 ? dc.formatDistance(t.rangeNm)
                                      : QStringLiteral("--");
    row["bearingText"] = t.bearingDeg >= 0 ? dc.formatBearing(t.bearingDeg)
                                           : QStringLiteral("--");
    row["sogText"] = QString::number(t.sog, 'f', 1);
    row["cogText"] = QString::number(t.cog, 'f', 0) + QChar(0x00B0);
    row["cpaText"] =
        t.cpaValid ? dc.formatDistance(t.cpaNm) : QStringLiteral("--");
    row["tcpaText"] = t.cpaValid && t.tcpaMin >= 0
                          ? QString::number(t.tcpaMin, 'f', 0) + tr(" min")
                          : QStringLiteral("--");
    row["dangerous"] = t.dangerous;
    row["isSart"] = t.isSart;
    out.append(row);
  }
  return out;
}

void ChartCanvas::setMarkRangeRings(const QString& guid, bool show,
                                    int count, double step, int units) {
  if (!m_nav_provider) return;
  m_nav_provider->setWaypointRangeRings(guid, show, count, step, units);
  update();
}

void ChartCanvas::setMarkScamin(const QString& guid, int scamin) {
  if (!m_nav_provider) return;
  m_nav_provider->setWaypointScamin(guid, scamin);
  update();
}

void ChartCanvas::loadMmsiProperties() {
  const QString blob = ConfigStore::instance().getString("ais/mmsiProps");
  if (blob.isEmpty()) return;
  qDeleteAll(g_MMSI_Props_Array);
  g_MMSI_Props_Array.clear();
  for (const QString& spec : blob.split('|', Qt::SkipEmptyParts)) {
    wxString wspec = QString_to_wxString(spec);
    g_MMSI_Props_Array.append(new MmsiProperties(wspec));
  }
}

void ChartCanvas::persistMmsiProperties() const {
  QStringList specs;
  for (MmsiProperties* p : g_MMSI_Props_Array)
    if (p) specs.append(wxString_to_QString(p->Serialize()));
  ConfigStore::instance().setString("ais/mmsiProps", specs.join('|'));
}

QVariantList ChartCanvas::mmsiProperties() const {
  QVariantList out;
  for (MmsiProperties* p : g_MMSI_Props_Array) {
    if (!p) continue;
    QVariantMap row;
    row["mmsi"] = p->MMSI;
    row["trackType"] = p->TrackType;  // 0 default, 1 always, 2 never
    row["ignore"] = p->m_bignore;
    row["mob"] = p->m_bMOB;
    row["vdm"] = p->m_bVDM;
    row["follower"] = p->m_bFollower;
    row["persistTrack"] = p->m_bPersistentTrack;
    row["shipName"] = wxString_to_QString(p->m_ShipName);
    out.append(row);
  }
  return out;
}

void ChartCanvas::saveMmsiProperty(const QVariantMap& row) {
  const int mmsi = row.value("mmsi").toInt();
  if (mmsi <= 0) return;
  MmsiProperties* p = nullptr;
  for (MmsiProperties* q : g_MMSI_Props_Array)
    if (q && q->MMSI == mmsi) {
      p = q;
      break;
    }
  if (!p) {
    p = new MmsiProperties(mmsi);
    g_MMSI_Props_Array.append(p);
  }
  p->TrackType = row.value("trackType", TRACKTYPE_DEFAULT).toInt();
  p->m_bignore = row.value("ignore", false).toBool();
  p->m_bMOB = row.value("mob", false).toBool();
  p->m_bVDM = row.value("vdm", false).toBool();
  p->m_bFollower = row.value("follower", false).toBool();
  p->m_bPersistentTrack = row.value("persistTrack", false).toBool();
  p->m_ShipName = QString_to_wxString(row.value("shipName").toString());
  persistMmsiProperties();
}

void ChartCanvas::deleteMmsiProperty(int mmsi) {
  for (int i = 0; i < g_MMSI_Props_Array.size(); ++i) {
    if (g_MMSI_Props_Array[i] && g_MMSI_Props_Array[i]->MMSI == mmsi) {
      delete g_MMSI_Props_Array.takeAt(i);
      persistMmsiProperties();
      return;
    }
  }
}

bool ChartCanvas::hitTrackAt(const QPointF& sp, QString* guid,
                             QString* name) const {
  if (!m_nav_provider || !m_viewport) return false;
  constexpr double kR = 8.0;  // px to the nearest segment (route parity)
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  double best = kR * kR;
  bool found = false;
  for (const NavTrack& t : m_nav_provider->tracks()) {
    if (!t.visible || t.points.size() < 2) continue;
    QPointF prev;
    for (int i = 0; i < t.points.size(); ++i) {
      const QPointF w(t.points[i].x(), Viewport::latToWorldY(t.points[i].y()));
      const QPointF s2 = m.map(w);
      if (i > 0) {
        const QPointF d = s2 - prev;
        const double len2 = d.x() * d.x() + d.y() * d.y();
        double tparam = 0.0;
        if (len2 > 1e-9)
          tparam = std::clamp((QPointF::dotProduct(sp - prev, d)) / len2, 0.0,
                              1.0);
        const QPointF c = prev + tparam * d;
        const double dx = c.x() - sp.x(), dy = c.y() - sp.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < best) {
          best = d2;
          if (guid) *guid = t.guid;
          if (name) *name = t.name;
          found = true;
        }
      }
      prev = s2;
    }
  }
  return found;
}

bool ChartCanvas::hitWaypointAt(const QPointF& sp, QString* guid,
                                QString* name) const {
  if (!m_nav_provider || !m_viewport) return false;
  constexpr double kPickRadiusPx = 12.0;
  const QMatrix4x4 m = m_viewport->transformMatrix(
      static_cast<int>(width()), static_cast<int>(height()));
  double best = kPickRadiusPx * kPickRadiusPx;
  bool found = false;
  for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
    if (!wp.visible) continue;
    const QPointF s = m.map(QPointF(wp.lon, Viewport::latToWorldY(wp.lat)));
    const double dx = s.x() - sp.x(), dy = s.y() - sp.y();
    const double d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      if (guid) *guid = wp.guid;
      if (name) *name = wp.name;
      found = true;
    }
  }
  return found;
}

void ChartCanvas::placeSimShipHere() {
  if (!m_sim_ship) return;
  // The test ship IS the live position source, so leave demo mode and make
  // sure the model is polled (mirrors the globals the sim writes into the
  // overlays + HUD + the route follower).
  if (m_demo_mode) setDemoMode(false);
  if (m_model_provider) m_model_provider->setModelPolling(true);
  m_sim_ship->place(m_ctx_lat, m_ctx_lon);
  if (m_viewport) {
    m_viewport->setCenter(m_ctx_lat, m_ctx_lon);
    emit viewChanged();
  }
  m_live_centered = true;  // we explicitly centred on the test ship
  update();
}

bool ChartCanvas::routeVisible(int index) const {
  if (!m_nav_provider) return false;
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size()) return false;
  return m_visible_routes.contains(rs[index].guid);
}

void ChartCanvas::setRouteVisible(int index, bool on) {
  if (!m_nav_provider) return;
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size()) return;
  const QString guid = rs[index].guid;
  if (guid.isEmpty()) return;
  const bool had = m_visible_routes.contains(guid);
  if (had == on) return;
  if (on)
    m_visible_routes.insert(guid);
  else
    m_visible_routes.remove(guid);
  if (m_route_layer) m_route_layer->setVisibleRouteGuids(m_visible_routes);
  ++m_route_vis_rev;
  emit routeVisibilityChanged();
  update();
}

// --- Marks (free waypoints) -------------------------------------------------

QVariantMap ChartCanvas::viewBounds() const {
  QVariantMap m;
  if (!m_viewport) return m;
  double n = -90, s = 90, e = -180, w = 180;
  const int cw = qMax(1, static_cast<int>(width()));
  const int ch = qMax(1, static_cast<int>(height()));
  // Sample all four corners (rotation-safe).
  const double xs[2] = {0.0, static_cast<double>(cw)};
  const double ys[2] = {0.0, static_cast<double>(ch)};
  for (double sx : xs) {
    for (double sy : ys) {
      double lat = 0, lon = 0;
      m_viewport->screenToLatLon(sx, sy, cw, ch, lat, lon);
      n = qMax(n, lat);
      s = qMin(s, lat);
      e = qMax(e, lon);
      w = qMin(w, lon);
    }
  }
  m["north"] = n;
  m["south"] = s;
  m["east"] = e;
  m["west"] = w;
  return m;
}

void ChartCanvas::dropAnchorMark(double lat, double lon) {
  if (!m_nav_provider) return;
  const QString name =
      QStringLiteral("Anchor ") +
      QDateTime::currentDateTime().toString(QStringLiteral("dd MMM hh:mm"));
  const QString guid = m_nav_provider->dropMark(
      lat, lon, name, tr("Set by anchor watch"), QStringLiteral("anchor"));
  if (!guid.isEmpty()) {
    UndoOp op;
    op.created = true;
    op.guid = guid;
    op.snap = snapshotMark(guid);
    pushUndo(op);
  }
  update();
}

void ChartCanvas::dropMarkHere(const QString& name, const QString& comment,
                               const QString& icon) {
  if (!m_nav_provider) return;
  const QString guid =
      m_nav_provider->dropMark(m_ctx_lat, m_ctx_lon, name, comment, icon);
  if (!guid.isEmpty()) {
    UndoOp op;
    op.created = true;
    op.guid = guid;
    op.snap = snapshotMark(guid);
    pushUndo(op);
  }
  update();
}

// Instant menu-driven drops (wx parity): default icon, dated name; the
// mark dialog renames/re-icons afterwards if wanted.
static QString datedMarkName() {
  return QDateTime::currentDateTime().toString(
      QStringLiteral("MMddhhmmss"));
}

void ChartCanvas::dropMarkAtCursor() {
  if (!m_nav_provider) return;
  const QString guid =
      m_nav_provider->dropMark(m_cursor_pos_lat, m_cursor_pos_lon,
                               datedMarkName(), QString(),
                               QStringLiteral("circle"));
  if (!guid.isEmpty()) {
    UndoOp op;
    op.created = true;
    op.guid = guid;
    op.snap = snapshotMark(guid);
    pushUndo(op);
  }
  update();
}

void ChartCanvas::dropMarkAtBoat() {
  if (!m_nav_provider) return;
  const OwnShipState s = m_nav_provider->ownShip();
  if (!s.valid) return;
  const QString guid = m_nav_provider->dropMark(
      s.lat, s.lon, datedMarkName(), QString(), QStringLiteral("circle"));
  if (!guid.isEmpty()) {
    UndoOp op;
    op.created = true;
    op.guid = guid;
    op.snap = snapshotMark(guid);
    pushUndo(op);
  }
  update();
}

void ChartCanvas::dropMob() {
  if (!m_nav_provider) return;
  // Drop the MOB mark at the live own-ship fix; fall back to the view centre
  // when there's no fix (wx ActivateMOB drops at gLat/gLon).
  const OwnShipState s = m_nav_provider->ownShip();
  const double lat = s.valid ? s.lat : (m_viewport ? m_viewport->centerLat() : 0);
  const double lon = s.valid ? s.lon : (m_viewport ? m_viewport->centerLon() : 0);
  m_nav_provider->dropMark(lat, lon, tr("MOB"), QString(),
                           QStringLiteral("mob"));
  update();
}

void ChartCanvas::showMark(const QString& guid) {
  if (!m_nav_provider || !m_viewport) return;
  for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
    if (wp.guid != guid) continue;
    m_selected_waypoint_guid = guid;
    if (m_waypoint_layer) m_waypoint_layer->setSelectedWaypointGuid(guid);
    m_viewport->setCenter(wp.lat, wp.lon);  // centre (a point has no extent)
    emit viewChanged();
    update();
    return;
  }
}

void ChartCanvas::setMarkVisible(const QString& guid, bool on) {
  if (m_nav_provider) m_nav_provider->setWaypointVisible(guid, on);
  update();
}

void ChartCanvas::renameMark(const QString& guid, const QString& name) {
  if (m_nav_provider && !name.isEmpty())
    m_nav_provider->renameWaypoint(guid, name);
}

void ChartCanvas::setMarkComment(const QString& guid, const QString& comment) {
  if (m_nav_provider) m_nav_provider->setWaypointComment(guid, comment);
}

void ChartCanvas::setMarkIcon(const QString& guid, const QString& icon) {
  if (m_nav_provider) m_nav_provider->setWaypointIcon(guid, icon);
  update();
}

void ChartCanvas::deleteMark(const QString& guid) {
  if (!m_nav_provider) return;
  UndoOp op;
  op.created = false;
  op.guid = guid;
  op.snap = snapshotMark(guid);
  if (!op.snap.isEmpty()) pushUndo(op);
  m_nav_provider->deleteWaypoint(guid);
  if (guid == m_selected_waypoint_guid) {
    m_selected_waypoint_guid.clear();
    if (m_waypoint_layer) m_waypoint_layer->setSelectedWaypointGuid(QString());
  }
  update();
}

QStringList ChartCanvas::markIconNames() const {
  // The picker list is deduplicated: alias keys sharing one image (the wx
  // vocabulary, kept so persisted marks resolve) show a single tile.
  return pickerIconKeys();
}

// --- Tracks (own-vessel) ----------------------------------------------------

void ChartCanvas::resetTrack() {
  if (m_nav_provider) m_nav_provider->resetTrack();
  update();
}

void ChartCanvas::showTrack(const QString& guid) {
  if (!m_nav_provider) return;
  for (const NavTrack& t : m_nav_provider->tracks()) {
    if (t.guid != guid || t.points.isEmpty()) continue;
    m_selected_track_guid = guid;
    if (m_track_layer) m_track_layer->setSelectedTrackGuid(guid);
    double n = -90, s = 90, e = -180, w = 180;
    for (const QPointF& p : t.points) {  // (lon, lat)
      n = std::max(n, p.y());
      s = std::min(s, p.y());
      e = std::max(e, p.x());
      w = std::min(w, p.x());
    }
    fitBounds(n, s, e, w);
    return;
  }
}

void ChartCanvas::renameTrack(const QString& guid, const QString& name) {
  if (m_nav_provider && !name.isEmpty())
    m_nav_provider->renameTrack(guid, name);
}

void ChartCanvas::deleteTrack(const QString& guid) {
  if (m_nav_provider) m_nav_provider->deleteTrack(guid);
  if (guid == m_selected_track_guid) {
    m_selected_track_guid.clear();
    if (m_track_layer) m_track_layer->setSelectedTrackGuid(QString());
  }
  update();
}

void ChartCanvas::setTrackVisible(const QString& guid, bool on) {
  if (m_nav_provider) m_nav_provider->setTrackVisible(guid, on);
  update();
}

void ChartCanvas::renameRoute(int index, const QString& name) {
  if (m_nav_provider && !name.isEmpty()) m_nav_provider->renameRoute(index, name);
}

void ChartCanvas::deleteRoute(int index) {
  if (m_nav_provider) m_nav_provider->deleteRoute(index);
  clearRouteSelection();
}


}  // namespace ocpn::qtui
