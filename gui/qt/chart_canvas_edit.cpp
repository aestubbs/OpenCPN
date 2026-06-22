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
 * Undo/redo stack and the distance/bearing measure tool for ChartCanvas.
 *
 * Part of the ChartCanvas implementation, split out of chart_canvas.cpp. See
 * chart_canvas_internal.h for the shared include block and rationale.
 */

#include "chart_canvas.h"

#include "chart_canvas_internal.h"

namespace ocpn::qtui {

void ChartCanvas::pushUndo(const UndoOp& op) {
  m_undo_stack.append(op);
  while (m_undo_stack.size() > kMaxUndo) m_undo_stack.removeFirst();
  m_redo_stack.clear();
  emit undoChanged();
}

QVariantMap ChartCanvas::snapshotMark(const QString& guid) const {
  if (!m_nav_provider) return {};
  for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
    if (wp.guid != guid) continue;
    QVariantMap snap;
    snap["name"] = wp.name;
    snap["comment"] = wp.comment;
    snap["icon"] = wp.iconName;
    snap["lat"] = wp.lat;
    snap["lon"] = wp.lon;
    return snap;
  }
  return {};
}

QString ChartCanvas::recreateMark(const QVariantMap& snap) {
  if (!m_nav_provider || snap.isEmpty()) return {};
  return m_nav_provider->dropMark(
      snap.value("lat").toDouble(), snap.value("lon").toDouble(),
      snap.value("name").toString(), snap.value("comment").toString(),
      snap.value("icon").toString());
}

void ChartCanvas::undo() {
  if (m_undo_stack.isEmpty() || !m_nav_provider) return;
  UndoOp op = m_undo_stack.takeLast();
  if (op.created) {
    // Undo a creation: delete the mark (still snap-ed for redo).
    m_nav_provider->deleteWaypoint(op.guid);
  } else {
    // Undo a deletion: recreate (fresh GUID -- record it for redo).
    op.guid = recreateMark(op.snap);
  }
  m_redo_stack.append(op);
  emit undoChanged();
  update();
}

void ChartCanvas::redo() {
  if (m_redo_stack.isEmpty() || !m_nav_provider) return;
  UndoOp op = m_redo_stack.takeLast();
  if (op.created) {
    // Redo a creation: recreate it (fresh GUID).
    op.guid = recreateMark(op.snap);
  } else {
    // Redo a deletion: delete again.
    m_nav_provider->deleteWaypoint(op.guid);
  }
  m_undo_stack.append(op);
  emit undoChanged();
  update();
}

void ChartCanvas::startMeasure() {
  if (m_measure_active) return;
  m_measure_active = true;
  m_measure_pts.clear();
  m_measure_text = tr("Click to start measuring");
  if (m_measure_layer) m_measure_layer->setState({}, QPointF(), false);
  emit measureChanged();
  update();
}

void ChartCanvas::stopMeasure() {
  if (!m_measure_active) return;
  m_measure_active = false;
  m_measure_pts.clear();
  m_measure_text.clear();
  if (m_measure_layer) m_measure_layer->setState({}, QPointF(), false);
  emit measureChanged();
  update();
}

void ChartCanvas::updateMeasure(double cur_lat, double cur_lon,
                                bool has_cursor) {
  if (!m_measure_active) return;
  // Total over the fixed legs, plus the live rubber-band leg to the cursor.
  double total = 0.0;
  for (int i = 1; i < m_measure_pts.size(); ++i) {
    double brg = 0.0, dist = 0.0;
    DistanceBearingMercator(m_measure_pts[i].y(), m_measure_pts[i].x(),
                            m_measure_pts[i - 1].y(), m_measure_pts[i - 1].x(),
                            &brg, &dist);
    total += dist;
  }
  DisplayConfig& dc = DisplayConfig::instance();
  if (has_cursor && !m_measure_pts.isEmpty()) {
    const QPointF& last = m_measure_pts.last();
    double brg = 0.0, dist = 0.0;
    DistanceBearingMercator(cur_lat, cur_lon, last.y(), last.x(), &brg, &dist);
    m_measure_text = tr("Leg %1: %2  %3   ·   Total %4")
                         .arg(m_measure_pts.size())
                         .arg(dc.formatBearing(brg), dc.formatDistance(dist),
                              dc.formatDistance(total + dist));
  } else if (m_measure_pts.size() >= 2) {
    m_measure_text = tr("Total %1").arg(dc.formatDistance(total));
  } else {
    m_measure_text = tr("Click the next point");
  }
  if (m_measure_layer)
    m_measure_layer->setState(m_measure_pts, QPointF(cur_lon, cur_lat),
                              has_cursor);
  emit measureChanged();
  update();
}


}  // namespace ocpn::qtui
