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
#include <QHash>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

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

// Per-feature-class viewing group, for the mariner-selectable display
// toggles (P2.9). Derived from the object's S-57 class (FeatureName) at emit
// time; the consumer can hide whole groups independently of the display
// category. VgOther is everything not separately toggleable.
enum ViewGroup { VgOther = 0, VgLights, VgBuoysBeacons };

struct Prim {
  PrimType type = PrimType::Triangles;
  QList<QPointF> verts;  // (lon, lat) per point
  QColor color;
  float width = 1.0f;
  // S-52 line dash, in MILLIMETRES (physical, screen-fixed). 0 = solid. For
  // LineStrip only: the consumer converts to logical px and runs the pattern
  // along the line's screen arc length. DASH ~ 2mm on / 1mm off; DOTT ~ 0.5/0.5.
  float dashOnMm = 0.0f;
  float dashOffMm = 0.0f;
  int dispCat = CatStandard;
  // S-52 SCAMIN: the 1:N chart scale beyond which (more zoomed out) this
  // fill/line is hidden. The "unset" sentinel (~1e8) means always show -- the
  // consumer only SCAMIN-culls a Prim that carries a real value, so an
  // un-SCAMIN'd area fill never vanishes (it must persist as the composite
  // underlay -- see Docs/QT_QUILT_VS_WX.md §10.3, P2.14).
  int scamin = 100000002;
  // S-52 display priority (0..9, from LUP DPRI). Lower draws first; group-1
  // areas sit at 1..3, line/area symbols above. The consumer draws prims in
  // priority order so a depth-area fill never paints over the pontoon/berth
  // lines that lie on it (wx renders via the same per-priority pass).
  int priority = 5;
  // Index into Buffer::classes (the source feature's S-57 class acronym), for
  // the per-class "User Standard Objects" filter (P3.6). -1 = unknown.
  int classIdx = -1;
};

/** An area filled with a repeated (tiled) pattern bitmap -- S-52 AP fills
 *  (CATZOC quality overlays, marine farms, marshes, dredged-area stipple,
 *  foul areas, ...). `tris` is the tessellated polygon as an independent
 *  triangle list in (lon, lat); `pattern` is the tile bitmap. The consumer
 *  tiles it at a fixed screen size (UVs recomputed on zoom), so the pattern
 *  density is constant regardless of zoom. Both raster (atlas) and vector
 *  (HPGL, rasterised at emit) patterns are supported; vector patterns carry
 *  the tile size here rather than deriving it from the image, since a
 *  staggered tile is baked double-height (see RenderToSGAP). */
struct PatternFill {
  QList<QPointF> tris;  // (lon, lat) triangle list
  QImage pattern;       // RGBA tile
  int scamin = 100000002;
  int dispCat = CatStandard;
  // Tiling period in logical px (screen-fixed). <= 0 means "derive from the
  // image dimensions" (the raster-pattern default the consumer already used).
  double tileW = 0.0;
  double tileH = 0.0;
  int classIdx = -1;  // index into Buffer::classes (per-class filter, P3.6)
};

/** One drawing op of a vector symbol: either line segments (vertex pairs)
 *  or filled triangles (vertex triples), in symbol-local pixel coords
 *  (relative to the symbol pivot at origin), with a resolved colour. */
struct VectorOp {
  bool filled = false;     // false = line segments, true = triangles
  QList<QPointF> verts;    // local pixel coords
  QColor color;
  float width = 1.0f;      // HPGL pen width (SW value); used by AP pattern raster
};

/** A vector (HPGL) point symbol -- buoys/beacons/light flares decoded from
 *  the S-52 vector definitions rather than the raster atlas. `ops` is the
 *  symbol's geometry in local pixel coords; the consumer billboards it
 *  (world position, screen-fixed size) like a raster Symbol. Crisp at any
 *  DPI and recolourable. */
struct VectorSymbol {
  QPointF pos;             // (lon, lat) anchor (== symbol pivot/hot-spot)
  QList<VectorOp> ops;
  int scamin = 100000002;
  int dispCat = CatStandard;
  int viewGroup = VgOther;
  int classIdx = -1;  // index into Buffer::classes (per-class filter, P3.6)
};

/** A complex (LC) line: an HPGL line-symbol walked along the polyline -- the
 *  wavy submarine-cable glyph, the T-shapes of a restricted-area border, etc.
 *  `symbol` is the glyph's geometry in symbol-local pixels (pivot at origin),
 *  `lengthPx` its repeat length along the line. The consumer walks `path` in
 *  screen space, stamping the rotated symbol every `lengthPx`, rebuilt on zoom
 *  so the glyph stays screen-fixed (like wx draw_lc_poly). `color` backs the
 *  geometry node when the ops are uncoloured. */
struct ComplexLine {
  QList<QPointF> path;      // (lon, lat) polyline
  QList<VectorOp> symbol;   // local screen-px ops (pivot at origin)
  float lengthPx = 10.0f;   // repeat length along the line, screen px
  QColor color;
  int dispCat = CatStandard;
  int priority = 5;
  int scamin = 100000002;
  int viewGroup = VgOther;
  int classIdx = -1;  // index into Buffer::classes (per-class filter, P3.6)
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
  double rotationDeg = 0.0;  // symbol rotation about the pivot (S-52 SY angle)
  // S-52 SCAMIN: the 1:N chart scale beyond which (more zoomed out) this
  // item is hidden. The s52plib "unset" sentinel (~1e8) means always show.
  int scamin = 100000002;
  int dispCat = CatStandard;
  int viewGroup = VgOther;
  int classIdx = -1;  // index into Buffer::classes (per-class filter, P3.6)
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
  // S-52 text offsets from the object anchor (PresLib TX/TE): xoffs in units of
  // average char width, yoffs in units of char height (+x right, +y down). With
  // hjust/vjust these place names/light text clear of the symbol (and each
  // other) instead of stacked on it.
  int xoffs = 0;
  int yoffs = 0;
  // S-52 SCAMIN: hidden when the chart is more zoomed out than 1:scamin.
  int scamin = 100000002;
  // Soundings get spatial density declutter keeping the SHALLOWEST per
  // cell (safety). isSounding marks them; depth is the value in metres.
  bool isSounding = false;
  float depth = 0.0f;
  // S-52 SNDFRM quality flags (consulted only when isSounding). `soundingSwept`
  // is TECSOU "swept by wire drag" -> the figures get a swept-depth bracket;
  // `soundingLowAccuracy` is a doubtful/unreliable QUASOU, existence-doubtful
  // STATUS, or approximate QUAPOS -> the figures render italic.
  bool soundingSwept = false;
  bool soundingLowAccuracy = false;
  int dispCat = CatStandard;
  // Per-feature-class viewing group (Lights/BuoysBeacons/Other), so the
  // consumer can apply the nav-aid detail-scale cap to a light/buoy name too.
  int viewGroup = VgOther;
  int classIdx = -1;  // index into Buffer::classes (per-class filter, P3.6)
};

/** One S-57 attribute (acronym + value as text), for object query. */
struct QueryAttr {
  QString name;
  QString value;
};

enum class QueryGeom { Area, Line, Point };

/** A queryable S-57 feature snapshot (object query). Built at decode time
 *  straight from the OGR feature -- class, attributes, and enough geometry
 *  (bbox + shape in lon/lat) to hit-test a click. Independent of the
 *  (discarded) S57Obj objects. */
struct QueryObject {
  QString className;          // S-57 class acronym, e.g. "DEPARE"
  QList<QueryAttr> attrs;
  QueryGeom geom = QueryGeom::Area;
  double minLon = 0, minLat = 0, maxLon = 0, maxLat = 0;  // bbox
  QList<QPointF> shape;       // (lon, lat): area exterior ring / line / point
};

/** A decoded chart's geometry, ready for the consumer to upload. */
class Buffer {
public:
  QList<Prim> prims;
  QList<PatternFill> patternFills;
  QList<Symbol> symbols;
  QList<VectorSymbol> vectorSymbols;
  QList<ComplexLine> complexLines;
  QList<Label> labels;
  // Land-area (LNDARE) exterior rings, (lon, lat), closed -- for the
  // coastline land-shade pass. Not symbology; a cartographic emphasis.
  QList<QList<QPointF>> landContours;
  // Queryable feature snapshots (object query); all feature classes.
  QList<QueryObject> queryObjects;
  // Per-buffer S-57 class table: every primitive's classIdx indexes into
  // `classes` (FeatureName acronyms encountered at emit), so the consumer
  // can hide whole object classes -- the wx MARINERS_STANDARD / "User
  // Standard Objects" filter (P3.6). classIndex is the emit-side
  // lookup accelerator (same data, keyed by acronym).
  QStringList classes;
  QHash<QString, int> classIndex;
  int classOf(const char *feature_name) {
    if (!feature_name || !feature_name[0]) return -1;
    const QString acr = QString::fromLatin1(
        feature_name, static_cast<int>(qstrnlen(feature_name, 7)));
    const auto it = classIndex.constFind(acr);
    if (it != classIndex.constEnd()) return it.value();
    classIndex.insert(acr, classes.size());
    classes.append(acr);
    return classes.size() - 1;
  }
  void clear() {
    prims.clear();
    patternFills.clear();
    symbols.clear();
    vectorSymbols.clear();
    complexLines.clear();
    labels.clear();
    landContours.clear();
    queryObjects.clear();
    classes.clear();
    classIndex.clear();
  }
  bool empty() const {
    return prims.isEmpty() && patternFills.isEmpty() && symbols.isEmpty() &&
           vectorSymbols.isEmpty() && labels.isEmpty();
  }
};

}  // namespace s52sg

#endif  // _S52_SG_H_
