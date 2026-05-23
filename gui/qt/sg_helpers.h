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
 * Shared scene-graph node/material factory helpers (P2.4 materials catalog).
 *
 * Every Layer/provider that emits geometry was hand-rolling the same five
 * steps: `new QSGGeometryNode`, `new QSGGeometry`, set the drawing mode,
 * `new QSGFlatColorMaterial`/`QSGTextureMaterial`, then the
 * `OwnsGeometry`/`OwnsMaterial` flags. This module centralises that so the
 * built-in-material decision lives in one place.
 *
 * The materials catalog (which legacy GL shader maps to which built-in
 * `QSGMaterial`, and the residual custom-shader candidates) is documented in
 * `Docs/QT_MIGRATION_MATERIALS.md`. The short version: the chart core needs
 * only Qt's built-in materials --
 *   - solid fills/lines   -> `QSGFlatColorMaterial` (here)
 *   - per-vertex colour    -> `QSGVertexColorMaterial`
 *   - textured quads/tiles -> `QSGTextureMaterial` (here) / `QSGImageNode`
 * -- so these factories cover the whole vocabulary in use. Custom
 * `QSGMaterialShader`s remain an optional fidelity upgrade (AA-line), gated
 * on P2.12 profiling, not a baseline dependency.
 */

#ifndef OCPN_QT_SG_HELPERS_H_
#define OCPN_QT_SG_HELPERS_H_

#include <QColor>
#include <QSGGeometry>

class QSGGeometryNode;
class QSGTexture;

namespace ocpn::qtui::sg {

/**
 * A `QSGGeometryNode` with `Point2D` geometry and a `QSGFlatColorMaterial`.
 * The node owns both. `vertex_count` may be 0 to allocate the vertices later
 * (call `node->geometry()->allocate(n)`); fill them via
 * `node->geometry()->vertexDataAsPoint2D()`.
 *
 * `mode` is e.g. `QSGGeometry::DrawTriangles` / `DrawLines` / `DrawLineStrip`.
 * `line_width` only matters for the line modes (the Qt RHI caps it at 1 on
 * most backends -- wide lines use the parallel-strip technique, not this).
 */
QSGGeometryNode* makeFlatColorNode(const QColor& color,
                                   QSGGeometry::DrawingMode mode,
                                   int vertex_count, float line_width = 1.0f);

/**
 * A `QSGGeometryNode` with `TexturedPoint2D` geometry (interleaved
 * position + UV) and a `QSGTextureMaterial`. The node owns the geometry and
 * the material; the material does NOT own `texture` (Qt convention) -- the
 * caller keeps the `QSGTexture*` alive for the node's lifetime (e.g. via the
 * texture cache, P2.5). Pass `blending = true` when the texture has
 * transparent texels (pattern fills with gaps).
 *
 * Fill the vertices via `node->geometry()->vertexDataAsTexturedPoint2D()`.
 */
QSGGeometryNode* makeTextureNode(QSGTexture* texture,
                                 QSGGeometry::DrawingMode mode,
                                 int vertex_count, bool blending);

}  // namespace ocpn::qtui::sg

#endif  // OCPN_QT_SG_HELPERS_H_
