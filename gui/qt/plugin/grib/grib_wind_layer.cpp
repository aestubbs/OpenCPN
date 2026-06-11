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

#include <QtMath>

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

void GribWindLayer::setViewport(const Viewport* vp) {
  m_vp = vp;
  if (!m_vp) return;
  m_last_scale = m_vp->scale();
  connect(m_vp, &Viewport::changed, this, [this]() {
    const double s = m_vp->scale();
    if (s != m_last_scale) {
      m_last_scale = s;
      Q_EMIT dirty();
    }
  });
}

double GribWindLayer::worldPerPx() const {
  const double s = m_vp ? m_vp->scale() : 0.0;
  return s > 0 ? 1.0 / s : 0.01;
}

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
  drawArrows(b);
  drawNumbers(b);
  if (m_label_cache.size() > 1200) m_label_cache.clear();
  if (m_grid.ni <= 1 || m_grid.nj <= 1) return m_root;

  // Screen-fixed sizing: barbs are ~42 px regardless of zoom; the grid
  // decimates so barbs sit >= ~60 px apart (PERF: rebuilds only on zoom).
  const double wpp = worldPerPx();
  const double cellPx = std::fabs(m_grid.di) / wpp;
  const int step =
      cellPx > 0 ? qMax(1, qCeil(60.0 / cellPx)) : qMax(1, m_grid.ni / 48);

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
      const double len = 42.0 * wpp;  // ~42 px staff, zoom-independent
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

// Direction arrows (waves / current): screen-fixed ~26 px shafts with a
// small head, optional magnitude number beneath.
void GribWindLayer::drawArrows(SgBuilder& b) {
  const double wpp = worldPerPx();
  for (auto it = m_arrows.cbegin(); it != m_arrows.cend(); ++it) {
    const ArrowField& f = it.value();
    const WindGrid& g = f.grid;
    if (g.ni <= 1 || g.nj <= 1) continue;
    const double cellPx = std::fabs(g.di) / wpp;
    const int step =
        cellPx > 0 ? qMax(1, qCeil(70.0 / cellPx)) : qMax(1, g.ni / 40);
    const double len = 26.0 * wpp;
    b.setPen(f.color, 1.6f);
    b.noBrush();
    for (int j = 0; j < g.nj; j += step) {
      for (int i = 0; i < g.ni; i += step) {
        const float a = g.u[j * g.ni + i];
        const float m = g.v[j * g.ni + i];
        if (std::isnan(a) || std::isnan(m)) continue;
        double dx, dy, mag;
        if (f.dirMag) {
          if (m <= 0.01) continue;
          // FROM-direction degrees -> flow vector (towards).
          const double rad = (a + 180.0) * M_PI / 180.0;
          dx = std::sin(rad);
          dy = -std::cos(rad);
          mag = m;
        } else {
          mag = std::hypot(a, m);
          if (mag <= 0.01) continue;
          dx = a / mag;
          dy = -m / mag;
        }
        const double lon = g.lon0 + i * g.di;
        const double lat = g.lat0 + j * g.dj;
        const QPointF w(lon, Viewport::latToWorldY(lat));
        const QPointF tip = w + QPointF(dx * len, dy * len);
        const QPointF tail = w - QPointF(dx * len, dy * len);
        b.drawLine(tail, tip);
        const QPointF back(-dx, -dy);
        const QPointF perp(-dy, dx);
        const double hl = len * 0.4;
        b.drawLine(tip, tip + QPointF((back.x() + perp.x() * 0.5) * hl,
                                      (back.y() + perp.y() * 0.5) * hl));
        b.drawLine(tip, tip + QPointF((back.x() - perp.x() * 0.5) * hl,
                                      (back.y() - perp.y() * 0.5) * hl));
        if (f.showNumber) {
          const QString t =
              QString::number(mag * f.unitFactor, 'f', 1) + f.unitSuffix;
          if (!m_label_cache.contains(t))
            m_label_cache.insert(
                t, SgBuilder::renderText(t, f.color, 8.0f));
          const QImage img = m_label_cache.value(t);
          if (!img.isNull()) {
            const qreal dpr =
                img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1;
            b.drawImage(QRectF(w.x() + 4 * wpp, w.y() + 6 * wpp,
                               img.width() / dpr * wpp,
                               img.height() / dpr * wpp),
                        img);
          }
        }
      }
    }
  }
}

// Scalar values as numbers at decimated grid points (gust, rain, temps…).
void GribWindLayer::drawNumbers(SgBuilder& b) {
  const double wpp = worldPerPx();
  // Stack multiple number fields with a vertical offset per field.
  int fieldRow = 0;
  for (auto it = m_numbers.cbegin(); it != m_numbers.cend(); ++it) {
    const NumberField& f = it.value();
    const ScalarGrid& g = f.grid;
    if (g.ni <= 1 || g.nj <= 1) continue;
    const double cellPx = std::fabs(g.di) / wpp;
    const int step =
        cellPx > 0 ? qMax(1, qCeil(85.0 / cellPx)) : qMax(1, g.ni / 30);
    for (int j = 0; j < g.nj; j += step) {
      for (int i = 0; i < g.ni; i += step) {
        const float v = g.v[j * g.ni + i];
        if (std::isnan(v)) continue;
        const QString t =
            QString::number(v * f.factor + f.offset, 'f', f.decimals) +
            f.suffix;
        if (!m_label_cache.contains(t))
          m_label_cache.insert(t, SgBuilder::renderText(t, f.color, 8.5f));
        const QImage img = m_label_cache.value(t);
        if (img.isNull()) continue;
        const double lon = g.lon0 + i * g.di;
        const double lat = g.lat0 + j * g.dj;
        const QPointF w(lon, Viewport::latToWorldY(lat));
        const qreal dpr =
            img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1;
        b.drawImage(QRectF(w.x() - img.width() / dpr * wpp / 2,
                           w.y() + fieldRow * 12.0 * wpp,
                           img.width() / dpr * wpp,
                           img.height() / dpr * wpp),
                    img);
      }
    }
    ++fieldRow;
  }
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
