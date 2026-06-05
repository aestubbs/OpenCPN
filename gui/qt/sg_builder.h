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
 * SgBuilder -- a scene-graph drawing context (P2.6 ocpnDC port, core paths).
 *
 * The legacy renderer drew through `ocpnDC` (gui/src/ocpndc.cpp), which
 * wrapped either a wxDC (CPU) or raw OpenGL (GPU). In the Qt renderer the
 * core no longer rasterises into a device: each Layer emits a retained
 * scene-graph subtree that the GPU composites. SgBuilder is the Qt-native
 * equivalent of ocpnDC's *primitive* surface -- it builds QSGNodes for the
 * core's line / polyline / polygon / rectangle / circle / text / bitmap
 * primitives and appends them to a parent node.
 *
 * Scope (matches the P2.6 task): the **core** drawing surface only. The
 * wx-plugin rendering ABI (RenderOverlay(wxDC*) / RenderGLOverlay) is
 * deferred to the Phase 4 Qt plugin host, so this carries no wx types --
 * it is QColor / QPointF / QImage / QString throughout.
 *
 * **Coordinate-space agnostic.** Coordinates are whatever the caller's Layer
 * works in: pass world coordinates (x = lon, y = -lat) for a WorldAnchored
 * Layer, or screen pixels for a DisplayAnchored one. The owning Layer
 * attaches the parent node under the matching transform root; SgBuilder neither
 * knows nor cares which. (So world-anchored thick lines / circles are sized
 * in world units, display-anchored ones in pixels -- the caller chooses.)
 *
 * Built on the P2.4 material factories (sg_helpers.h) and the P2.5 texture
 * cache (sg_texture_cache.h): textured primitives (drawImage / drawText) are
 * uploaded once and owned by a TextureCacheNode child of the parent, freed
 * with the subtree on the render thread.
 *
 * Batching: one QSGGeometryNode per draw call (clear and correct). Merging
 * same-material draws into shared buffers is a P2.12 optimisation; the API
 * here does not change when that lands.
 */

#ifndef OCPN_QT_SG_BUILDER_H_
#define OCPN_QT_SG_BUILDER_H_

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

QT_BEGIN_NAMESPACE
class QImage;
class QSGNode;
class QQuickWindow;
QT_END_NAMESPACE

namespace ocpn::qtui {

class TextureCacheNode;

class SgBuilder {
public:
  /**
   * @param parent node to append emitted primitives to (owned by the caller
   *               / compositor). Must outlive this SgBuilder's draw calls.
   * @param window the QQuickWindow -- required for drawImage / drawText
   *               (texture upload); may be nullptr for geometry-only use.
   */
  explicit SgBuilder(QSGNode* parent, QQuickWindow* window = nullptr);

  // --- Pen / brush state (mirrors ocpnDC::SetPen / SetBrush) -------------
  // An invalid colour disables that part: noPen() draws fills only, noBrush()
  // draws outlines only. Pen lines render through the shared AA-line shader
  // (aa_line.h), so **pen width is in logical pixels** (screen-fixed at any
  // zoom) -- unlike brush fills, whose geometry is in the caller's
  // coordinate units. setDash() gives the pen a dash pattern (logical px).
  void setPen(const QColor& color, float width = 1.0f);
  void noPen();
  void setDash(float on_px, float off_px);
  void noDash();
  // Pencil/graphite stroke style for subsequent pen lines (routes): grain +
  // toothed edge + modest pressure, ruler-straight. Off by default; persists
  // like the dash state until changed.
  void setPencil(bool on);
  void setBrush(const QColor& color);
  void noBrush();

  // --- Primitives --------------------------------------------------------
  /** A single straight segment, in the current pen. */
  void drawLine(const QPointF& a, const QPointF& b);

  /** An open polyline through `pts`, in the current pen. */
  void drawPolyline(const QList<QPointF>& pts);

  /** A polygon: filled with the current brush (tessellated, so concave and
   *  self-intersecting outlines fill correctly) and outlined with the
   *  current pen. `pts` need not be explicitly closed. */
  void drawPolygon(const QList<QPointF>& pts);

  /** An axis-aligned rectangle: brush fill + pen outline. */
  void drawRect(const QRectF& rect);

  /** A circle centred at `center`: brush fill + pen outline. `segments` 0
   *  picks a radius-dependent count. */
  void drawCircle(const QPointF& center, float radius, int segments = 0);

  /** Blit `image` into `dest` (caller coordinates). Needs a window. */
  void drawImage(const QRectF& dest, const QImage& image);

  /**
   * Draw `text` with its top-left at `top_left`, at screen-pixel size
   * regardless of zoom when used WorldAnchored (the quad is sized in the
   * caller's units from the rendered pixel extent -- callers that want
   * screen-fixed world text wrap the parent in a counter-scaled transform,
   * as S52VectorChartProvider does for labels). Renders via QPainter to an
   * RGBA image (system font) and uploads through the texture cache. Needs a
   * window. `color` invalid -> current pen colour; `point_size` 0 -> default
   * application font size.
   */
  void drawText(const QString& text, const QPointF& top_left,
                const QColor& color = QColor(), float point_size = 0.0f);

  /**
   * Render `text` to a premultiplied-RGBA image with a transparent
   * background, using the system font. Rendered at 2x (devicePixelRatio 2)
   * for hi-DPI crispness, so the image's logical size is its on-screen
   * extent. Shared by drawText and by retained-node layers that build their
   * own label QSGImageNodes (e.g. AIS / own-ship). `point_size` 0 -> default
   * application font size.
   */
  static QImage renderText(const QString& text, const QColor& color,
                           float point_size = 0.0f);

private:
  // Lazily create (once) a TextureCacheNode child of m_parent that owns the
  // textures for drawImage / drawText.
  TextureCacheNode* textureRoot();
  // Append a polyline in the current pen (width/dash) via the shared AA-line
  // shader. `closed` joins last->first.
  void appendLine(const QList<QPointF>& pts, bool closed);

  QSGNode* m_parent;
  QQuickWindow* m_window;
  TextureCacheNode* m_tex_root = nullptr;

  QColor m_pen_color = QColor(0, 0, 0);
  float m_pen_width = 1.0f;  // logical px (AA-line shader is screen-fixed)
  bool m_has_pen = true;
  float m_dash_on = 0.0f;   // logical px; 0 = solid
  float m_dash_off = 0.0f;
  bool m_pencil = false;    // pencil/graphite stroke style for pen lines
  QColor m_brush_color;  // invalid by default -> no fill
  bool m_has_brush = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SG_BUILDER_H_
