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
 * NavLayer -- common base for the world-anchored nav-overlay Layers (P2.11):
 * AIS, own ship, routes/tracks/waypoints.
 *
 * Provides the shared plumbing only: subscribe to a NavDataProvider's
 * changed() and to viewport zoom (so screen-fixed symbol sizes can re-scale),
 * declare the WorldAnchored anchor, and offer geometry helpers. Each subclass
 * implements updateSubtree() with the retention strategy that suits it:
 *   - Dynamic layers (AIS, own ship) RETAIN per-target nodes and, each tick,
 *     mutate only their QSGTransformNode matrices (position/heading) -- no
 *     geometry rebuild -- mirroring the S-52 billboard approach.
 *   - Static layers (routes/tracks/waypoints) may rebuild wholesale via
 *     SgBuilder, since they change rarely and the world transform handles
 *     pan/zoom for free.
 *
 * World convention (viewport.h): x = lon, y = -lat. Heading th (deg true,
 * 0 = north, clockwise) is a screen-correct direction in the equirectangular
 * prototype projection: unit world vector = (sin th, -cos th) (east = +x,
 * north = -y = up).
 */

#ifndef OCPN_QT_NAV_LAYER_H_
#define OCPN_QT_NAV_LAYER_H_

#include <cmath>

#include <QPointF>
#include <QSGNode>

#include "layer.h"
#include "nav_data_provider.h"
#include "sg_builder.h"
#include "viewport.h"

namespace ocpn::qtui {

class NavLayer : public Layer {
public:
  NavLayer(NavDataProvider* provider, const Viewport* viewport,
           QObject* parent = nullptr)
      : Layer(parent), m_provider(provider), m_viewport(viewport) {
    if (m_provider)
      connect(m_provider, &NavDataProvider::changed, this, &Layer::dirty);
    // Re-render on zoom so screen-fixed symbol sizes track the scale; a pure
    // pan needs no rebuild (the world-anchored root transform moves us).
    if (m_viewport) {
      m_last_scale = m_viewport->scale();
      connect(m_viewport, &Viewport::changed, this, [this]() {
        const double s = m_viewport->scale();
        if (s != m_last_scale) {
          m_last_scale = s;
          Q_EMIT dirty();
        }
      });
    }
  }

  Anchor anchor() const override { return WorldAnchored; }

protected:
  const NavDataProvider* provider() const { return m_provider; }

  /** Current viewport scale (pixels per degree); 1.0 if unavailable. */
  double currentScale() const {
    return (m_viewport && m_viewport->scale() > 0.0) ? m_viewport->scale()
                                                     : 1.0;
  }
  /** World units per screen pixel -- multiply a desired pixel size by this to
   *  get the world-space size for a screen-fixed symbol. */
  double worldPerPx() const { return 1.0 / currentScale(); }

  /** True if the scale changed since the last call -- subclasses use this to
   *  decide whether to refresh scale-dependent transforms. */
  bool scaleChangedSince(double& cached) const {
    const double s = currentScale();
    if (s == cached) return false;
    cached = s;
    return true;
  }

  // Geographic (lat, lon) -> world point (x = lon, y = -lat).
  static QPointF world(double lat, double lon) { return QPointF(lon, -lat); }

  // Screen-correct heading unit vector in world space.
  static QPointF headingVec(double cog_deg) {
    const double th = cog_deg * M_PI / 180.0;
    return QPointF(std::sin(th), -std::cos(th));
  }

private:
  NavDataProvider* m_provider;
  const Viewport* m_viewport;
  double m_last_scale = 0.0;
};

/**
 * Base for STATIC nav overlays (routes, tracks, waypoints): geometry that
 * changes rarely and is screen-correct under the world transform, so a
 * wholesale rebuild via SgBuilder is fine (NavLayer only re-fires on data
 * change and zoom, never on pan). Subclasses implement draw().
 */
class StaticNavLayer : public NavLayer {
public:
  using NavLayer::NavLayer;

  QSGNode* updateSubtree(QSGNode* /*old*/, QQuickWindow* window) override {
    // Rebuild wholesale and return a fresh root; the compositor frees the
    // previous subtree (do NOT delete `old` here -- that double-frees).
    auto* root = new QSGNode();
    SgBuilder b(root, window);
    draw(b, worldPerPx());
    return root;
  }

protected:
  // Build the overlay into `b`. `world_per_px` sizes screen-fixed elements
  // (line widths, dot radii, label quads) in world units.
  virtual void draw(SgBuilder& b, double world_per_px) = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NAV_LAYER_H_
