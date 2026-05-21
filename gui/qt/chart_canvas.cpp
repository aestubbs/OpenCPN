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
 * Implement chart_canvas.h -- ChartCanvas QQuickItem scaffold.
 *
 * For the P0.4 + P2.1 scaffold, each top transform node hosts a single
 * coloured QSGSimpleRectNode so we can visually confirm the wiring:
 *   - World-anchored: a slowly-rotating red rect (the transform is exercised
 *                     by ChartCanvas::updatePaintNode so we can see "pan/zoom
 *                     would change one matrix" working end to end).
 *   - Display-anchored: a fixed blue rect (identity transform).
 *
 * These placeholder rects are removed when Layer / LayerCompositor (P2.2 /
 * P2.3) start populating the subtrees with real content.
 */

#include "chart_canvas.h"

#include <QColor>
#include <QDateTime>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>

namespace ocpn::qtui {

ChartCanvas::ChartCanvas(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  // Drives the placeholder world-anchored-rect rotation. Calls update() from
  // the main thread (QQuickItem::update() is thread-affine), which marks the
  // item dirty so updatePaintNode() runs on the next frame. ~60 FPS.
  //
  // Real Layers will not poll like this -- they schedule update() in response
  // to model signals (AisDecoder::info_update etc.). Remove with the
  // placeholder rects.
  connect(&m_animation_timer, &QTimer::timeout, this, [this]() { update(); });
  m_animation_timer.start(16);
}

QSGNode* ChartCanvas::updatePaintNode(QSGNode* old_node,
                                      UpdatePaintNodeData* /*update_data*/) {
  QSGNode* root = old_node;
  if (!root) {
    root = new QSGNode();

    m_world_anchored_root = new QSGTransformNode();
    root->appendChildNode(m_world_anchored_root);

    m_display_anchored_root = new QSGTransformNode();
    root->appendChildNode(m_display_anchored_root);

    // Placeholder children -- removed when Layer/LayerCompositor (P2.2/P2.3)
    // plug real subtrees in.
    auto* world_rect = new QSGSimpleRectNode(QRectF(60, 60, 120, 120),
                                             QColor(200, 60, 60));
    m_world_anchored_root->appendChildNode(world_rect);

    auto* display_rect = new QSGSimpleRectNode(QRectF(20, 20, 80, 80),
                                               QColor(60, 60, 200));
    m_display_anchored_root->appendChildNode(display_rect);
  }

  // Exercise the world-anchored transform with a tiny rotation around the
  // canvas centre, sourced from the wall clock. Lets us see the world-vs-
  // display behaviour immediately. (Remove with the placeholder rects.)
  QMatrix4x4 m;
  const QPointF c = boundingRect().center();
  m.translate(c.x(), c.y());
  const double seconds = QDateTime::currentMSecsSinceEpoch() / 1000.0;
  m.rotate(std::fmod(seconds * 30.0, 360.0), 0, 0, 1);
  m.translate(-c.x(), -c.y());
  if (m_world_anchored_root) m_world_anchored_root->setMatrix(m);

  return root;
}

}  // namespace ocpn::qtui
