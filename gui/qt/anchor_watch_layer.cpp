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
 * Implement anchor_watch_layer.h.
 */

#include "anchor_watch_layer.h"

#include <QMatrix4x4>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGTransformNode>

#include <cmath>

#include "aa_line.h"
#include "alert_engine.h"
#include "viewport.h"

namespace ocpn::qtui {

namespace {
const QColor kWatchColor(230, 170, 30);   // amber -- inside the circle
const QColor kBreachColor(230, 40, 40);   // red -- dragging
constexpr float kWatchPx = 1.5f;          // line width (screen-fixed)
constexpr int kSegments = 72;             // circle tessellation
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
}  // namespace

AnchorWatchLayer::AnchorWatchLayer(const AlertEngine* engine, QObject* parent)
    : Layer(parent), m_engine(engine) {
  setOwner(QStringLiteral("core.anchorwatch"));
  // Reposition / resize on a watch change, recolour on a breach change.
  auto bump = [this]() {
    m_dirty = true;
    emit dirty();
  };
  connect(m_engine, &AlertEngine::anchorChanged, this, bump);
  connect(m_engine, &AlertEngine::changed, this, bump);
}

QSGNode* AnchorWatchLayer::updateSubtree(QSGNode* /*old*/,
                                         QQuickWindow* /*window*/) {
  if (!m_root) m_root = new QSGNode();
  if (!m_dirty) return m_root;
  m_dirty = false;

  while (m_root->childCount() > 0) {
    QSGNode* c = m_root->childAtIndex(0);
    m_root->removeChildNode(c);
    delete c;
  }
  if (!m_engine || !m_engine->anchorSet()) return m_root;

  const double lat = m_engine->anchorLat();
  const double lon = m_engine->anchorLon();
  // World units per metre at the anchor latitude (Mercator-conformal, so the
  // circle is a circle in world space). 1 NM = 1852 m = 1/(60 cos lat) world X.
  const double cos_lat = std::max(0.05, std::cos(lat * kDeg2Rad));
  const double r = m_engine->anchorRadiusM() / (60.0 * 1852.0 * cos_lat);

  QList<QPointF> circle;
  circle.reserve(kSegments);
  for (int i = 0; i < kSegments; ++i) {
    const double a = (kTwoPi * i) / kSegments;
    circle.append(QPointF(std::cos(a) * r, std::sin(a) * r));
  }
  const QColor col = m_engine->anchorBreach() ? kBreachColor : kWatchColor;
  QSGGeometryNode* ring =
      makeAaLineNode(circle, col, kWatchPx, /*closed=*/true);
  if (!ring) return m_root;

  auto* xf = new QSGTransformNode();
  QMatrix4x4 m;
  m.translate(static_cast<float>(Viewport::lonToWorldX(lon)),
              static_cast<float>(Viewport::latToWorldY(lat)));
  xf->setMatrix(m);
  xf->appendChildNode(ring);
  m_root->appendChildNode(xf);
  return m_root;
}

}  // namespace ocpn::qtui
