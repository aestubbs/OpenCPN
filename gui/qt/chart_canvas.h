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
 * ChartCanvas -- the QQuickItem that hosts the new Qt-Quick chart rendering.
 *
 * Three-tier scene-graph (per docs/QT_MIGRATION.md §5 + Phase 2 plan):
 *   - World-anchored subtree   : QSGTransformNode driven by the Viewport's
 *                                world→screen matrix; chart pan/zoom mutates
 *                                this one matrix and the whole subtree
 *                                follows. Hosts chart tiles, S-52 vector
 *                                objects, AIS targets, routes, tracks.
 *   - Display-anchored subtree : identity QSGTransformNode for fixed-screen
 *                                overlays (radar/PPI, range rings, compass
 *                                rose, mini-map).
 *   - QML HUD                  : declarative QML items LAYERED ABOVE the
 *                                ChartCanvas in QML.
 *
 * Owns a `LayerCompositor` that maintains the two anchored subtrees from
 * registered Layer instances, and a `Viewport` that's mutated by the
 * canvas's mouse handlers (drag = pan, wheel = zoom about cursor).
 */

#ifndef OCPN_QT_CHART_CANVAS_H_
#define OCPN_QT_CHART_CANVAS_H_

#include <memory>

#include <QHash>
#include <QList>
#include <QPointF>
#include <QQuickItem>
#include <QSet>

#include "chart_extent.h"  // CellExtent -- catalog entry (value type)
#include "s52_engine.h"    // S52Engine -- complete type needed for Q_PROPERTY

class OcpnConfig;

QT_BEGIN_NAMESPACE
class QSGNode;
class QSGTransformNode;
class QMouseEvent;
class QWheelEvent;
class QThread;
class QTimer;
QT_END_NAMESPACE

namespace ocpn::qtui {

class LayerCompositor;
class Viewport;
class S52VectorChartProvider;
class ChartBoundaryProvider;
class ChartWorker;
class DemoNavDataProvider;
class ModelNavDataProvider;
class SwitchableNavDataProvider;
class NmeaLogReplay;
class AisLayer;
class OwnShipLayer;

class ChartCanvas : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT

  // The S-52 vector-chart engine. Bound from QML (`s52Engine: s52`). When
  // set and initialised, the canvas decodes a demo S-57 chart through it
  // and adds the vector layer (P2.8c). Optional -- without it the canvas
  // shows only the raster test chart.
  Q_PROPERTY(ocpn::qtui::S52Engine* s52Engine READ s52Engine WRITE
                 setS52Engine NOTIFY s52EngineChanged)

  // S-52 display category: 0 = Base, 1 = Standard, 2 = All. Bound from a
  // QML control; forwards to the vector chart provider.
  Q_PROPERTY(int displayCategory READ displayCategory WRITE setDisplayCategory
                 NOTIFY displayCategoryChanged)

  // S-52 viewing-group toggles (soundings, text). Bound from QML controls;
  // forwarded to every loaded vector provider as a post-decode filter.
  Q_PROPERTY(bool showSoundings READ showSoundings WRITE setShowSoundings
                 NOTIFY showSoundingsChanged)
  Q_PROPERTY(bool showText READ showText WRITE setShowText NOTIFY
                 showTextChanged)
  Q_PROPERTY(bool showLights READ showLights WRITE setShowLights NOTIFY
                 showLightsChanged)
  Q_PROPERTY(bool showBuoys READ showBuoys WRITE setShowBuoys NOTIFY
                 showBuoysChanged)

  // Demo mode: feed the nav overlays (AIS / own-ship) from the synthetic
  // DemoNavDataProvider (animated). When off, the demo animation freezes;
  // the same NavDataProvider seam will host the live model adapter later.
  Q_PROPERTY(bool demoMode READ demoMode WRITE setDemoMode NOTIFY
                 demoModeChanged)

public:
  explicit ChartCanvas(QQuickItem* parent = nullptr);
  ~ChartCanvas() override;

  S52Engine* s52Engine() const { return m_s52_engine; }
  void setS52Engine(S52Engine* engine);

  int displayCategory() const { return m_display_category; }
  void setDisplayCategory(int cat);

  bool showSoundings() const { return m_show_soundings; }
  void setShowSoundings(bool on);
  bool showText() const { return m_show_text; }
  void setShowText(bool on);
  bool showLights() const { return m_show_lights; }
  void setShowLights(bool on);
  bool showBuoys() const { return m_show_buoys; }
  void setShowBuoys(bool on);

  bool demoMode() const { return m_demo_mode; }
  void setDemoMode(bool on);

  // Toolbar actions (bound from the QML chrome). Zoom about the canvas
  // centre; fitWorld zooms out to show the whole scanned chart set.
  Q_INVOKABLE void zoomIn();
  Q_INVOKABLE void zoomOut();
  Q_INVOKABLE void fitWorld();
  // Re-seed the demo nav fleet around the current view centre (P2.11).
  Q_INVOKABLE void dropDemoHere();

Q_SIGNALS:
  void s52EngineChanged();
  void displayCategoryChanged();
  void showSoundingsChanged();
  void showTextChanged();
  void showLightsChanged();
  void showBuoysChanged();
  void demoModeChanged();

protected:
  QSGNode* updatePaintNode(QSGNode* old_node,
                           UpdatePaintNodeData* update_data) override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  // Spin up the worker thread + ChartWorker and kick off the catalog scan
  // for the configured chart set. Called once from setS52Engine().
  void startAsyncLoad(const QStringList& cell_paths, const QString& s57data);
  // Worker results (delivered to the main thread via queued connections).
  void onExtentsScanned(const QList<CellExtent>& cells);
  void onCellLoaded(const QString& id, const s52sg::Buffer& buffer,
                    double north, double south, double east, double west);
  // Reconcile the set of loaded cells with the current view: request
  // catalogued cells that have come into view (and grown large enough on
  // screen to be worth decoding), and evict loaded cells that have left the
  // view or shrunk too small. Debounced off Viewport::changed.
  void updateVisibleCells();
  // The ~1:N display-scale denominator for a viewport scale (px/degree), at
  // a nominal display density. Compared against cells' native CSCL to pick
  // the quilt tier.
  static double displayScaleN(double scale);
  // Layer z-order for a cell of the given native scale: finer (smaller 1:N)
  // draws over coarser, so overlaps hide the coarse cell.
  static int zOrderForScale(int native_scale);

  // Top transform nodes — non-owning pointers into the scene-graph tree
  // (which is owned by Qt's scene graph); the LayerCompositor attaches
  // Layer subtrees under each.
  QSGTransformNode* m_world_anchored_root = nullptr;
  QSGTransformNode* m_display_anchored_root = nullptr;

  // Declared before the compositor so they outlive the overlay Layers that
  // reference them (members destroy in reverse declaration order). The
  // switchable provider is declared last of these so it's destroyed first
  // (it references the demo + model providers).
  std::unique_ptr<DemoNavDataProvider> m_demo_provider;
  std::unique_ptr<ModelNavDataProvider> m_model_provider;
  std::unique_ptr<NmeaLogReplay> m_nav_replay;
  std::unique_ptr<SwitchableNavDataProvider> m_nav_provider;

  std::unique_ptr<LayerCompositor> m_compositor;
  std::unique_ptr<Viewport> m_viewport;
  // Backs per-Layer visible/zOrder/opacity persistence (P2.10). Handed to
  // the compositor; saved on destruction.
  std::unique_ptr<OcpnConfig> m_layer_config;

  // Nav overlays (owned by their ChartLayer-less Layer entries in the
  // compositor; pointers kept only to forward state). World-anchored, on top.
  AisLayer* m_ais_layer = nullptr;
  OwnShipLayer* m_own_ship_layer = nullptr;
  bool m_demo_mode = true;
  bool m_live_centered = false;  // recentre on own ship once per live session

  // Non-owning; set from QML. nullptr until bound.
  S52Engine* m_s52_engine = nullptr;
  int m_display_category = 1;  // 0 Base, 1 Standard, 2 All
  bool m_show_soundings = true;
  bool m_show_text = true;
  bool m_show_lights = true;
  bool m_show_buoys = true;
  // Push the current display category + viewing-group toggles onto a newly
  // created provider (called at both provider-creation sites).
  void applyDisplaySettings(S52VectorChartProvider* provider) const;

  // A decoded, on-screen cell. The provider is owned by its ChartLayer in
  // the compositor (removeLayer deletes both); we keep the pointer only to
  // forward display-category changes, and the extent to re-test visibility
  // for eviction.
  struct LoadedCell {
    CellExtent extent;
    QString layerId;
    S52VectorChartProvider* provider = nullptr;
  };
  // Currently-resident cells, keyed by cell name ("demo" for the synthetic
  // chart, which is never evicted as it has no catalog entry).
  QHash<QString, LoadedCell> m_loaded;

  // --- Async chart loading (P2.x) ---
  // The worker + its thread (owned: thread parented to this; worker
  // deleteLater on thread finish). null in the demo-chart path.
  QThread* m_worker_thread = nullptr;
  ChartWorker* m_worker = nullptr;
  // Boundary overlay (owned by its ChartLayer in the compositor).
  ChartBoundaryProvider* m_boundary_provider = nullptr;
  // The decode-free catalog, keyed by cell name.
  QHash<QString, CellExtent> m_catalog;
  // Cells already asked of the worker (loaded or in flight) -- never twice.
  QSet<QString> m_requested;
  // Coalesces a burst of pan/zoom into one visible-cell evaluation.
  QTimer* m_load_debounce = nullptr;
  QString m_s57data_dir;
  // The catalog scan publishes progressively; fit the viewport to the set
  // only on the first batch (refitting each batch would jump the view).
  bool m_world_fitted = false;
  // The set of cell names currently selected for display. Computed per
  // location: each spot in view is covered by the candidate cell whose
  // native scale best matches the zoom (so a location never drops to the
  // world backdrop while any ENC cell covers it). Cells not in this set are
  // evicted. Recomputed on every updateVisibleCells.
  QSet<QString> m_needed;

  // Drag state.
  bool m_dragging = false;
  QPointF m_drag_last_pos;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_CANVAS_H_
