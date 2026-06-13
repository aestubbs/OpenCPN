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
 * ChartProvider -- boundary interface between the chart-rendering pipeline
 * and any chart source (raster, vector / S-52, MBTiles, web tiles, ...).
 *
 * The renderer (ChartCanvas / LayerCompositor / ChartLayer) knows nothing
 * about specific chart formats. Each concrete provider implements
 * `renderChart()` to produce a scene-graph subtree appropriate for its
 * data and the current viewport.
 *
 * Implementations:
 *   - RasterChartProvider  (raster_chart_provider.h) -- one QImage at
 *     fixed lat/lon corners; renders as a single QSGImageNode.
 *   - S57ChartProvider     (future) -- wraps s52plib output, renders as
 *     a QSGGeometryNode tree.
 *   - MbtilesChartProvider (future) -- web-tile layer.
 *
 * A `ChartLayer` (chart_layer.h) wraps one ChartProvider and adapts it to
 * the `Layer` interface the LayerCompositor consumes.
 */

#ifndef OCPN_QT_CHART_PROVIDER_H_
#define OCPN_QT_CHART_PROVIDER_H_

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QQuickWindow;
class QSGNode;
QT_END_NAMESPACE

namespace ocpn::qtui {

class Viewport;

class ChartProvider : public QObject {
  Q_OBJECT

public:
  explicit ChartProvider(QObject* parent = nullptr) : QObject(parent) {}
  ~ChartProvider() override = default;

  /** Stable identity (used by ChartLayer::id() → OcpnConfig persistence). */
  virtual QString id() const = 0;
  /** Human-readable display name. */
  virtual QString name() const = 0;

  // Geographic bounds. Used by chart-DB selection, quilting (future), and
  // simple culling.
  virtual double northLat() const = 0;
  virtual double southLat() const = 0;
  virtual double westLon() const = 0;
  virtual double eastLon() const = 0;

  /**
   * Produce or update the scene-graph subtree for this chart.
   *
   * Called by `ChartLayer::updateSubtree` whenever the layer is dirty
   * (provider data changed, or the wrapping layer's visibility/opacity
   * changed; ChartLayer itself does not auto-dirty on viewport changes --
   * providers that need viewport-dependent re-rendering subscribe to the
   * viewport themselves and emit `changed()`).
   *
   * @param old_subtree previous return value (nullptr first time); the
   *                    provider may mutate in place and return same, or
   *                    return a fresh node (caller adopts; old is freed
   *                    by the compositor).
   * @param viewport    current Viewport -- providers may use it for
   *                    level-of-detail decisions (vector providers do;
   *                    raster providers ignore).
   * @param window      the QQuickWindow -- needed for
   *                    `QQuickWindow::createTextureFromImage()` etc.
   */
  virtual QSGNode* renderChart(QSGNode* old_subtree,
                               const Viewport& viewport,
                               QQuickWindow* window) = 0;

signals:
  /** Emit when the chart's data has changed and the subtree should be
   *  re-built. The wrapping ChartLayer connects this to its own
   *  `Layer::dirty` signal. */
  void changed();
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_PROVIDER_H_
