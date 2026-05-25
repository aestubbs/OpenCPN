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
 * Implement route_overlay_layers.h.
 *
 * Note: these share the provider's single changed() signal, so in demo mode
 * (which ticks at 5 Hz for the moving AIS/own-ship) they currently rebuild
 * each tick despite being static. Cheap for the demo data set; a granular
 * per-feature signal on NavDataProvider would avoid it -- a later refinement.
 */

#include "route_overlay_layers.h"

#include <cmath>

#include <QImage>

namespace ocpn::qtui {

namespace {
// (lon, lat) geographic point -> world (x = lon, y = -lat).
inline QPointF lonLatToWorld(const QPointF& ll) {
  return QPointF(ll.x(), -ll.y());
}

// True compass bearing (deg, 0..360) from a to b, both (lon, lat) degrees.
double bearingDeg(const QPointF& a_ll, const QPointF& b_ll) {
  const double d2r = M_PI / 180.0;
  const double lat1 = a_ll.y() * d2r, lat2 = b_ll.y() * d2r;
  const double dlon = (b_ll.x() - a_ll.x()) * d2r;
  const double y = std::sin(dlon) * std::cos(lat2);
  const double x = std::cos(lat1) * std::sin(lat2) -
                   std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
  double brg = std::atan2(y, x) / d2r;
  if (brg < 0.0) brg += 360.0;
  return brg;
}
}  // namespace

void RouteLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  // Route line colour by scheme: graphite grey in day; lighter grey at
  // dusk/night so it reads against the dimmed chart.
  const QColor lineColor =
      m_scheme == 0 ? QColor(58, 64, 70) : QColor(150, 156, 162);
  const QColor labelColor =
      m_scheme == 0 ? QColor(35, 39, 43) : QColor(214, 218, 222);

  for (const NavRoute& r : provider()->routes()) {
    if (r.points.size() < 1) continue;
    QList<QPointF> pts;
    pts.reserve(r.points.size());
    for (const QPointF& ll : r.points) pts.append(lonLatToWorld(ll));

    if (pts.size() >= 2) {
      b.setPen(lineColor, 3.0f);  // 3 px (AA-line shader is screen-fixed)
      b.noBrush();
      b.drawPolyline(pts);

      // Per-segment compass bearing label, larger font, above the midpoint.
      for (int i = 0; i + 1 < r.points.size(); ++i) {
        const double brg = bearingDeg(r.points[i], r.points[i + 1]);
        const QString txt =
            QStringLiteral("%1°").arg(
                static_cast<int>(std::lround(brg)) % 360, 3, 10, QChar('0'));
        const QImage img = SgBuilder::renderText(txt, labelColor, 13.0f);
        if (img.isNull()) continue;
        const qreal dpr =
            img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
        const double tw = img.width() / dpr * wpp;
        const double th = img.height() / dpr * wpp;
        const QPointF mid = (pts[i] + pts[i + 1]) / 2.0;
        b.drawImage(
            QRectF(mid.x() - tw / 2.0, mid.y() - th - 3.0 * wpp, tw, th), img);
      }
    }
    // Route-point markers: filled dot with a thin white ring.
    b.setBrush(lineColor);
    b.setPen(QColor(255, 255, 255), 1.0f);
    for (const QPointF& w : pts)
      b.drawCircle(w, static_cast<float>(4.0 * wpp));  // radius: world units
  }
}

void TrackLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  for (const NavTrack& t : provider()->tracks()) {
    if (t.points.size() < 2) continue;
    QList<QPointF> pts;
    pts.reserve(t.points.size());
    for (const QPointF& ll : t.points) pts.append(lonLatToWorld(ll));
    b.setPen(t.color, 1.5f);  // px
    b.noBrush();
    b.drawPolyline(pts);
  }
}

void WaypointLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  for (const NavWaypoint& wp : provider()->waypoints()) {
    const QPointF w(wp.lon, -wp.lat);  // world
    b.setBrush(wp.color);
    b.setPen(QColor(40, 40, 40), 1.0f);              // px
    b.drawCircle(w, static_cast<float>(5.0 * wpp));  // radius: world units

    if (!wp.name.isEmpty()) {
      const QImage img = SgBuilder::renderText(wp.name, QColor(20, 20, 20), 9.0f);
      if (!img.isNull()) {
        const qreal dpr =
            img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
        const double tw = img.width() / dpr * wpp;
        const double th = img.height() / dpr * wpp;
        // Label to the right of the marker, vertically centred. Sized in
        // world units (px * wpp) so it stays screen-fixed; the layer rebuilds
        // on zoom (NavLayer re-fires) to keep it crisp.
        b.drawImage(QRectF(w.x() + 7.0 * wpp, w.y() - th / 2.0, tw, th), img);
      }
    }
  }
}

}  // namespace ocpn::qtui
