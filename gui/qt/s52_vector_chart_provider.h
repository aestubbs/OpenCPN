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
#include <QString>

#include "chart_provider.h"
#include "s52_sg.h"

QT_BEGIN_NAMESPACE
class QSGTransformNode;
class QSGGeometryNode;
class QSGOpacityNode;
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

  /** S-52 display category to show: 0 = Base, 1 = Standard, 2 = All
   *  (Other). Items with a higher category rank are filtered out. Changing
   *  it forces a rebuild (emits changed()). */
  void setDisplayCategory(int cat);
  int displayCategory() const { return m_displayCategory; }

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
    float depth = 0.0f;       // sounding depth (metres) for shallowest-wins
    float screenW = 0.0f;     // on-screen size (logical px) -- for label
    float screenH = 0.0f;     // bounding-box declutter
  };

  // A line feature rendered as N parallel 1px polylines offset from the
  // centreline. Qt RHI backends only support line width 1, but thin
  // polylines rasterise cleanly (proper joins, MSAA edges), so stacking a
  // few 1px strips offset perpendicular by ~1px each gives a smooth thick
  // line without triangle-tessellation joint artifacts. The strip count is
  // fixed by the physical pen width; only the per-zoom offset distance
  // (offset_px / scale) changes, so the strips are created once and their
  // vertices rebuilt when the scale changes.
  struct LineGeom {
    QList<QSGGeometryNode*> strips;  // one DrawLineStrip per parallel offset
    QList<QPointF> worldPts;         // (x=lon, y=-lat) centreline
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

  void updateBillboards(const Viewport& viewport);
  void rebuildLines(double scale);
  void rebuildPatternUVs(double scale);

  // Pixels per millimetre of the display, for the 1:N chart-scale
  // denominator used by SCAMIN. Set from the window's QScreen each build;
  // falls back to a 96-dpi nominal until then.
  double m_screen_ppmm = 3.8;
  QList<LineGeom> m_lines;
  QList<PatternGeom> m_patterns;
  // Scale at the last full billboard/line/pattern update; updates are
  // skipped while it's unchanged (so panning is free). Reset to -1 on build.
  double m_last_line_scale = -1.0;
  // Scale at which we last dirtied the layer; the provider only emits
  // changed() (forcing a re-sync) when the scale actually changes, so a pan
  // never re-syncs this chart.
  double m_emit_scale = -1.0;
  int m_displayCategory = 1;  // 0 Base, 1 Standard, 2 All

  QString m_id;
  s52sg::Buffer m_buffer;
  double m_north, m_south, m_west, m_east;
  const Viewport* m_viewport;
  QList<Billboard> m_billboards;
  bool m_built = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_S52_VECTOR_CHART_PROVIDER_H_
