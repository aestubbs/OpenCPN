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
 * Implement chart_canvas.h -- ChartCanvas QQuickItem with a LayerCompositor.
 *
 * The canvas only owns the two top transform nodes; the LayerCompositor
 * (P2.3) populates them from registered Layer instances (P2.2). For the
 * scaffold we register two demo Layers (a rotating world-anchored rect,
 * a fixed display-anchored rect) that exercise the full plumbing.
 */

#include "chart_canvas.h"

#include <cmath>

#include <QColor>
#include <QDateTime>
#include <QRectF>
#include <QSGNode>
#include <QSGTransformNode>

#include "demo_layers.h"
#include "layer_compositor.h"

namespace ocpn::qtui {

ChartCanvas::ChartCanvas(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);

  m_compositor = std::make_unique<LayerCompositor>();

  // Demo Layers -- removed when real Layers (raster chart, S52, AIS,
  // routes) replace them. The rotating world-anchored rect exercises
  // the Layer::dirty() → compositor → ChartCanvas::update() path; the
  // fixed display-anchored rect exercises the second transform tier.
  m_demo_rotating = new WorldRotatingRectLayer(
      "demo.world.rotating-red",
      QRectF(60, 60, 120, 120),
      QColor(200, 60, 60));
  m_compositor->addLayer(m_demo_rotating);

  m_compositor->addLayer(new DisplayRectLayer(
      "demo.display.fixed-blue",
      QRectF(20, 20, 80, 80),
      QColor(60, 60, 200)));

  // Drive the demo rect's rotation from a main-thread timer. Real Layers
  // schedule updates off model signals (AisDecoder::info_update etc.) --
  // no polling needed.
  connect(&m_animation_timer, &QTimer::timeout, this, [this]() {
    const double seconds = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    m_demo_rotating->setRotationDeg(std::fmod(seconds * 30.0, 360.0));
  });
  m_animation_timer.start(16);

  // Any composition change (dirty Layer, z-order shuffle, add/remove)
  // schedules a paint-node update.
  connect(m_compositor.get(), &LayerCompositor::changed, this,
          [this]() { update(); });
}

ChartCanvas::~ChartCanvas() = default;

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

  // World-anchored root's transform stays identity for now -- when the
  // chart viewport (pan/zoom) ships, it mutates this one matrix. The
  // visible rotation comes from the demo Layer, not from this root.
  m_compositor->syncToScene(m_world_anchored_root, m_display_anchored_root,
                            window());

  return root;
}

}  // namespace ocpn::qtui
