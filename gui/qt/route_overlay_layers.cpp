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
#include "route_defaults_config.h"
#include "viewport.h"

namespace ocpn::qtui {

namespace {
// (lon, lat) geographic point -> world (x = lon, y = Mercator-lat).
inline QPointF lonLatToWorld(const QPointF& ll) {
  return QPointF(ll.x(), Viewport::latToWorldY(ll.y()));
}

// Map a RouteDefaultsConfig route style (0 solid .. 4 dash-dot) to an AA-line
// dash pattern (logical px). Dash-dot is approximated (single on/off period).
void applyRouteDash(SgBuilder& b, int style) {
  switch (style) {
    case 1: b.setDash(1.5f, 4.0f); break;   // dot
    case 2: b.setDash(14.0f, 8.0f); break;  // long dash
    case 3: b.setDash(6.0f, 5.0f); break;   // short dash
    case 4: b.setDash(10.0f, 6.0f); break;  // dash-dot (approx)
    default: b.noDash(); break;             // 0 = solid
  }
}

// Lighten a colour for dusk/night so a dark default (graphite) still reads on
// the dimmed chart. scheme 0 = day (unchanged).
QColor forScheme(const QColor& c, int scheme) {
  if (scheme == 0) return c;
  return QColor((c.red() + 200) / 2, (c.green() + 205) / 2, (c.blue() + 210) / 2);
}

// Graphite grain for a rendered text label (the bearing numbers), so they read
// like the pencil route line: a per-device-pixel coverage speckle, floored so
// the glyphs stay legible. In-place on the premultiplied image; scaling all
// four channels keeps it a valid premultiplied pixel.
void applyPencilGrain(QImage& img) {
  if (img.isNull()) return;
  if (img.format() != QImage::Format_RGBA8888_Premultiplied)
    img = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
  const int w = img.width(), h = img.height();
  for (int y = 0; y < h; ++y) {
    uchar* line = img.scanLine(y);
    for (int x = 0; x < w; ++x) {
      uchar* px = line + x * 4;  // R, G, B, A (premultiplied byte order)
      if (px[3] == 0) continue;
      const float s = std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f;
      const float g = 0.62f + 0.38f * (s - std::floor(s));  // grain 0.62..1.0
      px[0] = static_cast<uchar>(px[0] * g);
      px[1] = static_cast<uchar>(px[1] * g);
      px[2] = static_cast<uchar>(px[2] * g);
      px[3] = static_cast<uchar>(px[3] * g);
    }
  }
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
  // Route line colour + style come from RouteDefaultsConfig (Options > Routes),
  // lightened for dusk/night so a dark default (graphite) still reads on the
  // dimmed chart.
  const RouteDefaultsConfig& rc = RouteDefaultsConfig::instance();
  const QColor lineColor = forScheme(rc.routeColor(), m_scheme);
  const int routeStyle = rc.routeStyle();
  const QColor labelColor =
      m_scheme == 0 ? QColor(35, 39, 43) : QColor(214, 218, 222);
  // Display 1:N for SCAMIN decluttering (approx; cos-lat ~ 1 for a threshold).
  const double scale = currentScale();  // px per degree
  const double displayN =
      scale > 0.0 ? 111320.0 * 3.78 * 1000.0 / scale : 1.0e12;

  for (const NavRoute& r : provider()->routes()) {
    if (r.points.size() < 1) continue;
    const bool selected = !m_selected.isEmpty() && r.guid == m_selected;
    // Draw a route only when its visibility "eye" is on OR it is the selected
    // route (visibility and selection are independent). The in-progress draft
    // has no GUID, so it always shows while drawing.
    if (!r.guid.isEmpty() && !selected && !m_visible.contains(r.guid)) continue;
    // SCAMIN declutter: cull a route once zoomed out past its SCAMIN -- but
    // keep the active (followed) and selected routes visible at any scale.
    if (r.scamin > 0 && !r.active && !selected && displayN > r.scamin) continue;
    QList<QPointF> pts;
    pts.reserve(r.points.size());
    for (const QPointF& ll : r.points) pts.append(lonLatToWorld(ll));

    // Opacity by state: the active route at full strength; a merely-visible
    // (eye-on, not selected) route is dimmed so the active one stands out.
    const double op = (r.active || selected) ? 1.0 : 0.55;
    QColor lc = lineColor;
    lc.setAlphaF(op);

    if (pts.size() >= 2) {
      b.setPen(lc, 2.0f);   // 2 px (AA-line shader, screen-fixed)
      b.setPencil(true);    // graphite pencil stroke for the line + chevrons
      applyRouteDash(b, routeStyle);
      b.noBrush();
      b.drawPolyline(pts);
      b.noDash();  // chevrons stay solid (still pencil); labels/markers below

      // Per-segment compass bearing label, rotated to lie along the segment
      // (kept upright) and offset just off the line.
      for (int i = 0; i + 1 < r.points.size(); ++i) {
        // Direction chevron at the leg midpoint, pointing the way the route
        // runs (so a route's travel direction reads at a glance).
        {
          const QPointF a = pts[i], c = pts[i + 1];
          const double dx = c.x() - a.x(), dy = c.y() - a.y();
          const double len = std::hypot(dx, dy);
          if (len > 0.0) {
            const double ux = dx / len, uy = dy / len;  // along the leg
            const double nx = -uy, ny = ux;             // leg normal
            const double sz = 7.0 * wpp;                // chevron size (world)
            const QPointF mid = (a + c) / 2.0;
            const QPointF tip(mid.x() + ux * sz, mid.y() + uy * sz);
            const QPointF wL(mid.x() - ux * sz + nx * sz,
                             mid.y() - uy * sz + ny * sz);
            const QPointF wR(mid.x() - ux * sz - nx * sz,
                             mid.y() - uy * sz - ny * sz);
            b.setPen(lc, 2.0f);
            b.noBrush();
            b.drawPolyline(QList<QPointF>{wL, tip, wR});
          }
        }
        const double brg = bearingDeg(r.points[i], r.points[i + 1]);
        const QString txt =
            QStringLiteral("%1°").arg(
                static_cast<int>(std::lround(brg)) % 360, 3, 10, QChar('0'));
        QImage img = SgBuilder::renderText(txt, labelColor, 20.0f);
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
        applyPencilGrain(img);  // graphite grain, after rotation so it's crisp

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
    b.setPencil(false);  // node markers are clean (not pencil)
    const bool editing = selected && m_editing;
    const QColor ring = editing ? QColor(255, 200, 60)        // amber: editable
                                : (selected ? QColor(90, 200, 255)  // cyan: selected
                                            : QColor(255, 255, 255));
    // Per-route mark icon (route-details dialog) if set + present in the icon
    // catalogue; otherwise the plain dot. Edit/selection still get a ring.
    const QImage* icon = (!r.pointIcon.isEmpty() && pWayPointMan)
                             ? pWayPointMan->GetIconBitmap(r.pointIcon)
                             : nullptr;
    if (icon && !icon->isNull()) {
      const qreal dpr =
          icon->devicePixelRatio() > 0 ? icon->devicePixelRatio() : 1.0;
      const double iw = icon->width() / dpr * wpp;
      const double ih = icon->height() / dpr * wpp;
      for (const QPointF& w : pts)
        b.drawImage(QRectF(w.x() - iw / 2.0, w.y() - ih / 2.0, iw, ih), *icon);
      if (editing || selected) {
        b.noBrush();
        b.setPen(ring, editing ? 2.0f : 1.5f);
        const double rr = (iw > ih ? iw : ih) * 0.65;
        for (const QPointF& w : pts) b.drawCircle(w, static_cast<float>(rr));
      }
    } else {
      const double radius = (editing ? 6.0 : 4.0) * wpp;
      b.setBrush(lc);
      b.setPen(ring, editing ? 2.0f : 1.0f);
      for (const QPointF& w : pts)
        b.drawCircle(w, static_cast<float>(radius));  // radius: world units
    }
  }
}

void RouteFollowLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  // Active-route accent colour (orange-red), distinct from the grey route line
  // and the cyan selection / amber edit handles.
  const QColor accent(255, 90, 40);
  const OwnShipState own = provider()->ownShip();

  for (const NavRoute& r : provider()->routes()) {
    if (!r.active || r.points.size() < 2) continue;
    QList<QPointF> pts;
    pts.reserve(r.points.size());
    for (const QPointF& ll : r.points) pts.append(lonLatToWorld(ll));

    const int leg = r.activeLeg;
    const bool haveActive = leg >= 0 && leg < pts.size();

    // Highlight the active leg (previous active point -> active waypoint).
    if (haveActive && leg >= 1) {
      b.setPen(accent, 4.0f);
      b.noBrush();
      b.drawPolyline(QList<QPointF>{pts[leg - 1], pts[leg]});
    }

    // Ship-to-active rubber-band line (wx g_bShowShipToActive): from the boat
    // straight to the active waypoint, so the steer-to target is unmistakable.
    if (own.valid && haveActive) {
      const QPointF shipW = lonLatToWorld(QPointF(own.lon, own.lat));
      b.setPen(accent, 1.5f);
      b.noBrush();
      b.drawPolyline(QList<QPointF>{shipW, pts[leg]});
    }

    // Emphasise the active waypoint with a ring.
    if (haveActive) {
      b.noBrush();
      b.setPen(accent, 2.0f);
      b.drawCircle(pts[leg], static_cast<float>(8.0 * wpp));
    }
  }
}

void TrackLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  // Historical tracks: the configured colour (brown by default), dashed -- a
  // breadcrumb trail distinct from the solid route line. The live recording
  // track keeps its own (provider) colour and is drawn solid so it stands out;
  // the selected track gets a cyan emphasis.
  const RouteDefaultsConfig& rc = RouteDefaultsConfig::instance();
  const QColor trackBase = rc.trackColor();
  const bool highlight = rc.trackHighlight();  // wide translucent underlay
  for (const NavTrack& t : provider()->tracks()) {
    if (!t.visible) continue;          // per-track eye
    if (t.points.size() < 2) continue;
    QList<QPointF> pts;
    pts.reserve(t.points.size());
    for (const QPointF& ll : t.points) pts.append(lonLatToWorld(ll));
    const bool selected = !m_selected.isEmpty() && t.guid == m_selected;
    const QColor col =
        selected ? QColor(90, 200, 255) : (t.active ? t.color : trackBase);
    // Highlight (g_bHighliteTracks): a fat translucent halo under the line.
    if (highlight) {
      QColor halo = col;
      halo.setAlphaF(0.30);
      b.setPen(halo, 7.0f);
      b.noBrush();
      b.drawPolyline(pts);  // solid, no dash, beneath
    }
    b.setPen(col, selected ? 2.5f : 1.5f);
    if (!t.active) b.setDash(6.0f, 4.0f);  // historical = dashed; live = solid
    b.noBrush();
    b.drawPolyline(pts);
    b.noDash();
  }
}

void WaypointLayer::draw(SgBuilder& b, double wpp) {
  if (!provider()) return;
  // Display 1:N for SCAMIN decluttering (approx; cos-lat ~ 1 is fine for a
  // visibility threshold). A mark with SCAMIN set hides when zoomed out past it.
  const double scale = currentScale();  // px per degree
  const double displayN =
      scale > 0.0 ? 111320.0 * 3.78 * 1000.0 / scale : 1.0e12;
  for (const NavWaypoint& wp : provider()->waypoints()) {
    if (!wp.visible) continue;  // the per-mark eye (default on)
    if (wp.scamin > 0 && displayN > wp.scamin) continue;  // SCAMIN declutter
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
