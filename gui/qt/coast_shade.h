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
 * Coastline land-shade primitive: a soft "inner glow" along land boundaries,
 * darkening the land just inside the coast and fading to transparent over a
 * few pixels, so land areas lift off the water. Built from closed land-ring
 * contours; the land side is taken from each ring's winding (signed area).
 *
 * Implemented as a one-sided gradient band via a custom QSGMaterialShader
 * (shaders/coastshade.{vert,frag}): screen-space expansion to the landward
 * side (constant pixel width at any zoom) + an alpha ramp in the fragment
 * shader. Shares the design of aa_line.h.
 */

#ifndef OCPN_QT_COAST_SHADE_H_
#define OCPN_QT_COAST_SHADE_H_

#include <functional>

#include <QColor>
#include <QList>
#include <QPointF>
#include <QSGMaterial>
#include <QSGMaterialShader>

class QSGGeometryNode;

namespace ocpn::qtui {

class CoastShadeMaterial : public QSGMaterial {
public:
  CoastShadeMaterial() {
    setFlag(Blending, true);
    setFlag(NoBatching, true);
    setFlag(RequiresFullMatrix, true);
  }
  QSGMaterialType* type() const override;
  QSGMaterialShader* createShader(
      QSGRendererInterface::RenderMode) const override;
  int compare(const QSGMaterial* other) const override;

  QColor color = QColor(0, 0, 0);
  float widthPx = 6.0f;     // inland fade distance, logical px
  float maxAlpha = 0.35f;   // opacity at the coast edge
};

/**
 * Predicate: keep (shade) the segment a->b? Used to skip artificial polygon
 * clip edges (basemap tile-grid lines, ENC cell-boundary cuts) so only real
 * coastline is shaded. Both points are in world coords (x = lon, y = -lat).
 * A null predicate keeps every segment.
 */
using ShadeEdgeFilter = std::function<bool(const QPointF&, const QPointF&)>;

/**
 * Build a QSGGeometryNode rendering an inland shade band along `contours`
 * (each a closed land-ring in world coords: x = lon, y = -lat). `width_px`
 * is the inland fade distance and `max_alpha` the darkness at the coast.
 * The land side of each ring is derived from its winding (so the inward
 * normals are correct even where segments are skipped). `keep`, if set,
 * suppresses clip-edge segments. Returns nullptr if there's nothing to draw.
 * The node owns its geometry + material.
 */
QSGGeometryNode* makeCoastShadeNode(const QList<QList<QPointF>>& contours,
                                    const QColor& color, float width_px,
                                    float max_alpha,
                                    const ShadeEdgeFilter& keep = {});

}  // namespace ocpn::qtui

#endif  // OCPN_QT_COAST_SHADE_H_
