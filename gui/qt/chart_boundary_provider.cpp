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

void ChartBoundaryProvider::setHighlight(const QString& cell_name) {
  if (cell_name == m_highlight) return;
  m_highlight = cell_name;
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

  // The availability grid: draw each cell's BOUNDING-BOX rectangle (the chart's
  // extent), always on, for every catalogued cell -- a simple "a chart exists
  // here" indicator (per the display rules in Docs/QT_QUILT_VS_WX.md). The
  // rendered CONTENT of a cell is separately clipped to its M_COVR coverage, so
  // a cell's data may fill less than its rectangle; the rectangle still marks
  // where the chart is, including finer charts not yet rendered at this zoom.
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
    const float yt = static_cast<float>(Viewport::latToWorldY(c.north));  // N
    const float yb = static_cast<float>(Viewport::latToWorldY(c.south));  // S
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

  // Highlight overlay: the selected cell's coverage (or bbox), drawn brighter
  // over the grid as a child node. Use DrawLines (segment pairs) so multiple
  // coverage loops live in one geometry without primitive restarts.
  auto* hi = node->childCount() > 0
                 ? static_cast<QSGGeometryNode*>(node->firstChild())
                 : nullptr;
  const CellExtent* sel = nullptr;
  if (!m_highlight.isEmpty())
    for (const CellExtent& c : m_extents)
      if (c.name == m_highlight && c.valid()) {
        sel = &c;
        break;
      }
  if (!sel) {
    if (hi) {
      node->removeChildNode(hi);
      delete hi;
    }
  } else {
    QVector<QPointF> segs;  // even count: consecutive pairs are line segments
    auto addLoop = [&](const QPolygonF& poly) {
      const int n = poly.size();
      if (n < 2) return;
      for (int k = 0; k < n; ++k) {
        const QPointF a = poly[k], b = poly[(k + 1) % n];
        segs.append(QPointF(a.x(), Viewport::latToWorldY(a.y())));  // Mercator
        segs.append(QPointF(b.x(), Viewport::latToWorldY(b.y())));
      }
    };
    if (!sel->coverage.isEmpty())
      for (const QPolygonF& p : sel->coverage) addLoop(p);
    else
      addLoop(QPolygonF({{sel->west, sel->north},
                         {sel->east, sel->north},
                         {sel->east, sel->south},
                         {sel->west, sel->south}}));

    if (!hi) {
      hi = sg::makeFlatColorNode(m_highlight_color, QSGGeometry::DrawLines, 0);
      hi->geometry()->setLineWidth(3.0f);  // best-effort; clamped on some RHIs
      node->appendChildNode(hi);
    }
    QSGGeometry* hg = hi->geometry();
    hg->allocate(segs.size());
    QSGGeometry::Point2D* hv = hg->vertexDataAsPoint2D();
    for (int k = 0; k < segs.size(); ++k)
      hv[k].set(static_cast<float>(segs[k].x()), static_cast<float>(segs[k].y()));
    hi->markDirty(QSGNode::DirtyGeometry);
  }
  return node;
}

}  // namespace ocpn::qtui
