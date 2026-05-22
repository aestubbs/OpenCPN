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
 * s52sg -- the world-coordinate geometry buffer emitted by
 * s52plib::RenderObjectToSG (P2.8c).
 *
 * This is the seam between the S-52 *symbology decode* (s52plib's job:
 * S-57 object + Look-Up rules -> "fill this polygon with colour DEPVS",
 * "draw this line with pen CHGRD") and the *display* (the Qt scene
 * graph's job: world->screen projection on the GPU, compositing).
 *
 * The classic s52plib render paths (RenderObjectToDC / RenderObjectToGL)
 * project geometry to *screen pixels* and rasterise immediately. The SG
 * path stops one step earlier: it resolves colours/pens/positions but
 * leaves vertices in *geographic* coordinates (degrees lon/lat). The
 * consumer applies its own world->screen transform, so pan/zoom needs no
 * re-decode and stays vector-sharp at every scale.
 *
 * Uses only Qt value types (QList / QPointF / QColor) -- no scene-graph or
 * GPU types -- so the decode can run off the render thread and the buffer
 * is testable without a GPU, while staying idiomatic Qt. gui/qt
 * translates it into QSGGeometryNodes.
 */

#ifndef _S52_SG_H_
#define _S52_SG_H_

#include <QColor>
#include <QList>
#include <QPointF>

namespace s52sg {

/** Primitive topology. Triangle kinds back area fills; LineStrip backs
 *  line features (coastlines, depth contours, ...). Symbols and text are
 *  added in later P2.8c sub-steps. */
enum class PrimType { Triangles, TriangleStrip, TriangleFan, LineStrip };

/** One renderable batch: a run of geographic vertices (QPointF holding
 *  (lon, lat) -- s52plib applies no projection, the consumer maps into
 *  its own world/screen space) with a resolved colour. For LineStrip,
 *  `width` is the pen width; ignored for fills. */
struct Prim {
  PrimType type = PrimType::Triangles;
  QList<QPointF> verts;  // (lon, lat) per point
  QColor color;
  float width = 1.0f;
};

/** A decoded chart's geometry, ready for the consumer to upload. */
class Buffer {
public:
  QList<Prim> prims;
  void clear() { prims.clear(); }
  bool empty() const { return prims.isEmpty(); }
};

}  // namespace s52sg

#endif  // _S52_SG_H_
