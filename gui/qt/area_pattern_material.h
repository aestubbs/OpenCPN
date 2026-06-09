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
 * AreaPatternMaterial -- a custom QSGMaterial that tiles a pattern texture by
 * computing the tile coordinate PER-FRAGMENT from a per-cell-relative world
 * offset (geometry attribute 1), instead of an interpolated per-vertex UV.
 *
 * S-52 AP area patterns (DRGARE dredged-area stipple, MARCUL marine farms, the
 * CATZOC overlays, ...) are filled over areas that the tessellator splits into
 * many polygons SMALLER than one pattern tile. With the stock QSGTextureMaterial
 * (interpolated UV + QSGTexture::Repeat) each such polygon sampled a single
 * clamped edge texel -- the area went flat and the sparse stipple vanished. This
 * material reproduces wx's per-pixel pattern fill: the fragment derives the tile
 * coord from the world position and wraps it with fract(), so the pattern tiles
 * continuously regardless of tessellation. See areapattern.{vert,frag}.
 *
 * Geometry uses the stock TexturedPoint2D attribute set, but attribute 1 (the
 * "texcoord") carries the world offset (worldPos - cell reference), NOT a UV.
 * kScale (= viewport scale / tilePx, per axis) converts that offset to tile
 * repeats and is updated on zoom.
 */

#ifndef OCPN_QT_AREA_PATTERN_MATERIAL_H_
#define OCPN_QT_AREA_PATTERN_MATERIAL_H_

#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QVector2D>

class QSGTexture;
class QSGGeometryNode;

namespace ocpn::qtui {

class AreaPatternMaterial : public QSGMaterial {
 public:
  AreaPatternMaterial() {
    setFlag(Blending, true);
    // Opt out of MERGED batching. The batch renderer's merge path rewrites the
    // vertex shader to inject a z-order attribute and asserts on a custom
    // textured material ("No rewriter-inserted attribute found" -> Metal vertex
    // attribute index -1). RequiresFullMatrix makes each node its own draw with
    // its own combined matrix (which the shader already uses) -- no merge, no
    // rewriter. Pattern nodes carry distinct textures so merging gained little.
    setFlag(RequiresFullMatrix, true);
  }

  QSGMaterialType* type() const override;
  QSGMaterialShader* createShader(
      QSGRendererInterface::RenderMode) const override;
  int compare(const QSGMaterial* other) const override;

  QSGTexture* texture = nullptr;  // BORROWED (owned by the TextureCacheNode)
  QVector2D kScale{1.0f, 1.0f};   // world offset -> tile repeats, per axis
};

// Build a pattern-fill node with `vertex_count` TexturedPoint2D vertices
// (position = world coord, texcoord = world offset from the cell reference).
// The caller fills the vertex data and sets the material's kScale.
QSGGeometryNode* makeAreaPatternNode(QSGTexture* texture, int vertex_count);

}  // namespace ocpn::qtui

#endif  // OCPN_QT_AREA_PATTERN_MATERIAL_H_
