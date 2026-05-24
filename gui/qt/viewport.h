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

  /** Pan by a screen-pixel delta (e.g. from a mouse drag). dx/dy are in
   *  Qt's screen coords (x right, y down). */
  void panBy(double dx_pixels, double dy_pixels) {
    if (dx_pixels == 0.0 && dy_pixels == 0.0) return;
    m_center_lon -= dx_pixels / m_scale;
    // Y is "down" in screen and in our world convention (y = -lat), so a
    // mouse drag DOWN moves the viewport to look further SOUTH (lat
    // decreases).
    m_center_lat += dy_pixels / m_scale;
    Q_EMIT changed();
  }

  /** Zoom by `factor` (>1 zooms in) about a screen-space point.
   *  The world point under (sx, sy) stays fixed under the cursor. */
  void zoomAt(double sx, double sy, double factor,
              int canvas_w, int canvas_h) {
    if (factor <= 0.0 || factor == 1.0) return;
    // World point under the cursor before:
    const double world_x = m_center_lon + (sx - canvas_w / 2.0) / m_scale;
    const double world_y_down = -m_center_lat
                                + (sy - canvas_h / 2.0) / m_scale;
    m_scale *= factor;
    if (m_scale < kMinScale) m_scale = kMinScale;
    if (m_scale > kMaxScale) m_scale = kMaxScale;
    // Recompute centre so that the same world point lands under (sx, sy).
    m_center_lon = world_x - (sx - canvas_w / 2.0) / m_scale;
    m_center_lat = -(world_y_down - (sy - canvas_h / 2.0) / m_scale);
    Q_EMIT changed();
  }

  /** Build the world→screen matrix for the WorldAnchored root.
   *  World coords: x = lon, y = -lat. */
  QMatrix4x4 transformMatrix(int canvas_w, int canvas_h) const {
    QMatrix4x4 m;
    m.translate(canvas_w / 2.0f, canvas_h / 2.0f);
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
   *  screen-pixel point (item-local coords). */
  void screenToLatLon(double sx, double sy, int canvas_w, int canvas_h,
                      double& lat, double& lon) const {
    lon = m_center_lon + (sx - canvas_w / 2.0) / m_scale;
    lat = m_center_lat - (sy - canvas_h / 2.0) / m_scale;
  }

Q_SIGNALS:
  void changed();

private:
  static constexpr double kMinScale = 0.01;   // ~36000 px = whole world
  static constexpr double kMaxScale = 1.0e6;  // generous upper bound

  double m_center_lat = 52.5;   // sensible default: somewhere in the
  double m_center_lon = 5.0;    // North Sea, for the test chart
  double m_scale = 60.0;        // pixels per degree
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_VIEWPORT_H_
