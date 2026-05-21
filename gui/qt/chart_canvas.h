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
 * ChartCanvas -- the QQuickItem that hosts the new Qt-Quick chart rendering.
 *
 * Three-tier scene-graph (per docs/QT_MIGRATION.md §5 + Phase 2 plan):
 *   - World-anchored subtree   : QSGTransformNode driven by the Viewport's
 *                                world→screen matrix; chart pan/zoom mutates
 *                                this one matrix and the whole subtree
 *                                follows. Hosts chart tiles, S-52 vector
 *                                objects, AIS targets, routes, tracks.
 *   - Display-anchored subtree : identity QSGTransformNode for fixed-screen
 *                                overlays (radar/PPI, range rings, compass
 *                                rose, mini-map).
 *   - QML HUD                  : declarative QML items LAYERED ABOVE the
 *                                ChartCanvas in QML.
 *
 * Owns a `LayerCompositor` that maintains the two anchored subtrees from
 * registered Layer instances, and a `Viewport` that's mutated by the
 * canvas's mouse handlers (drag = pan, wheel = zoom about cursor).
 */

#ifndef OCPN_QT_CHART_CANVAS_H_
#define OCPN_QT_CHART_CANVAS_H_

#include <memory>

#include <QPointF>
#include <QQuickItem>

QT_BEGIN_NAMESPACE
class QSGNode;
class QSGTransformNode;
class QMouseEvent;
class QWheelEvent;
QT_END_NAMESPACE

namespace ocpn::qtui {

class LayerCompositor;
class Viewport;

class ChartCanvas : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT

public:
  explicit ChartCanvas(QQuickItem* parent = nullptr);
  ~ChartCanvas() override;

protected:
  QSGNode* updatePaintNode(QSGNode* old_node,
                           UpdatePaintNodeData* update_data) override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  // Top transform nodes — non-owning pointers into the scene-graph tree
  // (which is owned by Qt's scene graph); the LayerCompositor attaches
  // Layer subtrees under each.
  QSGTransformNode* m_world_anchored_root = nullptr;
  QSGTransformNode* m_display_anchored_root = nullptr;

  std::unique_ptr<LayerCompositor> m_compositor;
  std::unique_ptr<Viewport> m_viewport;

  // Drag state.
  bool m_dragging = false;
  QPointF m_drag_last_pos;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_CANVAS_H_
