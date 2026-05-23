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
 * Implement chart_boundary_provider.h.
 */

#include "chart_boundary_provider.h"

#include <QSGGeometry>
#include <QSGGeometryNode>

#include "sg_helpers.h"
#include "viewport.h"

namespace ocpn::qtui {

ChartBoundaryProvider::ChartBoundaryProvider(QString id, QObject* parent)
    : ChartProvider(parent), m_id(std::move(id)) {}

void ChartBoundaryProvider::setExtents(const QList<CellExtent>& extents) {
  m_extents = extents;
  // Recompute the union bounds (chart-DB / culling consumers read these).
  m_north = -90.0;
  m_south = 90.0;
  m_east = -180.0;
  m_west = 180.0;
  for (const CellExtent& c : m_extents) {
    if (!c.valid()) continue;
    if (c.north > m_north) m_north = c.north;
    if (c.south < m_south) m_south = c.south;
    if (c.east > m_east) m_east = c.east;
    if (c.west < m_west) m_west = c.west;
  }
  Q_EMIT changed();
}

QSGNode* ChartBoundaryProvider::renderChart(QSGNode* old_subtree,
                                            const Viewport& /*viewport*/,
                                            QQuickWindow* /*window*/) {
  // One flat-colour line-list geometry node holding every cell's four edges.
  // Rebuild from scratch each time we're dirtied (the catalog only changes
  // when a fresh scan completes -- not per frame).
  auto* node = static_cast<QSGGeometryNode*>(old_subtree);
  if (!node)
    node = sg::makeFlatColorNode(m_color, QSGGeometry::DrawLines, 0);

  int valid = 0;
  for (const CellExtent& c : m_extents)
    if (c.valid()) ++valid;

  // 4 edges * 2 endpoints per cell. Re-allocate the existing geometry in
  // place rather than swapping in a fresh QSGGeometry each rebuild.
  const int vcount = valid * 8;
  QSGGeometry* geom = node->geometry();
  geom->allocate(vcount);
  QSGGeometry::Point2D* v = geom->vertexDataAsPoint2D();

  int i = 0;
  for (const CellExtent& c : m_extents) {
    if (!c.valid()) continue;
    // World coords: x = lon, y = -lat. Corners (clockwise).
    const float xl = static_cast<float>(c.west);
    const float xr = static_cast<float>(c.east);
    const float yt = static_cast<float>(-c.north);  // top  (north)
    const float yb = static_cast<float>(-c.south);  // bottom (south)
    auto edge = [&](float x0, float y0, float x1, float y1) {
      v[i++].set(x0, y0);
      v[i++].set(x1, y1);
    };
    edge(xl, yt, xr, yt);  // north edge
    edge(xr, yt, xr, yb);  // east edge
    edge(xr, yb, xl, yb);  // south edge
    edge(xl, yb, xl, yt);  // west edge
  }

  node->markDirty(QSGNode::DirtyGeometry);
  return node;
}

}  // namespace ocpn::qtui
