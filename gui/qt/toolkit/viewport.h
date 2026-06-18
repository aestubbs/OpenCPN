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
 *   y = -(Mercator latitude) -- "Y-down" world space so north is up without a
 *       Y-flip in the transform matrix.
 *
 * Projection: SPHERICAL MERCATOR, matching the wx app's toSM (georef.cpp):
 * y_merc = ln(tan(pi/4 + lat/2)) = atanh(sin lat). We express it in
 * DEGREE-EQUIVALENT units (x = lon in degrees, y = mercator-lat scaled so
 * dy/dlat = 1 at the equator), so a single `m_scale` (px per degree of
 * longitude) drives both axes -- conformal: local shapes are correct and
 * rhumb lines are straight, eliminating the plate-carrée high-latitude skew.
 * Latitude is clamped near the poles where Mercator diverges (~±85.05°).
 *
 * Helpers (the SINGLE place the projection lives -- every layer routes its
 * lat/lon -> world conversion through these):
 *   - `lonToWorldX(lon)` = lon
 *   - `latToWorldY(lat)` = -(180/pi) ln(tan(pi/4 + lat/2))
 *   - `worldYToLat(y)`   = inverse of the above
 */

#ifndef OCPN_QT_VIEWPORT_H_
#define OCPN_QT_VIEWPORT_H_

#include <algorithm>
#include <cmath>

#include <QMatrix4x4>
#include <QObject>
#include <QRectF>

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
    clampCenter();
    emit changed();
  }

  // The world extent used for the zoom-out fit + recentre. Full longitude, but
  // latitude is trimmed at the south to kFitMinLat -- under Mercator the deep
  // south (Antarctica) is hugely stretched and just wastes screen height in the
  // world view, so the fit stops there. (It is only the FIT/centre bound; when
  // zoomed in you can still pan freely further south.)
  static constexpr double kFitMaxLat = 85.05113;   // north (= kMercMaxLat)
  static constexpr double kFitMinLat = -85.05113;  // south (full world)

  // Smallest scale (most zoomed out) that still fits the whole fit-extent world
  // inside the canvas: the world fills the BINDING window dimension edge-to-edge
  // and letterboxes the other. Stops the zoom-out from shrinking the map to a
  // dot. 0 canvas (pre-layout) falls back to the fixed kMinScale.
  static double minScaleForCanvas(int w, int h) {
    if (w <= 0 || h <= 0) return kMinScale;
    const double worldX = 360.0;  // lon -180..180
    const double worldY = latToWorldY(kFitMinLat) - latToWorldY(kFitMaxLat);
    return std::max(kMinScale, std::min(w / worldX, h / worldY));
  }

  // LONGITUDE scrolls continuously round the world (no edge, no clamp) -- the
  // basemap renders the world repeated across the antimeridian. LATITUDE is
  // letterboxed: when the view is taller than the fit-extent world, centre it
  // (it can't be panned off into the background); zoomed in, lat panning is
  // free. (m_center_lon is left un-normalised so the view never jumps across
  // the seam; double precision is ample for many laps.)
  void clampCenter() {
    if (m_canvas_h <= 0 || m_scale <= 0.0) return;
    const double yTop = latToWorldY(kFitMaxLat);  // north (more negative)
    const double yBot = latToWorldY(kFitMinLat);  // south
    if (m_canvas_h / m_scale >= (yBot - yTop))     // view taller than world
      m_center_lat = worldYToLat(0.5 * (yTop + yBot));
  }

  void setScale(double s) {
    if (s <= 0.0) return;
    s = std::min(std::max(s, minScaleForCanvas(m_canvas_w, m_canvas_h)),
                 kMaxScale);
    if (m_scale == s) return;
    m_scale = s;
    clampCenter();
    emit changed();
  }

  void setRotation(double radians) {
    if (m_rotation == radians) return;
    m_rotation = radians;
    emit changed();
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
    // Pan in world (Mercator) Y, then invert back to latitude. A mouse drag
    // DOWN moves the viewport to look further SOUTH.
    m_center_lat = worldYToLat(latToWorldY(m_center_lat) - wdy);
    clampCenter();
    emit changed();
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
    // Clamp zoom-out to "whole world fits the canvas" (letterboxed), not the
    // fixed floor; use the passed canvas size (most current).
    const double fitMin = minScaleForCanvas(canvas_w, canvas_h);
    if (m_scale < fitMin) m_scale = fitMin;
    if (m_scale > kMaxScale) m_scale = kMaxScale;
    // Recompute centre so that the same world point lands under (sx, sy).
    const double c = std::cos(m_rotation), s = std::sin(m_rotation);
    const double srx = sx - canvas_w / 2.0, sry = sy - canvas_h / 2.0;
    const double wrx = (c * srx + s * sry) / m_scale;
    const double wry = (-s * srx + c * sry) / m_scale;
    m_center_lon = w_lon - wrx;
    m_center_lat = worldYToLat(latToWorldY(w_lat) - wry);
    clampCenter();
    emit changed();
  }

  /** Build the world→screen matrix for the WorldAnchored root.
   *  World coords: x = lon, y = -lat. Order: centre, rotate, scale, -world. */
  QMatrix4x4 transformMatrix(int canvas_w, int canvas_h) const {
    QMatrix4x4 m;
    m.translate(canvas_w / 2.0f, canvas_h / 2.0f);
    if (m_rotation != 0.0)
      m.rotate(static_cast<float>(m_rotation * 180.0 / M_PI), 0.0f, 0.0f, 1.0f);
    m.scale(static_cast<float>(m_scale), static_cast<float>(m_scale));
    // Translate by -worldCentre. World Y = latToWorldY(centre_lat) (Mercator,
    // Y-down), so the Y translation is -that.
    m.translate(static_cast<float>(-m_center_lon),
                static_cast<float>(-latToWorldY(m_center_lat)));
    return m;
  }

  // Near the poles Mercator -> +/-infinity; clamp like web-mercator.
  static constexpr double kMercMaxLat = 85.05113;
  static inline double lonToWorldX(double lon) { return lon; }
  static inline double latToWorldY(double lat) {
    const double L = lat < -kMercMaxLat ? -kMercMaxLat
                                        : (lat > kMercMaxLat ? kMercMaxLat : lat);
    // -(180/pi) ln(tan(pi/4 + L/2)); degree-equivalent, Y-down.
    return -(180.0 / M_PI) * std::log(std::tan(M_PI / 4.0 + L * M_PI / 360.0));
  }
  static inline double worldYToLat(double y) {
    // Inverse: lat = (360/pi) atan(exp(-y*pi/180)) - 90.
    return (360.0 / M_PI) * std::atan(std::exp(-y * M_PI / 180.0)) - 90.0;
  }

  /** The canvas pixel size, set by ChartCanvas on resize. Lets layers compute
   *  the visible world rectangle for view-frustum culling without each having
   *  to know the canvas geometry. */
  void setCanvasSize(int w, int h) {
    if (m_canvas_w == w && m_canvas_h == h) return;
    m_canvas_w = w;
    m_canvas_h = h;
    // A resize changes the zoom-out floor and the fit; re-clamp both so a
    // shrink can't leave us zoomed out past "world fits", and re-centre.
    m_scale = std::min(std::max(m_scale, minScaleForCanvas(w, h)), kMaxScale);
    clampCenter();
    emit changed();  // re-cull at the new extent
  }
  int canvasWidth() const { return m_canvas_w; }
  int canvasHeight() const { return m_canvas_h; }

  /** Axis-aligned world-space bounding box of the visible view, grown by
   *  `marginPx` screen pixels (so symbols straddling the edge aren't culled).
   *  World coords: x = lon, y = Mercator-lat (see latToWorldY). Accounts for
   *  chart rotation by taking the AABB of the four rotated view corners.
   *  Returns a huge rect (no culling) until the canvas size is known. */
  QRectF visibleWorldBounds(double marginPx = 0.0) const {
    if (m_canvas_w <= 0 || m_canvas_h <= 0 || m_scale <= 0.0)
      return QRectF(-1e9, -1e9, 2e9, 2e9);
    const double c = std::cos(m_rotation), s = std::sin(m_rotation);
    const double cx = m_center_lon, cy = latToWorldY(m_center_lat);
    const double m = marginPx;
    const double corners[4][2] = {{-m, -m},
                                  {m_canvas_w + m, -m},
                                  {m_canvas_w + m, m_canvas_h + m},
                                  {-m, m_canvas_h + m}};
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const auto& cor : corners) {
      const double srx = cor[0] - m_canvas_w / 2.0;
      const double sry = cor[1] - m_canvas_h / 2.0;
      const double wx = cx + (c * srx + s * sry) / m_scale;
      const double wy = cy + (-s * srx + c * sry) / m_scale;
      minX = std::min(minX, wx);
      maxX = std::max(maxX, wx);
      minY = std::min(minY, wy);
      maxY = std::max(maxY, wy);
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
  }

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
    lat = worldYToLat(latToWorldY(m_center_lat) + wry);
  }

signals:
  void changed();

private:
  static constexpr double kMinScale = 0.01;   // ~36000 px = whole world
  static constexpr double kMaxScale = 1.0e6;  // generous upper bound

  double m_center_lat = 52.5;   // sensible default: somewhere in the
  double m_center_lon = 5.0;    // North Sea, for the test chart
  double m_scale = 60.0;        // pixels per degree
  double m_rotation = 0.0;      // chart rotation, radians (0 = north-up)
  int m_canvas_w = 0;           // canvas pixel size, set by ChartCanvas; 0 until
  int m_canvas_h = 0;           // known (visibleWorldBounds then declines to cull)
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_VIEWPORT_H_
