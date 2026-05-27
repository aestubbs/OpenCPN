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
 * Viewport -- the chart-canvas viewport: where on Earth we're looking and at
 * what scale.
 *
 * Holds the centre (lat/lon) and a scale in pixels per degree. Provides the
 * world→screen transform matrix that ChartCanvas applies to the World-
 * anchored scene-graph root. Emits `changed()` when pan/zoom mutates the
 * state so the canvas can schedule a repaint.
 *
 * World-coordinate convention used by Layers attached to the World-anchored
 * root:
 *   x = longitude (degrees east, positive)
 *   y = -latitude (south positive) -- "Y-down" world space so that
 *       QSGImageNode rectangles (which take positive width/height starting
 *       at top-left) end up oriented with north at the top of the screen
 *       without needing a Y-flip in the transform matrix.
 *
 * Helpers:
 *   - `lonToWorldX(double lon)` = lon
 *   - `latToWorldY(double lat)` = -lat
 *
 * Equirectangular for the prototype (lat and lon scaled equally). Mercator
 * / proper latitude scaling comes later when real chart projections matter.
 */

#ifndef OCPN_QT_VIEWPORT_H_
#define OCPN_QT_VIEWPORT_H_

#include <cmath>

#include <QMatrix4x4>
#include <QObject>

namespace ocpn::qtui {

class Viewport : public QObject {
  Q_OBJECT

public:
  explicit Viewport(QObject* parent = nullptr) : QObject(parent) {}

  double centerLat() const { return m_center_lat; }
  double centerLon() const { return m_center_lon; }
  /** Pixels per degree of lon/lat. Higher = more zoomed in. */
  double scale() const { return m_scale; }
  /** Chart rotation, radians. 0 = north-up. Positive rotates the chart so a
   *  heading/course points up (Course-Up / Head-Up). */
  double rotation() const { return m_rotation; }

  void setCenter(double lat, double lon) {
    if (m_center_lat == lat && m_center_lon == lon) return;
    m_center_lat = lat;
    m_center_lon = lon;
    Q_EMIT changed();
  }

  void setScale(double s) {
    if (s <= 0.0 || m_scale == s) return;
    m_scale = s;
    Q_EMIT changed();
  }

  void setRotation(double radians) {
    if (m_rotation == radians) return;
    m_rotation = radians;
    Q_EMIT changed();
  }

  /** Pan by a screen-pixel delta (e.g. from a mouse drag). dx/dy are in
   *  Qt's screen coords (x right, y down). Accounts for chart rotation. */
  void panBy(double dx_pixels, double dy_pixels) {
    if (dx_pixels == 0.0 && dy_pixels == 0.0) return;
    // Un-rotate the screen delta into world axes before scaling to degrees.
    const double c = std::cos(m_rotation), s = std::sin(m_rotation);
    const double wdx = (c * dx_pixels + s * dy_pixels) / m_scale;
    const double wdy = (-s * dx_pixels + c * dy_pixels) / m_scale;
    m_center_lon -= wdx;
    // Y is "down" in screen and in our world convention (y = -lat), so a
    // mouse drag DOWN moves the viewport to look further SOUTH.
    m_center_lat += wdy;
    Q_EMIT changed();
  }

  /** Zoom by `factor` (>1 zooms in) about a screen-space point.
   *  The world point under (sx, sy) stays fixed under the cursor. */
  void zoomAt(double sx, double sy, double factor,
              int canvas_w, int canvas_h) {
    if (factor <= 0.0 || factor == 1.0) return;
    // World point under the cursor before the zoom.
    double w_lat, w_lon;
    screenToLatLon(sx, sy, canvas_w, canvas_h, w_lat, w_lon);
    m_scale *= factor;
    if (m_scale < kMinScale) m_scale = kMinScale;
    if (m_scale > kMaxScale) m_scale = kMaxScale;
    // Recompute centre so that the same world point lands under (sx, sy).
    const double c = std::cos(m_rotation), s = std::sin(m_rotation);
    const double srx = sx - canvas_w / 2.0, sry = sy - canvas_h / 2.0;
    const double wrx = (c * srx + s * sry) / m_scale;
    const double wry = (-s * srx + c * sry) / m_scale;
    m_center_lon = w_lon - wrx;
    m_center_lat = w_lat + wry;
    Q_EMIT changed();
  }

  /** Build the world→screen matrix for the WorldAnchored root.
   *  World coords: x = lon, y = -lat. Order: centre, rotate, scale, -world. */
  QMatrix4x4 transformMatrix(int canvas_w, int canvas_h) const {
    QMatrix4x4 m;
    m.translate(canvas_w / 2.0f, canvas_h / 2.0f);
    if (m_rotation != 0.0)
      m.rotate(static_cast<float>(m_rotation * 180.0 / M_PI), 0.0f, 0.0f, 1.0f);
    m.scale(static_cast<float>(m_scale), static_cast<float>(m_scale));
    // World coords are Y-down (y = -lat). With centre_lat above the
    // equator, world centre y is negative; translating by -world_centre
    // moves it to the screen centre.
    m.translate(static_cast<float>(-m_center_lon),
                static_cast<float>(m_center_lat));
    return m;
  }

  static inline double lonToWorldX(double lon) { return lon; }
  static inline double latToWorldY(double lat) { return -lat; }

  /** Inverse of the world->screen mapping: the geographic position under a
   *  screen-pixel point (item-local coords). Inverts the chart rotation. */
  void screenToLatLon(double sx, double sy, int canvas_w, int canvas_h,
                      double& lat, double& lon) const {
    const double c = std::cos(m_rotation), s = std::sin(m_rotation);
    const double srx = sx - canvas_w / 2.0, sry = sy - canvas_h / 2.0;
    // R(-rotation) * screen_rel / scale.
    const double wrx = (c * srx + s * sry) / m_scale;
    const double wry = (-s * srx + c * sry) / m_scale;
    lon = m_center_lon + wrx;
    lat = m_center_lat - wry;
  }

Q_SIGNALS:
  void changed();

private:
  static constexpr double kMinScale = 0.01;   // ~36000 px = whole world
  static constexpr double kMaxScale = 1.0e6;  // generous upper bound

  double m_center_lat = 52.5;   // sensible default: somewhere in the
  double m_center_lon = 5.0;    // North Sea, for the test chart
  double m_scale = 60.0;        // pixels per degree
  double m_rotation = 0.0;      // chart rotation, radians (0 = north-up)
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_VIEWPORT_H_
