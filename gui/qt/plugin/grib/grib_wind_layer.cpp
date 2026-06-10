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
  if (!window) return m_root;

  SgBuilder b(m_root, window);
  b.setPencil(false);
  drawIsobars(b);
  if (m_grid.ni <= 1 || m_grid.nj <= 1) return m_root;

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

      // Meteorological wind barb (wx GRIB presentation parity): the
      // staff points INTO the wind (towards where it comes from); half
      // barbs = 5 kn, full barbs = 10 kn, pennants = 50 kn, on the
      // clockwise side (northern-hemisphere convention).
      const double len = std::fabs(m_grid.di) * step * 0.9;
      const double n = std::hypot(u, v);
      // Flow direction in world coords; the staff runs opposite it.
      const QPointF flow(u / n, -v / n);
      const QPointF staff(-flow.x(), -flow.y());
      const QPointF perp(-staff.y(), staff.x());
      const QPointF tip = w + QPointF(staff.x() * len, staff.y() * len);
      b.setPen(windColor(kn), 1.4f);
      b.noBrush();
      b.drawLine(w, tip);

      int rem = static_cast<int>(std::round(kn / 5.0)) * 5;
      double along = 1.0;             // fraction of the staff, outer end first
      const double spacing = 0.16;    // staff fractions between barbs
      const double bl = len * 0.42;   // full-barb length
      auto at = [&](double f) {
        return w + QPointF(staff.x() * len * f, staff.y() * len * f);
      };
      while (rem >= 50) {
        const QPointF p0 = at(along), p1 = at(along - spacing);
        const QPointF apex = p0 + QPointF(perp.x() * bl, perp.y() * bl);
        b.setBrush(windColor(kn));
        b.drawPolygon({p0, apex, p1});
        b.noBrush();
        rem -= 50;
        along -= spacing * 1.4;
      }
      while (rem >= 10) {
        const QPointF p0 = at(along);
        b.drawLine(p0, p0 + QPointF((perp.x() + staff.x() * 0.35) * bl,
                                    (perp.y() + staff.y() * 0.35) * bl));
        rem -= 10;
        along -= spacing;
      }
      if (rem >= 5) {
        if (along > 0.95) along = 0.85;  // a lone half barb sits inboard
        const QPointF p0 = at(along);
        b.drawLine(p0,
                   p0 + QPointF((perp.x() + staff.x() * 0.35) * bl * 0.5,
                                (perp.y() + staff.y() * 0.35) * bl * 0.5));
      }
    }
  }
  return m_root;
}

// Marching-squares isolines over the pressure grid, one polyline segment
// set per 2 hPa level (the wx overlay's default isobar spacing).
void GribWindLayer::drawIsobars(SgBuilder& b) {
  const ScalarGrid& g = m_isobars;
  if (g.ni <= 1 || g.nj <= 1) return;
  float lo = 1e9f, hi = -1e9f;
  for (float v : g.v) {
    if (std::isnan(v)) continue;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  if (hi <= lo) return;
  const double kStep = 2.0;  // hPa
  b.setPen(QColor(120, 120, 140, 200), 1.0f);
  b.noBrush();
  auto worldPt = [&](double fi, double fj) {
    return QPointF(g.lon0 + fi * g.di,
                   Viewport::latToWorldY(g.lat0 + fj * g.dj));
  };
  for (double level = std::ceil(lo / kStep) * kStep; level < hi;
       level += kStep) {
    for (int j = 0; j + 1 < g.nj; ++j) {
      for (int i = 0; i + 1 < g.ni; ++i) {
        const float v00 = g.v[j * g.ni + i];
        const float v10 = g.v[j * g.ni + i + 1];
        const float v01 = g.v[(j + 1) * g.ni + i];
        const float v11 = g.v[(j + 1) * g.ni + i + 1];
        if (std::isnan(v00) || std::isnan(v10) || std::isnan(v01) ||
            std::isnan(v11))
          continue;
        // Edge crossings, linearly interpolated.
        QList<QPointF> pts;
        auto cross = [&](float a, float bb, double xi0, double yj0,
                         double xi1, double yj1) {
          if ((a < level) == (bb < level)) return;
          const double t = (level - a) / (bb - a);
          pts.append(worldPt(xi0 + (xi1 - xi0) * t, yj0 + (yj1 - yj0) * t));
        };
        cross(v00, v10, i, j, i + 1, j);          // bottom
        cross(v10, v11, i + 1, j, i + 1, j + 1);  // right
        cross(v01, v11, i, j + 1, i + 1, j + 1);  // top
        cross(v00, v01, i, j, i, j + 1);          // left
        if (pts.size() >= 2) b.drawLine(pts[0], pts[1]);
        if (pts.size() == 4) b.drawLine(pts[2], pts[3]);  // saddle
      }
    }
  }
}

}  // namespace ocpn::qtui
