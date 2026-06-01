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
 * S52VectorChartProvider -- a ChartProvider backed by s52plib's
 * scene-graph emit path (P2.8c).
 *
 * Holds a decoded s52sg::Buffer (world-coordinate geometry produced by
 * s52plib::RenderObjectToSG) and builds a QSGGeometryNode subtree from it:
 * one node per primitive, vertices placed in world space (x = lon,
 * y = -lat), coloured by a flat-colour material. The geometry is static
 * in world coordinates -- the ChartCanvas viewport transform on the
 * World-anchored root does all the projection, so pan/zoom never
 * re-decodes and the chart stays vector-sharp at any scale.
 *
 * Contrast with RasterChartProvider, which uploads a single QImage as a
 * textured quad. Both share the ChartProvider boundary; the renderer
 * doesn't care which kind it composites.
 */

#ifndef OCPN_QT_S52_VECTOR_CHART_PROVIDER_H_
#define OCPN_QT_S52_VECTOR_CHART_PROVIDER_H_

#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>

#include "chart_provider.h"
#include "s52_sg.h"

QT_BEGIN_NAMESPACE
class QSGTransformNode;
class QSGGeometryNode;
class QSGOpacityNode;
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class S52VectorChartProvider : public ChartProvider {
  Q_OBJECT

public:
  // `viewport` is used only to billboard point symbols / text labels
  // (world-positioned but screen-fixed size): the provider watches it and
  // re-applies the counter-scale on pan/zoom. Static fills/lines ignore it.
  S52VectorChartProvider(QString id, s52sg::Buffer buffer, double north,
                         double south, double west, double east,
                         const Viewport* viewport, QObject* parent = nullptr);

  QString id() const override { return m_id; }
  QString name() const override { return QStringLiteral("S-52 chart"); }

  double northLat() const override { return m_north; }
  double southLat() const override { return m_south; }
  double westLon() const override { return m_west; }
  double eastLon() const override { return m_east; }

  QSGNode* renderChart(QSGNode* old_subtree, const Viewport& viewport,
                       QQuickWindow* window) override;

  /** Object query: the feature snapshots whose geometry is hit by a click at
   *  (lat, lon), within `margin_deg` for points/lines (areas use
   *  point-in-polygon). For the S-57 object-query popup (P3.9). */
  QList<s52sg::QueryObject> objectsAt(double lat, double lon,
                                      double margin_deg) const;

  /** S-52 display category to show: 0 = Base, 1 = Standard, 2 = All
   *  (Other). Items with a higher category rank are filtered out. Changing
   *  it forces a rebuild (emits changed()). */
  void setDisplayCategory(int cat);
  int displayCategory() const { return m_displayCategory; }

  // S-52 viewing-group toggles (mirror s52plib's GetShowSoundings /
  // GetShowS57Text). Applied as a post-decode filter on the cached buffer's
  // labels -- no re-decode -- so toggling is cheap. Changing forces a
  // rebuild (emits changed()).
  void setShowSoundings(bool on);
  bool showSoundings() const { return m_showSoundings; }
  void setShowText(bool on);
  bool showText() const { return m_showText; }
  void setShowLights(bool on);
  bool showLights() const { return m_showLights; }
  void setShowBuoys(bool on);
  bool showBuoys() const { return m_showBuoys; }
  // De-cluttered text (P2.23a): when true, drop labels whose screen bounding
  // box overlaps one already placed (wx m_bDeClutterText). Default false ==
  // wx default: all labels shown, overlap allowed. Consumer-side per-frame
  // cull, so toggling is cheap (no re-decode).
  void setDeclutter(bool on);
  bool declutter() const { return m_declutter; }
  // Default minimum-display scale (1:N) for objects that carry no SCAMIN, so
  // un-SCAMIN'd detail (buoys, lights, sector arcs) thins out when zoomed out.
  void setDetailScale(double n);
  // Sounding display unit (DisplayConfig order: 0 = metres, 1 = feet,
  // 2 = fathoms). Soundings carry their depth in metres and are formatted +
  // rasterised at build time, so changing the unit just re-rasters the labels
  // (no re-decode). Changing forces a rebuild (emits changed()).
  void setDepthUnit(int unit);
  int depthUnit() const { return m_depth_unit; }
  // Safety depth in metres: a sounding at or shallower than this is emphasised
  // (bold/black), mirroring the S-52 SOUNDS vs SOUNDG split. Changing forces a
  // rebuild (emits changed()).
  void setSafetyDepth(double metres);
  double safetyDepth() const { return m_safety_depth_m; }
  // Multiplier on the base sounding figure size (1.0 = nominal), driven by the
  // ENC sounding-size slider. Render-time, so a change just re-rasters.
  void setSoundingScale(double mult);
  double soundingScale() const { return m_sounding_scale; }
  // The cell's compilation scale (1:N). When the display is zoomed in finer
  // than this, the cell is OVERSCALED and an S-52 over-scale hatch is drawn
  // over its extent. 0 (unknown) disables the hatch. Set after construction
  // (the catalog scale is known then). Triggers a re-layout (emits changed()).
  void setNativeScale(int n);

private:
  // One billboarded point item (symbol or text): a transform node placed at
  // the world anchor whose scale counters the viewport scale so the content
  // stays screen-pixel-sized. Wrapped in an opacity node so a culled item is
  // HIDDEN by setting opacity 0 -- the Qt renderer skips opacity-0 subtrees
  // entirely (no draw call), unlike a zero-scale transform which still draws
  // a degenerate quad. This matters at low zoom where most items are culled.
  enum class BbKind { Symbol, Label, Sounding, Vector };
  struct Billboard {
    QSGOpacityNode* opacity = nullptr;  // hide = opacity 0 (renderer culls)
    QSGTransformNode* xform = nullptr;
    QPointF worldPos;  // (x=lon, y=-lat)
    int scamin = 100000002;  // hidden when chart scale 1:N > scamin
    BbKind kind = BbKind::Symbol;
    int viewGroup = 0;        // s52sg::ViewGroup -- nav aids get the detail cap
    float depth = 0.0f;       // sounding depth (metres) for shallowest-wins
    float screenW = 0.0f;     // on-screen size (logical px) -- for label
    float screenH = 0.0f;     // bounding-box declutter
    // Screen offset (logical px, +x right / +y down) of the label's rect CENTRE
    // from the world anchor, BEFORE chart rotation. Zero for centred items
    // (symbols, soundings); for S-52 offset text it is the hjust/vjust +
    // xoffs/yoffs shift. Declutter tests the box where the text draws; the
    // per-frame matrix rotates this vector by the viewport rotation so the
    // offset follows the chart under course-/head-up.
    float screenCx = 0.0f;
    float screenCy = 0.0f;
    // True for system-font TEXT (labels/soundings): the glyph is held UPRIGHT
    // under chart rotation (the billboard counter-rotates), and its offset
    // rotates with the chart. False for symbols/vector marks, which rotate
    // with the chart so an ORIENT'd light/beacon keeps its bearing.
    bool upright = false;
    bool kept = true;         // survived SCAMIN + density declutter (scale-only;
                              // the per-frame view-cull is applied on top)
  };

  // An AP pattern fill: tessellated triangles (world coords) drawn with a
  // tiling texture. Positions are static; only the per-vertex UVs change
  // with zoom (screen-fixed tile size), so they rebuild on scale change.
  struct PatternGeom {
    QSGGeometryNode* node = nullptr;
    QList<QPointF> tris;  // (x=lon, y=-lat) triangle list
    double tileW = 16.0;  // pattern tile size in logical px
    double tileH = 16.0;
  };

  // A complex (LC) line: the HPGL glyph is walked along the path, rebuilt on
  // zoom so the glyph stays screen-fixed (like the pattern UVs). The node's
  // geometry is regenerated each scale change; opacity culls it by SCAMIN.
  struct ComplexLineGeom {
    QSGGeometryNode* node = nullptr;
    QSGOpacityNode* opacity = nullptr;
    s52sg::ComplexLine src;
  };

  // A static fill/line/pattern node that carries a real S-52 SCAMIN (P2.14).
  // The geometry is built once; this opacity node hides it (opacity 0, so the
  // renderer skips the subtree) once the chart scale is more zoomed out than
  // its SCAMIN. Only Prims/PatternFills with a real SCAMIN are wrapped --
  // un-SCAMIN'd fills go straight into the tree and always draw (so an area
  // fill never vanishes from the composite underlay).
  struct ScaminNode {
    QSGOpacityNode* opacity = nullptr;
    int scamin = 100000002;
  };

  // A spatial cull tile: a group of static fill/line nodes whose world bounding
  // boxes fall in one grid cell of the chart. applyPrimCull() sets its opacity 0
  // when it is entirely outside the view, so a large cell's off-screen geometry
  // is neither batched nor drawn -- the fill/line analogue of the billboard
  // frustum cull. `bbox` is the union (world AABB) of the tile's prims.
  struct PrimTile {
    QSGOpacityNode* opacity = nullptr;
    QRectF bbox;
  };

  // Full re-layout (SCALE-dependent, pan-invariant): pattern UVs, complex
  // lines, static-SCAMIN nodes, and the per-billboard `kept` flag (SCAMIN +
  // density declutter). Sets the counter-scale matrix on every kept billboard.
  // Expensive -- runs on build and once a zoom settles (not per pan frame).
  void recomputeDeclutter(const Viewport& viewport);
  // Cheap per-frame pass (PAN + zoom): view-frustum-cull -- show a billboard
  // only if it is `kept` AND inside `worldView` (opacity 0 otherwise, so the
  // off-screen ones become blocked subtrees: not batched, not drawn). Re-applies
  // the counter-scale only when the scale changed (zoom), so a pure pan just
  // toggles opacity. This bounds the draw-call count to on-screen content at any
  // zoom, and keeps text/symbols screen-fixed during a zoom gesture.
  void applyBillboardVisibility(double scale, double rotationRad,
                                const QRectF& worldView);
  // Per-frame pass: hide (opacity 0) any prim tile whose world bbox is fully
  // outside the view, so a cell only batches/draws the fills & lines on screen.
  void applyPrimCull(const QRectF& worldView);
  void rebuildPatternUVs(double scale);
  // Walk each LC glyph along its path at the given scale (px/deg) + centre
  // latitude, regenerating the geometry (screen-fixed glyph) and SCAMIN cull.
  void rebuildComplexLines(double scale, double chart_scale_n);
  // Hide/show the static fills & lines that carry a real SCAMIN, by the
  // current 1:N chart scale. Scale-only, so it runs in the recomputeDeclutter
  // pass (not per pan frame).
  void updateScaminNodes(double chart_scale_n);

  // Pixels per millimetre of the display, for the 1:N chart-scale
  // denominator used by SCAMIN. Set from the window's QScreen each build;
  // falls back to a 96-dpi nominal until then.
  double m_screen_ppmm = 3.8;
  QList<PatternGeom> m_patterns;
  QList<ComplexLineGeom> m_complex_lines;
  QList<ScaminNode> m_scamin_nodes;  // static fills/lines with a real SCAMIN
  QList<PrimTile> m_prim_tiles;      // spatial cull tiles for fills & lines
  // Scale at which the billboard counter-scale matrices were last set. The
  // per-frame view-cull skips re-setting matrices while this is unchanged (a
  // pan), and refreshes them when it differs (a zoom). Reset to -1 on build.
  double m_bb_scale = -1.0;
  // Chart rotation (radians) at which the billboard matrices were last set. An
  // upright text billboard's matrix counter-rotates by this AND rotates its
  // offset by it, so when the rotation changes (course-/head-up turn) the
  // matrices are refreshed -- like a zoom. NaN-safe sentinel forces first set.
  double m_bb_rotation = 0.0;
  // Last scale the viewport reported, to tell a zoom (scale change -> arm the
  // settle relayout) from a pan (same scale -> view-cull only).
  double m_emit_scale = -1.0;
  // True when a full scale-dependent re-layout (recomputeDeclutter) is owed:
  // set when a zoom settles or a declutter/detail setting changes. The next
  // renderChart runs it once, then clears the flag; pan frames never set it.
  bool m_relayout_pending = false;
  // Debounces the zoom relayout: a scale change (re)starts this timer; the
  // expensive declutter fires once, ~110ms after the last scale change. During
  // the gesture each frame only view-culls + counter-scales the cached subtree,
  // so zooming stays smooth and the CPU work happens once at the end.
  QTimer* m_zoom_timer = nullptr;
  int m_displayCategory = 1;  // 0 Base, 1 Standard, 2 All
  bool m_showSoundings = true;
  bool m_showText = true;
  bool m_showLights = true;
  bool m_showBuoys = true;
  bool m_declutter = false;  // P2.23a: label overlap-avoid (wx default off)
  int m_depth_unit = 0;          // sounding unit: 0 metres, 1 feet, 2 fathoms
  double m_safety_depth_m = 5.0;  // <= this (metres) -> emphasised sounding
  double m_sounding_scale = 1.0;  // ENC sounding-size slider multiplier
  int m_native_scale = 0;         // cell compilation 1:N (0 = unknown)
  // S-52 over-scale hatch: vertical lines over the cell's extent, shown only
  // when the display is zoomed finer than the cell's native scale. Built once
  // (a fixed set of world-X verticals across the bbox); shown/hidden + line
  // spacing rebuilt by the scale-dependent re-layout. Pixel-spaced like the
  // SCAMIN nodes. nullptr until the first build with a known native scale.
  QSGOpacityNode* m_overscale_hatch = nullptr;
  void rebuildOverscaleHatch(double scale, double chart_scale_n);
  double m_unset_scamin_n = 100000.0;  // default min display scale (no SCAMIN)
  // True if the symbol/vector-symbol's viewing group is currently enabled.
  bool viewGroupEnabled(int vg) const;

  QString m_id;
  s52sg::Buffer m_buffer;
  double m_north, m_south, m_west, m_east;
  const Viewport* m_viewport;
  QList<Billboard> m_billboards;
  bool m_built = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_S52_VECTOR_CHART_PROVIDER_H_
