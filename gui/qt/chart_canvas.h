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
 *   - World-anchored subtree   : QSGTransformNode with the viewport matrix;
 *                                pan/zoom mutates one matrix, the whole
 *                                subtree follows. Hosts: chart tiles, S-52
 *                                vector objects, AIS targets, routes, tracks.
 *   - Display-anchored subtree : identity QSGTransformNode for fixed-screen
 *                                overlays (radar/PPI, range rings, compass
 *                                rose, mini-map).
 *   - QML HUD                  : declarative QML items LAYERED ABOVE the
 *                                ChartCanvas in QML -- not a scene-graph
 *                                node; bound to QObject view-models via
 *                                Q_PROPERTY.
 *
 * This scaffold (P0.4 + P2.1) installs the two top transform nodes. Layer /
 * LayerCompositor (P2.2 / P2.3) plug subtrees in under each.
 */

#ifndef OCPN_QT_CHART_CANVAS_H_
#define OCPN_QT_CHART_CANVAS_H_

#include <QQuickItem>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QSGNode;
class QSGTransformNode;
QT_END_NAMESPACE

namespace ocpn::qtui {

class ChartCanvas : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT

public:
  explicit ChartCanvas(QQuickItem* parent = nullptr);

protected:
  QSGNode* updatePaintNode(QSGNode* old_node,
                           UpdatePaintNodeData* update_data) override;

private:
  // Non-owning pointers into the scene-graph tree we (re)build in
  // updatePaintNode. The tree itself is owned by Qt's scene graph; we just
  // remember the two top transform nodes so future Layer plumbing can find
  // them.
  QSGTransformNode* m_world_anchored_root = nullptr;
  QSGTransformNode* m_display_anchored_root = nullptr;

  // Drives the placeholder world-anchored-rect rotation in the scaffold.
  // Removed when Layer / LayerCompositor populates the subtrees with real
  // content; real Layers schedule their own updates off model signals.
  QTimer m_animation_timer;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_CANVAS_H_
