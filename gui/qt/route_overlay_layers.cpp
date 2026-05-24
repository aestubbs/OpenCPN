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

#include <QImage>

namespace ocpn::qtui {

namespace {
// (lon, lat) geographic point -> world (x = lon, y = -lat).
inline QPointF lonLatToWorld(const QPointF& ll) {
  return QPointF(ll.x(), -ll.y());
}
}  // namespace

void RouteLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  for (const NavRoute& r : provider()->routes()) {
    if (r.points.size() < 1) continue;
    QList<QPointF> pts;
    pts.reserve(r.points.size());
    for (const QPointF& ll : r.points) pts.append(lonLatToWorld(ll));

    if (pts.size() >= 2) {
      b.setPen(r.color, 2.0f);  // px (AA-line shader is screen-fixed)
      b.noBrush();
      b.drawPolyline(pts);
    }
    // Route-point markers: filled dot with a thin white ring.
    b.setBrush(r.color);
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
