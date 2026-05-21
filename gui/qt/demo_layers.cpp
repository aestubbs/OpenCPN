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
 * Implement demo_layers.h -- two trivial Layer subclasses for the Phase 2
 * scaffold.
 */

#include "demo_layers.h"

#include <QMatrix4x4>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>

namespace ocpn::qtui {

// ----------- DisplayRectLayer -----------

DisplayRectLayer::DisplayRectLayer(QString id, const QRectF& rect,
                                   QColor colour, QObject* parent)
    : Layer(parent), m_id(std::move(id)), m_rect(rect), m_colour(colour) {
  setOwner("core.demo");
}

QSGNode* DisplayRectLayer::updateSubtree(QSGNode* old, QQuickWindow*) {
  // Trivially recreate a fresh simple-rect node. (Real Layers would mutate
  // in place; demo throwaway code does the simplest thing.)
  delete old;
  return new QSGSimpleRectNode(m_rect, m_colour);
}

// ----------- WorldRotatingRectLayer -----------

WorldRotatingRectLayer::WorldRotatingRectLayer(QString id, const QRectF& rect,
                                               QColor colour,
                                               QObject* parent)
    : Layer(parent), m_id(std::move(id)), m_rect(rect), m_colour(colour) {
  setOwner("core.demo");
}

void WorldRotatingRectLayer::setRotationDeg(qreal deg) {
  if (qFuzzyCompare(m_rotation_deg, deg)) return;
  m_rotation_deg = deg;
  Q_EMIT dirty();
}

QSGNode* WorldRotatingRectLayer::updateSubtree(QSGNode* old, QQuickWindow*) {
  // Build (or update) a transform node containing the rect, rotating
  // around the rect's centre. Reusing the existing nodes is just a
  // minor optimisation -- shown here as a pattern for real Layers.
  QSGTransformNode* xform = nullptr;
  QSGSimpleRectNode* rect = nullptr;

  if (old) {
    xform = static_cast<QSGTransformNode*>(old);
    if (xform->childCount() > 0)
      rect = static_cast<QSGSimpleRectNode*>(xform->firstChild());
  }
  if (!xform) {
    xform = new QSGTransformNode();
    rect = new QSGSimpleRectNode(m_rect, m_colour);
    xform->appendChildNode(rect);
  } else if (rect) {
    rect->setRect(m_rect);
    rect->setColor(m_colour);
  }

  QMatrix4x4 m;
  const QPointF c = m_rect.center();
  m.translate(c.x(), c.y());
  m.rotate(m_rotation_deg, 0, 0, 1);
  m.translate(-c.x(), -c.y());
  xform->setMatrix(m);

  return xform;
}

}  // namespace ocpn::qtui
