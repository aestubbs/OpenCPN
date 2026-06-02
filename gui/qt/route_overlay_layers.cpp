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
#include <QTransform>

#include "model/routeman.h"  // pWayPointMan -- waypoint icon catalogue
#include "viewport.h"

namespace ocpn::qtui {

namespace {
// (lon, lat) geographic point -> world (x = lon, y = Mercator-lat).
inline QPointF lonLatToWorld(const QPointF& ll) {
  return QPointF(ll.x(), Viewport::latToWorldY(ll.y()));
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
    const bool selected = !m_selected.isEmpty() && r.guid == m_selected;
    // Draw a route only when its visibility "eye" is on OR it is the selected
    // route (visibility and selection are independent). The in-progress draft
    // has no GUID, so it always shows while drawing.
    if (!r.guid.isEmpty() && !selected && !m_visible.contains(r.guid)) continue;
    QList<QPointF> pts;
    pts.reserve(r.points.size());
    for (const QPointF& ll : r.points) pts.append(lonLatToWorld(ll));

    if (pts.size() >= 2) {
      b.setPen(lineColor, 2.0f);  // 2 px graphite (AA-line shader screen-fixed)
      b.noBrush();
      b.drawPolyline(pts);

      // Per-segment compass bearing label, rotated to lie along the segment
      // (kept upright) and offset just off the line.
      for (int i = 0; i + 1 < r.points.size(); ++i) {
        const double brg = bearingDeg(r.points[i], r.points[i + 1]);
        const QString txt =
            QStringLiteral("%1°").arg(
                static_cast<int>(std::lround(brg)) % 360, 3, 10, QChar('0'));
        QImage img = SgBuilder::renderText(txt, labelColor, 16.0f);
        if (img.isNull()) continue;
        const qreal dpr =
            img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;

        // Segment direction in world == screen orientation (x/y scaled
        // equally). Align the text to it, flipped to stay left-to-right.
        const QPointF a = pts[i], c = pts[i + 1];
        const double dx = c.x() - a.x(), dy = c.y() - a.y();
        const double len = std::hypot(dx, dy);
        if (len <= 0.0) continue;
        double angle = std::atan2(dy, dx) * 180.0 / M_PI;
        if (angle > 90.0)
          angle -= 180.0;
        else if (angle < -90.0)
          angle += 180.0;
        img = img.transformed(QTransform().rotate(angle),
                              Qt::SmoothTransformation);

        const double tw = img.width() / dpr * wpp;
        const double th = img.height() / dpr * wpp;
        // Offset off the line along its normal so the label clears the route.
        const double nx = -dy / len, ny = dx / len;  // left normal
        const double off = 12.0 * wpp;
        const QPointF mid =
            (a + c) / 2.0 + QPointF(nx * off, ny * off);
        b.drawImage(QRectF(mid.x() - tw / 2.0, mid.y() - th / 2.0, tw, th),
                    img);
      }
    }
    // Route-point markers. Three states, visually distinct: edit mode gets
    // large draggable handles ringed amber; a merely-selected route gets a
    // small cyan accent ring (selection, not edit); others a thin white ring.
    const bool editing = selected && m_editing;
    const double radius = (editing ? 6.0 : 4.0) * wpp;
    const QColor ring = editing ? QColor(255, 200, 60)        // amber: editable
                                : (selected ? QColor(90, 200, 255)  // cyan: selected
                                            : QColor(255, 255, 255));
    b.setBrush(lineColor);
    b.setPen(ring, editing ? 2.0f : 1.0f);
    for (const QPointF& w : pts)
      b.drawCircle(w, static_cast<float>(radius));  // radius: world units
  }
}

void TrackLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  for (const NavTrack& t : provider()->tracks()) {
    if (!t.visible) continue;          // per-track eye
    if (t.points.size() < 2) continue;
    QList<QPointF> pts;
    pts.reserve(t.points.size());
    for (const QPointF& ll : t.points) pts.append(lonLatToWorld(ll));
    const bool selected = !m_selected.isEmpty() && t.guid == m_selected;
    b.setPen(selected ? QColor(90, 200, 255) : t.color, selected ? 2.5f : 1.5f);
    b.noBrush();
    b.drawPolyline(pts);
  }
}

void WaypointLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  for (const NavWaypoint& wp : provider()->waypoints()) {
    if (!wp.visible) continue;  // the per-mark eye (default on)
    const QPointF w = lonLatToWorld(QPointF(wp.lon, wp.lat));  // Mercator world
    const bool selected = !m_selected.isEmpty() && wp.guid == m_selected;

    // Draw the chosen icon (screen-fixed); fall back to a coloured dot if the
    // icon catalogue has no such key. The selected mark gets a cyan ring.
    const QImage* icon =
        pWayPointMan ? pWayPointMan->GetIconBitmap(wp.iconName) : nullptr;
    if (icon && !icon->isNull()) {
      const qreal dpr =
          icon->devicePixelRatio() > 0 ? icon->devicePixelRatio() : 1.0;
      const double iw = icon->width() / dpr * wpp;
      const double ih = icon->height() / dpr * wpp;
      b.drawImage(QRectF(w.x() - iw / 2.0, w.y() - ih / 2.0, iw, ih), *icon);
      if (selected) {
        b.noBrush();
        b.setPen(QColor(90, 200, 255), 2.0f);
        b.drawCircle(w, static_cast<float>((iw > ih ? iw : ih) * 0.75));
      }
    } else {
      b.setBrush(wp.color);
      b.setPen(selected ? QColor(90, 200, 255) : QColor(40, 40, 40),
               selected ? 2.0f : 1.0f);
      b.drawCircle(w, static_cast<float>((selected ? 7.0 : 5.0) * wpp));
    }

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
