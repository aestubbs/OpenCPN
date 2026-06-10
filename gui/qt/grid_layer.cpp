/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "grid_layer.h"

#include <algorithm>
#include <cmath>

#include <QQuickWindow>

#include "display_config.h"

namespace ocpn::qtui {

namespace {

// "Nice" graticule intervals (degrees), coarse -> fine. Minute-based at the
// fine end so labels read as whole minutes.
constexpr double kIntervals[] = {
    30.0, 15.0, 10.0, 5.0,         2.0,        1.0,
    30.0 / 60,  20.0 / 60, 10.0 / 60, 5.0 / 60, 2.0 / 60, 1.0 / 60,
    0.5 / 60,   0.2 / 60,  0.1 / 60};

// Pick the interval that spaces lines ~`target_px` apart at `px_per_deg`:
// the first (coarsest-to-finest) interval under ~1.8x the target spacing.
double pickInterval(double px_per_deg, double target_px) {
  double best = kIntervals[0];
  for (double iv : kIntervals) {
    best = iv;
    if (iv * px_per_deg <= target_px * 1.8) break;
  }
  return best;
}

// "50°42.5'N" style label; whole degrees omit the minutes when exact.
QString fmtCoord(double v, bool is_lat) {
  const char hemi = is_lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
  const double a = std::abs(v);
  const int deg = static_cast<int>(a + 1e-9);
  const double min = (a - deg) * 60.0;
  if (min < 1e-6)
    return QString::number(deg) + QChar(0x00B0) + QChar(hemi);
  QString ms = QString::number(min, 'g', 4);
  return QString::number(deg) + QChar(0x00B0) + ms + QChar('\'') +
         QChar(hemi);
}

}  // namespace

GridLayer::GridLayer(NavDataProvider* provider, const Viewport* viewport,
                     QObject* parent)
    : NavLayer(provider, viewport, parent), m_vp(viewport) {
  setOwner(QStringLiteral("core.grid"));
  // The graticule spans the VISIBLE view, so a pan must rebuild it too --
  // the NavLayer base only re-fires on zoom.
  if (m_vp) connect(m_vp, &Viewport::changed, this, &Layer::dirty);
  connect(&DisplayConfig::instance(), &DisplayConfig::changed, this,
          &Layer::dirty);
}

QSGNode* GridLayer::updateSubtree(QSGNode* /*old*/, QQuickWindow* window) {
  if (!m_root)
    m_root = new QSGNode();
  else
    while (QSGNode* c = m_root->firstChild()) delete c;

  if (!m_vp || !window || !DisplayConfig::instance().showGrid()) return m_root;

  // Visible geographic extent: the window bounds over-cover the canvas (HUD /
  // time bar trim it), which is fine -- a slightly larger grid is harmless.
  // Sample all four corners so a rotated view still gets a covering bbox.
  const int w = static_cast<int>(window->width());
  const int h = static_cast<int>(window->height());
  if (w <= 0 || h <= 0) return m_root;
  double n = -90, s = 90, e = -180, west = 180;
  const QPointF corners[4] = {{0, 0},
                              {static_cast<qreal>(w), 0},
                              {0, static_cast<qreal>(h)},
                              {static_cast<qreal>(w), static_cast<qreal>(h)}};
  for (const QPointF& c : corners) {
    double lat = 0, lon = 0;
    m_vp->screenToLatLon(c.x(), c.y(), w, h, lat, lon);
    n = std::max(n, lat);
    s = std::min(s, lat);
    e = std::max(e, lon);
    west = std::min(west, lon);
  }
  if (!(n > s) || !(e > west)) return m_root;

  // Bound the label cache (panning the world accumulates labels).
  if (m_label_cache.size() > 600) m_label_cache.clear();

  const double iv = pickInterval(currentScale(), 140.0);
  // Bound the line count (a degenerate viewport could explode it).
  if ((n - s) / iv > 60 || (e - west) / iv > 60) return m_root;

  SgBuilder b(m_root, window);
  const double wpp = worldPerPx();
  const QColor line(110, 125, 145, 110);  // subtle blue-grey
  const QColor text(70, 85, 105);
  b.setPencil(false);
  b.noBrush();
  b.setPen(line, 1.0f);

  const QPointF tl = world(n, west);
  const QPointF br = world(s, e);

  // Meridians (vertical lines of constant longitude) + top-edge labels.
  for (double lon = std::ceil(west / iv) * iv; lon <= e + 1e-9; lon += iv) {
    b.drawLine(QPointF(lon, tl.y()), QPointF(lon, br.y()));
    const QString lbl = fmtCoord(lon, false);
    if (!m_label_cache.contains(lbl))
      m_label_cache.insert(lbl, SgBuilder::renderText(lbl, text, 8.5f));
    const QImage img = m_label_cache.value(lbl);
    if (!img.isNull()) {
      const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1;
      b.drawImage(QRectF(lon + 3.0 * wpp, tl.y() + 3.0 * wpp,
                         img.width() / dpr * wpp, img.height() / dpr * wpp),
                  img);
    }
  }
  // Parallels (horizontal lines of constant latitude) + left-edge labels.
  for (double lat = std::ceil(s / iv) * iv; lat <= n + 1e-9; lat += iv) {
    const double y = world(lat, 0).y();
    b.drawLine(QPointF(tl.x(), y), QPointF(br.x(), y));
    const QString lbl2 = fmtCoord(lat, true);
    if (!m_label_cache.contains(lbl2))
      m_label_cache.insert(lbl2, SgBuilder::renderText(lbl2, text, 8.5f));
    const QImage img = m_label_cache.value(lbl2);
    if (!img.isNull()) {
      const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1;
      b.drawImage(QRectF(tl.x() + 3.0 * wpp, y + 3.0 * wpp,
                         img.width() / dpr * wpp, img.height() / dpr * wpp),
                  img);
    }
  }
  return m_root;
}

}  // namespace ocpn::qtui
