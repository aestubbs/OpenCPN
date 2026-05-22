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
#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>

namespace s52sg {

/** Primitive topology. Triangle kinds back area fills; LineStrip backs
 *  line features (coastlines, depth contours, ...). */
enum class PrimType { Triangles, TriangleStrip, TriangleFan, LineStrip };

/** One renderable batch: a run of geographic vertices (QPointF holding
 *  (lon, lat) -- s52plib applies no projection, the consumer maps into
 *  its own world/screen space) with a resolved colour. For LineStrip,
 *  `width` is the pen width; ignored for fills. */
// S-52 display-category rank: 0 = DISPLAYBASE (always shown), 1 = STANDARD,
// 2 = OTHER. The consumer shows items whose rank <= the selected level
// (Base / Standard / All).
enum DisplayCat { CatBase = 0, CatStandard = 1, CatOther = 2 };

struct Prim {
  PrimType type = PrimType::Triangles;
  QList<QPointF> verts;  // (lon, lat) per point
  QColor color;
  float width = 1.0f;
  int dispCat = CatStandard;
};

/** A point symbol placement (buoy, beacon, ...). `image` is the symbol
 *  bitmap cropped from the S-52 raster atlas; `pos` is its geographic
 *  anchor; `pivot` is the pixel offset within the image that sits on the
 *  anchor. Screen-fixed size -- the consumer billboards it (world
 *  position, screen-pixel size). */
struct Symbol {
  QPointF pos;     // (lon, lat) anchor
  QImage image;    // RGBA symbol bitmap
  QPointF pivot;   // pixel offset of the anchor within image
  // S-52 SCAMIN: the 1:N chart scale beyond which (more zoomed out) this
  // item is hidden. The s52plib "unset" sentinel (~1e8) means always show.
  int scamin = 100000002;
  int dispCat = CatStandard;
};

/** A text label (sounding, feature name, ...). Rendered by the consumer
 *  with a SYSTEM font (not the proprietary chart font engine); s52plib
 *  only resolves the string, colour and nominal point size. Billboarded
 *  like Symbol. `hjust`/`vjust` follow S-52 ('1' centre, '2' right/bottom,
 *  '3' left/top per S-52 convention; the consumer interprets). */
struct Label {
  QPointF pos;       // (lon, lat) anchor
  QString text;
  QColor color;
  float pointSize = 10.0f;
  char hjust = '1';
  char vjust = '1';
  // S-52 SCAMIN: hidden when the chart is more zoomed out than 1:scamin.
  int scamin = 100000002;
  // Soundings get spatial density declutter keeping the SHALLOWEST per
  // cell (safety). isSounding marks them; depth is the value in metres.
  bool isSounding = false;
  float depth = 0.0f;
  int dispCat = CatStandard;
};

/** A decoded chart's geometry, ready for the consumer to upload. */
class Buffer {
public:
  QList<Prim> prims;
  QList<Symbol> symbols;
  QList<Label> labels;
  void clear() {
    prims.clear();
    symbols.clear();
    labels.clear();
  }
  bool empty() const {
    return prims.isEmpty() && symbols.isEmpty() && labels.isEmpty();
  }
};

}  // namespace s52sg

#endif  // _S52_SG_H_
