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
 * Deliberately Qt-free (plain structs + std::vector): the decode can run
 * off the scene-graph render thread, and the buffer is unit-testable
 * without a GPU. gui/qt translates it into QSGGeometryNodes.
 */

#ifndef _S52_SG_H_
#define _S52_SG_H_

#include <cstdint>
#include <vector>

namespace s52sg {

/** A vertex in geographic coordinates. s52plib applies no projection to
 *  these -- the consumer maps (lon, lat) into its own world/screen space. */
struct Vertex {
  double lon;
  double lat;
};

/** Primitive topology. Triangle kinds back area fills; LineStrip backs
 *  line features (coastlines, depth contours, ...). Symbols and text are
 *  added in later P2.8c sub-steps. */
enum class PrimType { Triangles, TriangleStrip, TriangleFan, LineStrip };

/** One renderable batch: a vertex run with a resolved RGBA colour. For
 *  LineStrip, `width` is the pen width in millimetres (the consumer
 *  converts to device pixels); ignored for fills. */
struct Prim {
  PrimType type = PrimType::Triangles;
  std::vector<Vertex> verts;
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
  float width = 1.0f;
};

/** A decoded chart's geometry, ready for the consumer to upload. */
class Buffer {
public:
  std::vector<Prim> prims;
  void clear() { prims.clear(); }
  bool empty() const { return prims.empty(); }
};

}  // namespace s52sg

#endif  // _S52_SG_H_
