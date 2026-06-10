/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "grib_wind_layer.h"

#include <cmath>

#include <QQuickWindow>
#include <QSGNode>

namespace ocpn::qtui {

namespace {
// Speed (kn) -> arrow colour: calm green through gale red/violet.
QColor windColor(double kn) {
  if (kn < 7) return QColor(80, 180, 90);
  if (kn < 14) return QColor(150, 190, 60);
  if (kn < 21) return QColor(220, 180, 40);
  if (kn < 28) return QColor(235, 130, 40);
  if (kn < 34) return QColor(230, 70, 50);
  if (kn < 48) return QColor(200, 40, 90);
  return QColor(150, 30, 160);
}
}  // namespace

QSGNode* GribWindLayer::updateSubtree(QSGNode* /*old*/,
                                      QQuickWindow* window) {
  if (!m_root)
    m_root = new QSGNode();
  else
    while (QSGNode* c = m_root->firstChild()) delete c;
  if (!window || m_grid.ni <= 1 || m_grid.nj <= 1) return m_root;

  SgBuilder b(m_root, window);
  b.setPencil(false);

  // World units per px for screen-fixed arrow sizing; decimate the grid so
  // arrows sit >= ~34 px apart at the current zoom.
  const double wpp = 1.0;  // sized in world units of the grid spacing below
  Q_UNUSED(wpp);
  // Approximate px per grid cell from the layer's compositor transform is
  // not available here; use a fixed decimation against the grid size for
  // v1 (the GRIB grids are coarse; density tuning follows verification).
  const int step = qMax(1, qMax(m_grid.ni, m_grid.nj) / 48);

  for (int j = 0; j < m_grid.nj; j += step) {
    for (int i = 0; i < m_grid.ni; i += step) {
      const float u = m_grid.u[j * m_grid.ni + i];
      const float v = m_grid.v[j * m_grid.ni + i];
      if (std::isnan(u) || std::isnan(v)) continue;
      const double spd_ms = std::hypot(u, v);
      const double kn = spd_ms * 1.94384;
      if (kn < 0.5) continue;
      const double lon = m_grid.lon0 + i * m_grid.di;
      const double lat = m_grid.lat0 + j * m_grid.dj;
      const QPointF w(lon, Viewport::latToWorldY(lat));

      // Arrow along the wind vector (towards where it blows), length
      // scaled by half the decimated grid spacing.
      const double len = std::fabs(m_grid.di) * step * 0.45;
      const double n = std::hypot(u, v);
      const QPointF dir(u / n, -v / n);  // world y is inverted-lat
      const QPointF tip = w + QPointF(dir.x() * len, dir.y() * len);
      const QPointF tail = w - QPointF(dir.x() * len, dir.y() * len);
      b.setPen(windColor(kn), 1.6f);
      b.noBrush();
      b.drawLine(tail, tip);
      // Head: two short back-strokes.
      const QPointF back(-dir.x(), -dir.y());
      const QPointF perp(-dir.y(), dir.x());
      const double hl = len * 0.35;
      b.drawLine(tip, tip + QPointF((back.x() + perp.x() * 0.6) * hl,
                                    (back.y() + perp.y() * 0.6) * hl));
      b.drawLine(tip, tip + QPointF((back.x() - perp.x() * 0.6) * hl,
                                    (back.y() - perp.y() * 0.6) * hl));
    }
  }
  return m_root;
}

}  // namespace ocpn::qtui
