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
 * Implement pick_highlight_provider.h.
 */

#include "pick_highlight_provider.h"

#include <cmath>

#include <QSGGeometry>
#include <QSGGeometryNode>

#include "sg_helpers.h"
#include "viewport.h"

namespace ocpn::qtui {

PickHighlightProvider::PickHighlightProvider(QString id,
                                             const Viewport* viewport,
                                             QObject* parent)
    : ChartProvider(parent), m_id(std::move(id)), m_viewport(viewport) {
  // Keep the point-feature ring a fixed pixel size as the chart zooms: a zoom
  // changes viewport.scale(), so re-emit the geometry (only meaningful when a
  // point is highlighted; line/area outlines are world-anchored and unchanged,
  // but rebuilding them is cheap). A pure pan leaves scale alone -> no rebuild.
  if (m_viewport)
    connect(m_viewport, &Viewport::changed, this, [this]() {
      if (m_shape.size() == 1 && m_geom == s52sg::QueryGeom::Point)
        emit changed();
    });
}

void PickHighlightProvider::setShape(const QList<QPointF>& shape,
                                     s52sg::QueryGeom geom) {
  m_shape = shape;
  m_geom = geom;
  emit changed();
}

QSGNode* PickHighlightProvider::renderChart(QSGNode* old_subtree,
                                            const Viewport& viewport,
                                            QQuickWindow* /*window*/) {
  auto* node = static_cast<QSGGeometryNode*>(old_subtree);
  if (!node) {
    node = sg::makeFlatColorNode(m_color, QSGGeometry::DrawLines, 0);
    node->geometry()->setLineWidth(2.0f);  // best-effort; clamped on some RHIs
  }

  // Build the outline as world-coord line segments (consecutive pairs). A
  // pure pan/zoom does not change the geometry (the world-anchored root
  // transforms it), so we only rebuild when the shape changes -- which is the
  // only time this provider is dirtied. The point-ring radius is recomputed
  // from the current scale so it stays a fixed pixel size.
  QVector<QPointF> segs;  // world coords (x = lon, y = -lat)
  auto addSeg = [&](const QPointF& a, const QPointF& b) {
    segs.append(QPointF(a.x(), Viewport::latToWorldY(a.y())));
    segs.append(QPointF(b.x(), Viewport::latToWorldY(b.y())));
  };

  if (m_shape.size() == 1 && m_geom == s52sg::QueryGeom::Point) {
    // A point feature: a small ring ~9 logical px in radius, screen-fixed. The
    // world->screen transform applies the SAME scale to x and y (world Y is the
    // Mercator coordinate), so a circle in WORLD space is a circle on screen.
    // Build it directly in world coords -- centre projected ONCE -- not by
    // perturbing latitude and re-projecting (that double-applies Mercator and
    // yields a vertically-stretched ellipse away from the equator).
    const QPointF c(m_shape.front().x(),
                    Viewport::latToWorldY(m_shape.front().y()));
    const double s = viewport.scale() > 0.0 ? viewport.scale() : 1.0;
    const double r = 9.0 / s;  // world units (= deg lon) giving ~9 px on screen
    constexpr int kSeg = 24;
    constexpr double kPi = 3.14159265358979323846;
    QPointF prev;
    for (int i = 0; i <= kSeg; ++i) {
      const double a = 2.0 * kPi * i / kSeg;
      const QPointF p(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
      if (i > 0) {
        segs.append(prev);  // already world coords -- do NOT re-project
        segs.append(p);
      }
      prev = p;
    }
  } else if (m_shape.size() >= 2) {
    const int n = m_shape.size();
    const bool closed = m_geom == s52sg::QueryGeom::Area;
    for (int k = 0; k + 1 < n; ++k) addSeg(m_shape[k], m_shape[k + 1]);
    if (closed) addSeg(m_shape[n - 1], m_shape[0]);  // close the ring
  }

  QSGGeometry* geom = node->geometry();
  geom->allocate(segs.size());
  QSGGeometry::Point2D* v = geom->vertexDataAsPoint2D();
  for (int k = 0; k < segs.size(); ++k)
    v[k].set(static_cast<float>(segs[k].x()), static_cast<float>(segs[k].y()));
  node->markDirty(QSGNode::DirtyGeometry);
  return node;
}

}  // namespace ocpn::qtui
