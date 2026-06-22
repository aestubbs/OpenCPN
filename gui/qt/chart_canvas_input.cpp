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
 * Scene-graph paint node, pointer/key/wheel input and object picking for ChartCanvas.
 *
 * Part of the ChartCanvas implementation, split out of chart_canvas.cpp. See
 * chart_canvas_internal.h for the shared include block and rationale.
 */

#include "chart_canvas.h"

#include "chart_canvas_internal.h"

namespace ocpn::qtui {

QSGNode* ChartCanvas::updatePaintNode(QSGNode* old_node,
                                      UpdatePaintNodeData* /*update_data*/) {
  QSGNode* root = old_node;
  if (!root) {
    root = new QSGNode();
    m_world_anchored_root = new QSGTransformNode();
    root->appendChildNode(m_world_anchored_root);
    m_display_anchored_root = new QSGTransformNode();
    root->appendChildNode(m_display_anchored_root);
  }

  // World-anchored root: the Viewport transform. Pan/zoom mutates this
  // one matrix and all world-anchored Layer subtrees follow.
  if (m_world_anchored_root) {
    m_world_anchored_root->setMatrix(
        m_viewport->transformMatrix(static_cast<int>(width()),
                                    static_cast<int>(height())));
  }
  // Display-anchored root stays identity.

  m_compositor->syncToScene(m_world_anchored_root, m_display_anchored_root,
                            window());

  // Debug perf readout: time between paints -> smoothed fps + last frame ms.
  // Measured here on the render thread; published to the GUI thread (throttled)
  // so the QML HUD can bind to perfText. Only meaningful while frames are
  // actually being produced (idle = render-on-demand, so it freezes).
  if (m_frame_clock.isValid()) {
    const double dt_ms = m_frame_clock.nsecsElapsed() / 1.0e6;
    m_frame_clock.restart();
    if (dt_ms > 0.0) {
      const double inst = 1000.0 / dt_ms;
      m_fps_smooth =
          m_fps_smooth > 0.0 ? (0.85 * m_fps_smooth + 0.15 * inst) : inst;
      if (++m_frame_count % 10 == 0) {  // publish ~once per 10 frames
        const QString txt = QString::asprintf("%.0f fps  %.1f ms",
                                              m_fps_smooth, dt_ms);
        QMetaObject::invokeMethod(
            this,
            [this, txt]() {
              if (m_perf_text == txt) return;
              m_perf_text = txt;
              emit perfTextChanged();
            },
            Qt::QueuedConnection);
      }
    }
  } else {
    m_frame_clock.start();
  }
  return root;
}

void ChartCanvas::mousePressEvent(QMouseEvent* event) {
  // Clicking the chart claims keyboard focus for the canvas key layer
  // (P3.20) -- e.g. back from a settings text field.
  forceActiveFocus();
  // Route-building mode (Create Route). Left-press begins a gesture that is a
  // PAN if the cursor moves, or places a vertex if it stays put -- decided on
  // release (reusing the click/drag threshold) so the chart stays fully
  // pannable (and wheel-zoomable) while drawing. Right finishes the route.
  if (m_route_build_mode && m_nav_provider) {
    if (event->button() == Qt::LeftButton) {
      m_dragging = true;
      m_drag_last_pos = event->position();
      m_press_pos = event->position();
    } else if (event->button() == Qt::RightButton) {
      m_nav_provider->finishRoute();
      m_route_build_mode = false;
      emit routeBuildModeChanged();
      update();
    }
    event->accept();
    return;
  }

  if (event->button() == Qt::LeftButton) {
    // Route editing: grabbing a node selects its route and starts a drag.
    int rt = -1, nd = -1;
    if (m_route_edit_mode && hitRouteNode(event->position(), rt, nd)) {
      selectRoute(rt);
      m_dragging_node = true;
      m_drag_node = nd;
      event->accept();
      return;
    }
    m_dragging = true;
    m_drag_last_pos = event->position();
    m_press_pos = event->position();
    event->accept();
  } else if (event->button() == Qt::RightButton) {
    // Record the world point under the cursor first -- every menu's actions
    // (drop mark / navigate-to / insert point / center here) consume it.
    m_ctx_pos = event->position();
    if (m_viewport)
      m_viewport->screenToLatLon(m_ctx_pos.x(), m_ctx_pos.y(),
                                 static_cast<int>(width()),
                                 static_cast<int>(height()), m_ctx_lat,
                                 m_ctx_lon);

    // Right-click on a route node in edit mode -> the node menu (delete
    // point/route) takes precedence over the object menus.
    int rt = -1, nd = -1;
    if (m_route_edit_mode && hitRouteNode(event->position(), rt, nd)) {
      selectRoute(rt);
      m_menu_route = rt;
      m_menu_node = nd;
      emit routeNodeMenuRequested(event->position().x(),
                                    event->position().y());
      event->accept();
      return;
    }

    // Object-focused menus (P3.18, wx CanvasMenuHandler): an AIS target,
    // then a mark, then a route node / segment, else the general menu.
    int ais_mmsi = 0;
    QString ais_name;
    if (hitAisAt(event->position(), &ais_mmsi, &ais_name)) {
      emit aisMenuRequested(event->position().x(), event->position().y(),
                              ais_mmsi, ais_name);
      event->accept();
      return;
    }
    QString wp_guid, wp_name;
    if (hitWaypointAt(event->position(), &wp_guid, &wp_name)) {
      emit markMenuRequested(event->position().x(), event->position().y(),
                               wp_guid, wp_name);
      event->accept();
      return;
    }
    int seg = -1;
    double ilat = 0, ilon = 0;
    if (hitRouteNode(event->position(), rt, nd)) {
      selectRoute(rt);
      m_menu_route = rt;
      m_menu_seg = -1;
      const bool act = m_nav_provider &&
                       m_nav_provider->userRoutes().value(rt).active;
      emit routeMenuRequested(event->position().x(), event->position().y(),
                                rt, act, false);
      event->accept();
      return;
    }
    if (hitRouteSegment(event->position(), rt, seg, ilat, ilon)) {
      selectRoute(rt);
      m_menu_route = rt;
      m_menu_seg = seg;
      m_menu_ins_lat = ilat;
      m_menu_ins_lon = ilon;
      const bool act = m_nav_provider &&
                       m_nav_provider->userRoutes().value(rt).active;
      emit routeMenuRequested(event->position().x(), event->position().y(),
                                rt, act, true);
      event->accept();
      return;
    }
    QString trk_guid, trk_name;
    if (hitTrackAt(event->position(), &trk_guid, &trk_name)) {
      emit trackMenuRequested(event->position().x(), event->position().y(),
                                trk_guid, trk_name);
      event->accept();
      return;
    }

    emit contextMenuRequested(m_ctx_pos.x(), m_ctx_pos.y());
    event->accept();
  } else {
    QQuickItem::mousePressEvent(event);
  }
}

void ChartCanvas::centerViewHere() {
  if (!m_viewport) return;
  m_viewport->setCenter(m_ctx_lat, m_ctx_lon);
  emit viewChanged();
  update();
}

void ChartCanvas::setAisTrail(int mmsi, bool on) {
  if (m_ais_layer) m_ais_layer->setTrailEnabled(mmsi, on);
  update();
}

bool ChartCanvas::aisTrailEnabled(int mmsi) const {
  return m_ais_layer && m_ais_layer->trailEnabled(mmsi);
}

void ChartCanvas::queryObjectsHere() {
  // Populate the object-query view-model from the chart objects under the
  // right-click point; the QML object-query window binds to it.
  pickObjectsAt(m_ctx_pos);
}

void ChartCanvas::mouseMoveEvent(QMouseEvent* event) {
  if (m_dragging_node && m_nav_provider) {
    double lat = 0, lon = 0;
    const QPointF p = event->position();
    m_viewport->screenToLatLon(p.x(), p.y(), static_cast<int>(width()),
                               static_cast<int>(height()), lat, lon);
    m_nav_provider->moveRoutePoint(m_selected_route, m_drag_node, lat, lon);
    checkEdgePan(p);  // dragging a node toward an edge scrolls the view
    event->accept();
    return;
  }
  if (m_dragging) {
    const QPointF pos = event->position();
    const QPointF delta = pos - m_drag_last_pos;
    m_drag_last_pos = pos;
    m_viewport->panBy(delta.x(), delta.y());
    event->accept();
    return;
  }
  QQuickItem::mouseMoveEvent(event);
}

void ChartCanvas::mouseReleaseEvent(QMouseEvent* event) {
  // Route-building: a left-release that barely moved places a vertex; one that
  // moved was a pan (already applied live in mouseMoveEvent) -- no vertex.
  if (m_route_build_mode && event->button() == Qt::LeftButton) {
    const bool was_press = m_dragging;
    m_dragging = false;
    const QPointF d = event->position() - m_press_pos;
    if (was_press && d.manhattanLength() <= 6 && m_nav_provider && m_viewport) {
      double lat = 0, lon = 0;
      m_viewport->screenToLatLon(event->position().x(), event->position().y(),
                                 static_cast<int>(width()),
                                 static_cast<int>(height()), lat, lon);
      // Nearby-waypoint snap (P3.13, wx GetNearbyWaypoint): clicking within
      // pick range of an existing mark places the vertex exactly on it.
      QString snap_guid;
      if (hitWaypointAt(event->position(), &snap_guid, nullptr)) {
        for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
          if (wp.guid == snap_guid) {
            lat = wp.lat;
            lon = wp.lon;
            break;
          }
        }
      }
      m_nav_provider->addRoutePoint(lat, lon);
    }
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && m_dragging_node) {
    m_dragging_node = false;
    m_drag_node = -1;
    if (m_nav_provider) m_nav_provider->commitRouteEdit();  // manager refresh
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && m_dragging) {
    m_dragging = false;
    // A press+release that barely moved is a click. Route interactions take
    // precedence: click a segment of the selected route to insert a point,
    // click another route's line to select it, else AIS pick / deselect.
    const QPointF d = event->position() - m_press_pos;
    if (d.manhattanLength() <= 6) {
      // Measure mode (P3.18): a click drops a measure point; everything else
      // (selection, AIS pick) is suspended while measuring.
      if (m_measure_active && m_viewport) {
        double mlat = 0, mlon = 0;
        m_viewport->screenToLatLon(event->position().x(),
                                   event->position().y(),
                                   static_cast<int>(width()),
                                   static_cast<int>(height()), mlat, mlon);
        m_measure_pts.append(QPointF(mlon, mlat));
        updateMeasure(mlat, mlon, false);
        event->accept();
        return;
      }
      int rt = -1, seg = -1;
      double ilat = 0, ilon = 0;
      if (hitRouteSegment(event->position(), rt, seg, ilat, ilon)) {
        if (m_route_edit_mode && rt == m_selected_route && m_nav_provider)
          m_nav_provider->insertRoutePoint(rt, seg, ilat, ilon);
        else
          selectRoute(rt);
      } else if (!pickAisAt(event->position())) {
        // Tides on? try a tide/current station before deselecting.
        if (!(DisplayConfig::instance().showTides() &&
              pickTideStationAt(event->position())))
          clearRouteSelection();  // clicked empty water -> leave edit
      }
    }
    event->accept();
  } else {
    QQuickItem::mouseReleaseEvent(event);
  }
}

void ChartCanvas::checkEdgePan(const QPointF& pos) {
  // 5%-margin band, 2%-of-dimension step per tick (wx CheckEdgePan(…,5,2)).
  const double w = width(), h = height();
  if (w <= 0 || h <= 0) return;
  const double mx = w * 0.05, my = h * 0.05;
  QPointF step(0, 0);
  // panBy takes a mouse-drag delta: cursor at the LEFT edge should reveal
  // more west = drag right = +x.
  if (pos.x() < mx)
    step.setX(w * 0.02);
  else if (pos.x() > w - mx)
    step.setX(-w * 0.02);
  if (pos.y() < my)
    step.setY(h * 0.02);
  else if (pos.y() > h - my)
    step.setY(-h * 0.02);
  if (step.isNull()) {
    m_edge_pan_timer->stop();
    return;
  }
  m_edge_pan_step = step;
  if (!m_edge_pan_timer->isActive()) m_edge_pan_timer->start();
}

void ChartCanvas::keyPressEvent(QKeyEvent* event) {
  // Arrow-pan step (logical px), matching a comfortable wx scroll notch.
  constexpr double kPanStep = 80.0;
  if (!m_viewport) {
    QQuickItem::keyPressEvent(event);
    return;
  }
  switch (event->key()) {
    // panBy takes a mouse-drag delta: dragging right reveals west, dragging
    // down reveals north -- so an arrow pans the VIEW toward its direction.
    case Qt::Key_Left:
      m_viewport->panBy(kPanStep, 0);
      break;
    case Qt::Key_Right:
      m_viewport->panBy(-kPanStep, 0);
      break;
    case Qt::Key_Up:
      m_viewport->panBy(0, kPanStep);
      break;
    case Qt::Key_Down:
      m_viewport->panBy(0, -kPanStep);
      break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      zoomIn();
      break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
      zoomOut();
      break;
    case Qt::Key_Z:
      // Cmd/Ctrl-Z undo, Shift-Cmd/Ctrl-Z redo (P3.18 tier 4).
      if (event->modifiers() & Qt::ControlModifier) {
        (event->modifiers() & Qt::ShiftModifier) ? redo() : undo();
        break;
      }
      QQuickItem::keyPressEvent(event);
      return;
    case Qt::Key_M:  // wx: M / F4 toggles the measure tool
    case Qt::Key_F4:
      m_measure_active ? stopMeasure() : startMeasure();
      break;
    case Qt::Key_Escape:
      // Cancel the transient mode, most specific first (wx parity).
      if (m_measure_active) {
        stopMeasure();
      } else if (m_route_build_mode) {
        setRouteBuildMode(false);  // discards the draft
      } else {
        clearRouteSelection();
      }
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      // Finish the route being built (same as the right-click finish).
      if (m_route_build_mode && m_nav_provider) {
        m_nav_provider->finishRoute();
        m_route_build_mode = false;
        emit routeBuildModeChanged();
        update();
      }
      break;
    default:
      QQuickItem::keyPressEvent(event);
      return;
  }
  event->accept();
}

void ChartCanvas::hoverMoveEvent(QHoverEvent* event) {
  if (m_viewport) {
    double lat = 0, lon = 0;
    const QPointF p = event->position();
    m_viewport->screenToLatLon(p.x(), p.y(), static_cast<int>(width()),
                               static_cast<int>(height()), lat, lon);
    m_cursor_text = DisplayConfig::instance().formatLatLon(lat, lon);
    // Bearing + range from own ship to the cursor (wx STAT_FIELD_CURSOR_BRGRNG).
    m_cursor_brgrng_text.clear();
    if (m_nav_provider) {
      const OwnShipState s = m_nav_provider->ownShip();
      if (s.valid) {
        double brg = 0.0, rng = 0.0;
        DistanceBearingMercator(lat, lon, s.lat, s.lon, &brg, &rng);
        DisplayConfig& dc = DisplayConfig::instance();
        m_cursor_brgrng_text =
            dc.formatBearing(brg) + QStringLiteral("  ") + dc.formatDistance(rng);
      }
    }
    m_cursor_pos_lat = lat;
    m_cursor_pos_lon = lon;
    emit cursorMoved();
    // Live rubber-band segment to the cursor while drawing a route.
    if (m_route_build_mode && m_nav_provider)
      m_nav_provider->setRouteRubberband(lat, lon);
    // Measure tool (P3.18): trail the dashed rubber-band + live readout.
    if (m_measure_active && !m_measure_pts.isEmpty())
      updateMeasure(lat, lon, true);
    // Edge auto-pan while building / measuring (P3.13).
    if (m_route_build_mode || m_measure_active)
      checkEdgePan(p);
    else if (m_edge_pan_timer->isActive())
      m_edge_pan_timer->stop();
  }
  QQuickItem::hoverMoveEvent(event);
}

void ChartCanvas::setRouteBuildMode(bool on) {
  if (on == m_route_build_mode) return;
  m_route_build_mode = on;
  if (m_nav_provider) {
    if (on)
      m_nav_provider->beginRoute();  // start a fresh draft
    else
      m_nav_provider->cancelRoute();  // toggled off -> discard draft
  }
  emit routeBuildModeChanged();
  update();
}

void ChartCanvas::setColorScheme(int scheme) {
  if (scheme < 0 || scheme > 2 || scheme == m_color_scheme) return;
  m_color_scheme = scheme;
  ConfigStore::instance().setInt("display/colorScheme", scheme);

  // Re-tint the world basemap + route lines immediately (cheap rebuilds).
  if (m_basemap) m_basemap->setColorScheme(scheme);
  if (m_route_layer) m_route_layer->setColorScheme(scheme);

  // S-52 cells bake their colours in at decode time, so switch the palette on
  // the decode thread (queued before the loadCell re-requests so the worker
  // applies it first), then re-decode the resident cells.
  if (m_worker) {
    QMetaObject::invokeMethod(m_worker, "setColorScheme", Qt::QueuedConnection,
                              Q_ARG(int, scheme));
    reloadResidentCells();
  }

  emit colorSchemeChanged();
  update();
}

void ChartCanvas::applyChartConfig() {
  if (!m_worker) return;
  const ChartConfig& c = ChartConfig::instance();
  ChartDisplaySettings s;
  s.importantTextOnly = c.importantTextOnly();
  s.useScamin = c.reducedDetailSmallScale();
  s.symbolStyle = c.graphicsStyle();
  s.boundaryStyle = c.boundaryStyle();
  s.twoShades = c.colourCount();
  s.safetyContour = c.safetyContour();
  s.shallowContour = c.shallowContour();
  s.deepContour = c.deepContour();
  m_height_unit = DisplayConfig::instance().heightUnit();
  s.heightUnit = m_height_unit;
  m_depth_unit = DisplayConfig::instance().depthUnit();
  s.depthUnit = m_depth_unit;
  s.chartInfoObjects = c.chartInfoObjects();          // P2.16
  s.dataQuality = c.dataQuality();                    // CATZOC overlay
  s.buoyLightLabels = c.buoyLightLabels();            // P2.16
  s.lightDescriptions = c.lightDescriptions();        // P2.16
  s.extendedLightSectors = c.extendedLightSectors();  // P2.16
  s.nationalText = c.nationalText();                  // P2.16
  s.declutterText = c.declutterText();                // P2.16
  s.superScamin = c.superScamin();                    // P2.16
  // Apply on the decode thread (queued before the re-requests), then re-decode
  // resident cells so the new symbology/shading bakes in.
  QMetaObject::invokeMethod(m_worker, "applyDisplaySettings",
                            Qt::QueuedConnection,
                            Q_ARG(ocpn::qtui::ChartDisplaySettings, s));
  reloadResidentCells();
  update();
}

// Evict every resident S-52 cell layer and re-run the quilt so they re-decode
// with the current global s52plib settings (palette / display options). Shared
// by setColorScheme and applyChartConfig.
void ChartCanvas::reloadResidentCells() {
  QList<QString> resident;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().extent.valid()) resident.append(it.key());  // skip demo
  for (const QString& name : resident) {
    m_compositor->removeLayer(m_loaded.value(name).layerId);
    m_loaded.remove(name);
    m_requested.remove(name);
  }
  m_needed.clear();
  // New settings/palette: a cell that was content-less before may now render
  // (e.g. SCAMIN off), so forget the known-empty set and let it re-decode. The
  // worker flushes its decoded-buffer cache on the same setting change.
  m_known_empty.clear();
  if (!m_catalog.isEmpty()) m_load_debounce->start();
}

void ChartCanvas::setTrackRecording(bool on) {
  if (on == m_track_recording) return;
  m_track_recording = on;
  if (m_nav_provider) m_nav_provider->setRecordingTrack(on);
  emit trackRecordingChanged();
  update();
}

bool ChartCanvas::pickAisAt(const QPointF& screen_pos) {
  if (!m_ais_selection || !m_nav_provider || !m_viewport) return false;
  // World->screen transform the canvas applies to the world-anchored root,
  // so target screen px = transform.map(world(lon, -lat)).
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  constexpr double kPickRadiusPx = 14.0;
  double best = kPickRadiusPx * kPickRadiusPx;
  const AisTarget* hit = nullptr;
  const QList<AisTarget> targets = m_nav_provider->aisTargets();
  for (const AisTarget& t : targets) {
    const QPointF sp = m.map(QPointF(t.lon, Viewport::latToWorldY(t.lat)));
    const double dx = sp.x() - screen_pos.x();
    const double dy = sp.y() - screen_pos.y();
    const double d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      hit = &t;
    }
  }
  if (hit) {
    m_ais_selection->select(*hit);
    return true;
  }
  m_ais_selection->clear();
  return false;
}

bool ChartCanvas::pickTideStationAt(const QPointF& screen_pos) {
  if (!m_tide_graph || !m_viewport || !ptcmgr || !ptcmgr->IsReady())
    return false;
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  constexpr double kPickRadiusPx = 13.0;
  double best = kPickRadiusPx * kPickRadiusPx;
  int hit = -1;
  for (int i = 0; i <= ptcmgr->Get_max_IDX(); ++i) {
    const IDX_entry* e = ptcmgr->GetIDX_entry(i);
    if (!e || !e->IDX_Useable) continue;
    const char ty = e->IDX_type;
    if (ty != 't' && ty != 'T' && ty != 'c' && ty != 'C') continue;
    // Skip the deeper records of multi-depth current stations so the pick (and
    // hence the graph) lands on the shallowest, most-usable record -- matching
    // what TideLayer draws and the legacy wx RebuildCurrentSelectList.
    if ((ty == 'c' || ty == 'C') && e->b_skipTooDeep) continue;
    const QPointF sp =
        m.map(QPointF(e->IDX_lon, Viewport::latToWorldY(e->IDX_lat)));
    const double dx = sp.x() - screen_pos.x();
    const double dy = sp.y() - screen_pos.y();
    const double d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      hit = i;
    }
  }
  if (hit >= 0) {
    m_tide_graph->select(hit);
    return true;
  }
  return false;
}

void ChartCanvas::pickObjectsAt(const QPointF& screen_pos) {
  if (!m_object_query || !m_viewport) return;
  double lat = 0, lon = 0;
  m_viewport->screenToLatLon(screen_pos.x(), screen_pos.y(),
                             static_cast<int>(width()),
                             static_cast<int>(height()), lat, lon);
  // ~10px pick radius in degrees at the current zoom (px/degree).
  const double margin =
      m_viewport->scale() > 0.0 ? 10.0 / m_viewport->scale() : 0.0;
  // The quilt loads several overlapping cells (usage bands), each holding the
  // same features, so de-duplicate by LNAM (the S-57 unique feature id) --
  // the same real-world object across cells collapses to one result.
  QList<s52sg::QueryObject> found;
  QSet<QString> seen;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it) {
    if (!it.value().provider) continue;
    for (const s52sg::QueryObject& qo :
         it.value().provider->objectsAt(lat, lon, margin)) {
      QString lnam;
      for (const s52sg::QueryAttr& a : qo.attrs)
        if (a.name == QLatin1String("LNAM")) {
          lnam = a.value;
          break;
        }
      if (!lnam.isEmpty()) {
        if (seen.contains(lnam)) continue;  // same feature, another cell
        seen.insert(lnam);
      }
      found.append(qo);
    }
  }
  m_object_query->setObjects(found);
}

// --- Overlay layer visibility (P3.7) ----------------------------------------
namespace {
bool layerVisible(LayerCompositor* c, const char* id) {
  Layer* l = c ? c->layer(QString::fromLatin1(id)) : nullptr;
  return l ? l->visible() : true;
}
void setLayerVisible(LayerCompositor* c, const char* id, bool on) {
  if (Layer* l = c ? c->layer(QString::fromLatin1(id)) : nullptr)
    l->setVisible(on);
}
}  // namespace

bool ChartCanvas::showRoutes() const {
  return layerVisible(m_compositor.get(), "core.routes");
}
void ChartCanvas::setShowRoutes(bool on) {
  setLayerVisible(m_compositor.get(), "core.routes", on);
  emit overlayVisibilityChanged();
  update();
}
bool ChartCanvas::showTracks() const {
  return layerVisible(m_compositor.get(), "core.tracks");
}
void ChartCanvas::setShowTracks(bool on) {
  setLayerVisible(m_compositor.get(), "core.tracks", on);
  emit overlayVisibilityChanged();
  update();
}
bool ChartCanvas::showWaypoints() const {
  return layerVisible(m_compositor.get(), "core.waypoints");
}
void ChartCanvas::setShowWaypoints(bool on) {
  setLayerVisible(m_compositor.get(), "core.waypoints", on);
  emit overlayVisibilityChanged();
  update();
}

QString ChartCanvas::scaleText() const {
  if (!m_viewport || m_viewport->scale() <= 0.0) return QString();
  const double n = displayScaleN(m_viewport->scale(), m_viewport->centerLat());
  return QStringLiteral("1:%1").arg(static_cast<qlonglong>(n));
}

void ChartCanvas::setFollowOwnShip(bool on) {
  if (on == m_follow_own_ship) return;
  m_follow_own_ship = on;
  if (on && m_nav_provider) {  // jump to the ship immediately
    const OwnShipState s = m_nav_provider->ownShip();
    if (s.valid) m_viewport->setCenter(s.lat, s.lon);
  }
  emit followOwnShipChanged();
  update();
}

double ChartCanvas::chartRotationDeg() const {
  return m_viewport ? m_viewport->rotation() * 180.0 / M_PI : 0.0;
}

void ChartCanvas::updateChartRotation() {
  if (!m_viewport) return;
  const int mode = DisplayConfig::instance().navMode();  // 0 N, 1 Course, 2 Head
  if (mode == 0) {  // North-Up
    m_cog_avg_valid = false;
    m_viewport->setRotation(0.0);
    emit viewChanged();
    return;
  }
  const OwnShipState s =
      m_nav_provider ? m_nav_provider->ownShip() : OwnShipState{};
  if (!s.valid) return;  // keep the last rotation until a fix arrives

  double heading_deg = -1.0;
  if (mode == 2) {  // Head-Up: live heading (fall back to COG if no HDT)
    heading_deg = (s.hdg < 360.0) ? s.hdg : s.cog;
    m_cog_avg_valid = false;
  } else {  // Course-Up: circular-smoothed COG over the averaging window
    const double tau = DisplayConfig::instance().chartRotationAveraging();
    if (tau <= 0.0 || !m_cog_avg_valid) {
      m_cog_avg = s.cog;
      m_cog_avg_valid = true;
    } else {
      // Assume ~1 Hz fixes: alpha ~ 1/tau, clamped. Shortest-arc blend.
      const double alpha = std::clamp(1.0 / tau, 0.02, 1.0);
      double diff = s.cog - m_cog_avg;
      while (diff > 180.0) diff -= 360.0;
      while (diff < -180.0) diff += 360.0;
      m_cog_avg += alpha * diff;
      while (m_cog_avg >= 360.0) m_cog_avg -= 360.0;
      while (m_cog_avg < 0.0) m_cog_avg += 360.0;
    }
    heading_deg = m_cog_avg;
  }
  if (heading_deg < 0.0) return;
  // Rotate the chart so the heading/course points up (mirrors wx: rotation =
  // -heading). The viewport stores radians.
  m_viewport->setRotation(-heading_deg * M_PI / 180.0);
  emit viewChanged();
}

void ChartCanvas::fitBounds(double north, double south, double east,
                            double west) {
  if (!m_viewport) return;
  // Pad a near-zero span (single waypoint) so a "zoom to" lands at a sensible
  // harbour scale rather than infinite zoom.
  constexpr double kMinSpan = 0.05;  // degrees
  if (north - south < kMinSpan) {
    const double c = (north + south) / 2.0;
    north = c + kMinSpan / 2.0;
    south = c - kMinSpan / 2.0;
  }
  if (east - west < kMinSpan) {
    const double c = (east + west) / 2.0;
    east = c + kMinSpan / 2.0;
    west = c - kMinSpan / 2.0;
  }
  m_viewport->setCenter((north + south) / 2.0, (east + west) / 2.0);
  const double w = width() > 0 ? width() : 1000.0;
  const double h = height() > 0 ? height() : 700.0;
  const double sLon = w / ((east - west) * 1.25);
  const double sLat = h / ((north - south) * 1.25);
  m_viewport->setScale(std::min(sLon, sLat));
  update();
}

void ChartCanvas::wheelEvent(QWheelEvent* event) {
  // Vertical wheel: zoom. 120 units = one notch on a normal mouse wheel.
  // Trackpads use smaller pixelDelta values; both feed into angleDelta.
  const int deg = event->angleDelta().y() / 8;  // degrees (eighths)
  if (deg == 0) {
    QQuickItem::wheelEvent(event);
    return;
  }
  // Each notch (15°) zooms by the user's wheel-zoom factor (Display options;
  // 1.1 gentle .. 2.0 brisk). The exponent normalises trackpad/fine scrolls.
  const double per_notch =
      std::max(1.01, DisplayConfig::instance().wheelZoomFactor());
  const double factor = std::pow(per_notch, deg / 15.0);
  const QPointF p = event->position();
  m_viewport->zoomAt(p.x(), p.y(), factor,
                     static_cast<int>(width()),
                     static_cast<int>(height()));
  event->accept();
}


}  // namespace ocpn::qtui
