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
 * Shared anti-aliased line primitive (P2.4 #5 / P2.13).
 *
 * A route, a track, an AIS vector and a depth contour are all the same
 * thing -- a polyline with a width, a colour and an optional dash -- so
 * they all render through this one path instead of the old per-consumer
 * workarounds (the S-52 parallel-1px-strip stack and SgBuilder's quad
 * expansion, both there only because the RHI caps the GL line primitive at
 * 1 px).
 *
 * `AaLineMaterial` is a custom QSGMaterial whose shader (gui/qt/shaders/
 * aaline.{vert,frag}, baked to .qsb) expands the centreline into a quad in
 * SCREEN space: line width is a device-pixel uniform applied after the MVP,
 * so the on-screen width is constant at any zoom -- the geometry never
 * needs rebuilding on pan or zoom. Edges are anti-aliased by a distance
 * feather; an optional dash pattern runs along the (screen-fixed) arc
 * length.
 *
 * Build a node with makeAaLineNode(); width / dash are in LOGICAL pixels
 * (the shader scales by the device pixel ratio).
 */

#ifndef OCPN_QT_AA_LINE_H_
#define OCPN_QT_AA_LINE_H_

#include <QColor>
#include <QList>
#include <QPointF>
#include <QSGMaterial>
#include <QSGMaterialShader>

class QSGGeometryNode;

namespace ocpn::qtui {

class AaLineMaterial : public QSGMaterial {
public:
  AaLineMaterial() {
    setFlag(Blending, true);
    // Custom vertex attributes + a per-node full MVP (read as qt_Matrix in
    // the shader): the node must NOT be merged into a batch, or the batch
    // renderer rewrites the vertex shader and appends an attribute the
    // geometry doesn't supply (Metal: "vertex attribute index ... must be
    // < 31"). NoBatching forces the unmerged path.
    setFlag(NoBatching, true);
    setFlag(RequiresFullMatrix, true);
  }

  QSGMaterialType* type() const override;
  QSGMaterialShader* createShader(
      QSGRendererInterface::RenderMode) const override;
  int compare(const QSGMaterial* other) const override;

  QColor color = QColor(0, 0, 0);
  // compare() runs O(n^2)-ish inside prepareAlphaBatches with thousands
  // of chart-line elements (the measured pan hitch); it reads this
  // cached key, NOT QColor::rgba() (which alone was ~half the cost).
  // Keep it in sync when setting `color`.
  QRgb rgbaKey = 0xff000000;
  float widthPx = 1.0f;     // logical px
  float dashOnPx = 0.0f;    // 0 -> solid
  float dashOffPx = 0.0f;
  // Pencil/graphite look (routes): 0 = clean AA line; 1 = grainy stroke with a
  // rough (toothed) edge and modest along-stroke pressure variation. Ruler
  // straight -- no geometry change, purely a fragment-coverage modulation.
  float pencil = 0.0f;
};

/**
 * Build a QSGGeometryNode rendering `world_pts` (world coords: x = lon,
 * y = -lat) as an anti-aliased line of `width_px` logical pixels in `color`.
 * `closed` joins the last point back to the first. `dash_on_px`/`dash_off_px`
 * (logical px) give a dash pattern; both 0 = solid. Returns nullptr for < 2
 * points. The node owns its geometry + material.
 */
QSGGeometryNode* makeAaLineNode(const QList<QPointF>& world_pts,
                                const QColor& color, float width_px,
                                bool closed = false, float dash_on_px = 0.0f,
                                float dash_off_px = 0.0f, float pencil = 0.0f);

/**
 * Many polylines of the SAME style in one geometry node (PERF-3): each
 * polyline renders exactly as a separate node would (independent segment
 * quads; the dash phase restarts per polyline), but the scene graph
 * carries one node instead of hundreds. Polylines with < 2 points are
 * skipped; returns nullptr if none remain.
 */
QSGGeometryNode* makeAaLineNode(const QList<QList<QPointF>>& polylines,
                                const QColor& color, float width_px,
                                float dash_on_px = 0.0f,
                                float dash_off_px = 0.0f);

}  // namespace ocpn::qtui

#endif  // OCPN_QT_AA_LINE_H_
