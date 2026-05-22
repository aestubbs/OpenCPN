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
 * ChartBoundaryProvider -- a ChartProvider that draws the coverage outline
 * of every catalogued ENC cell as a rectangle, so the whole chart set is
 * visible as a "world map of boundaries" before any cell's content is
 * loaded.
 *
 * Fed a list of CellExtent (the decode-free catalog from
 * S52Engine::scanCellExtents). Renders one closed rectangle per cell as a
 * flat-colour QSGGeometry line list. The outlines are world-anchored
 * (their vertices are in world coords x = lon, y = -lat, so they pan/zoom
 * with the chart) but draw at a constant 1px width regardless of zoom --
 * Qt's scene-graph line width is a raster property, unaffected by the
 * viewport transform -- so the grid stays crisp at every scale.
 */

#ifndef OCPN_QT_CHART_BOUNDARY_PROVIDER_H_
#define OCPN_QT_CHART_BOUNDARY_PROVIDER_H_

#include <QColor>
#include <QList>
#include <QString>

#include "chart_extent.h"
#include "chart_provider.h"

namespace ocpn::qtui {

class ChartBoundaryProvider : public ChartProvider {
  Q_OBJECT

public:
  explicit ChartBoundaryProvider(QString id, QObject* parent = nullptr);

  QString id() const override { return m_id; }
  QString name() const override { return QStringLiteral("Chart boundaries"); }

  double northLat() const override { return m_north; }
  double southLat() const override { return m_south; }
  double westLon() const override { return m_west; }
  double eastLon() const override { return m_east; }

  /** Replace the catalog and trigger a rebuild (emits changed()). */
  void setExtents(const QList<CellExtent>& extents);

  QSGNode* renderChart(QSGNode* old_subtree, const Viewport& viewport,
                       QQuickWindow* window) override;

private:
  QString m_id;
  QList<CellExtent> m_extents;
  QColor m_color{90, 110, 140, 200};  // muted slate blue, mostly opaque
  double m_north = -90.0, m_south = 90.0, m_east = -180.0, m_west = 180.0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_BOUNDARY_PROVIDER_H_
