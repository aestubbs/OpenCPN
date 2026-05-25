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

#include "ais_selection_view_model.h"  // complete type needed for Q_PROPERTY
#include "chart_extent.h"  // CellExtent -- catalog entry (value type)
#include "nav_state_view_model.h"  // complete type needed for Q_PROPERTY
#include "object_query_view_model.h"  // complete type needed for Q_PROPERTY
#include "route_list_view_model.h"    // complete type needed for Q_PROPERTY
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

  // Live nav state (own ship + AIS count) for the QML HUD (P3.2/P3.4).
  Q_PROPERTY(ocpn::qtui::NavStateViewModel* navState READ navState CONSTANT)

  // Currently-selected AIS target for the info popup (P3.9).
  Q_PROPERTY(ocpn::qtui::AisSelectionViewModel* selectedAis READ selectedAis
                 CONSTANT)

  // S-57 features under the last click, for the object-query popup (P3.9).
  Q_PROPERTY(ocpn::qtui::ObjectQueryViewModel* objectQuery READ objectQuery
                 CONSTANT)

  // Route & mark manager (P3.7): the route/waypoint lists + per-layer
  // visibility toggles.
  Q_PROPERTY(ocpn::qtui::RouteListViewModel* routeList READ routeList CONSTANT)
  Q_PROPERTY(bool showRoutes READ showRoutes WRITE setShowRoutes NOTIFY
                 overlayVisibilityChanged)
  Q_PROPERTY(bool showTracks READ showTracks WRITE setShowTracks NOTIFY
                 overlayVisibilityChanged)
  Q_PROPERTY(bool showWaypoints READ showWaypoints WRITE setShowWaypoints
                 NOTIFY overlayVisibilityChanged)

  // Cursor geographic position for the window status bar (formatted lat/lon),
  // updated on hover. Empty until the cursor enters the canvas.
  Q_PROPERTY(QString cursorText READ cursorText NOTIFY cursorMoved)

  // MUIBar (P3.x): current chart scale "1:N" + follow-own-ship mode.
  Q_PROPERTY(QString scaleText READ scaleText NOTIFY viewChanged)
  Q_PROPERTY(bool followOwnShip READ followOwnShip WRITE setFollowOwnShip
                 NOTIFY followOwnShipChanged)

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

  NavStateViewModel* navState() const { return m_nav_state.get(); }
  AisSelectionViewModel* selectedAis() const { return m_ais_selection.get(); }
  ObjectQueryViewModel* objectQuery() const { return m_object_query.get(); }
  RouteListViewModel* routeList() const { return m_route_list.get(); }

  bool showRoutes() const;
  void setShowRoutes(bool on);
  bool showTracks() const;
  void setShowTracks(bool on);
  bool showWaypoints() const;
  void setShowWaypoints(bool on);

  QString scaleText() const;
  QString cursorText() const { return m_cursor_text; }
  bool followOwnShip() const { return m_follow_own_ship; }
  void setFollowOwnShip(bool on);

  // Center + zoom the viewport to a lat/lon bounding box (route/mark "zoom
  // to"). A near-zero span zooms in to a sensible harbour scale.
  Q_INVOKABLE void fitBounds(double north, double south, double east,
                             double west);

  // Toolbar actions (bound from the QML chrome). Zoom about the canvas
  // centre; fitWorld zooms out to show the whole scanned chart set.
  Q_INVOKABLE void zoomIn();
  Q_INVOKABLE void zoomOut();
  Q_INVOKABLE void fitWorld();
  // Re-seed the demo nav fleet around the current view centre (P2.11).
  Q_INVOKABLE void dropDemoHere();

  // Right-click context-menu actions (operate on the world point recorded at
  // the last right-click). centerViewHere recentres; queryObjectsHere fills
  // the objectQuery view-model (the QML query window binds to it).
  Q_INVOKABLE void centerViewHere();
  Q_INVOKABLE void queryObjectsHere();

  // Chart bar / "Piano" (P3.8): the catalogued ENC cells whose coverage
  // intersects the current view, each a QVariantMap { name, band, scale,
  // displayed } ordered coarse->fine. The QML chart bar binds to this and
  // refreshes on chartCoverageChanged. "displayed" = currently in the quilt.
  Q_INVOKABLE QVariantList chartBarCells() const;

  // Highlight (or clear, with an empty name) a chart-bar cell's coverage on
  // the chart -- the Piano hover/rollover action (mirrors wx
  // HandlePianoRollover; non-destructive, does not pan).
  Q_INVOKABLE void highlightChartCell(const QString& name);

  // Autoscale the view to a chart's native scale (Piano click; mirrors wx
  // HandlePianoClick / SelectQuiltRefdbChart autoscale).
  Q_INVOKABLE void selectChart(const QString& name);

Q_SIGNALS:
  void s52EngineChanged();
  void displayCategoryChanged();
  void showSoundingsChanged();
  void showTextChanged();
  void showLightsChanged();
  void showBuoysChanged();
  void demoModeChanged();
  void overlayVisibilityChanged();
  void viewChanged();
  void followOwnShipChanged();
  void cursorMoved();
  // Right-click on the chart at item-local (x, y); QML pops the context menu.
  void contextMenuRequested(qreal x, qreal y);
  // The set of in-view / displayed ENC cells changed (chart bar refresh).
  void chartCoverageChanged();

protected:
  QSGNode* updatePaintNode(QSGNode* old_node,
                           UpdatePaintNodeData* update_data) override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void hoverMoveEvent(QHoverEvent* event) override;
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
  std::unique_ptr<SwitchableNavDataProvider> m_nav_provider;
  // View-model over m_nav_provider, exposed to the QML HUD (P3.2).
  std::unique_ptr<NavStateViewModel> m_nav_state;
  // Selected AIS target for the info popup (P3.9).
  std::unique_ptr<AisSelectionViewModel> m_ais_selection;
  // S-57 object-query result for the query popup (P3.9).
  std::unique_ptr<ObjectQueryViewModel> m_object_query;
  // Route/waypoint lists for the route & mark manager (P3.7).
  std::unique_ptr<RouteListViewModel> m_route_list;

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
  bool m_follow_own_ship = false;  // MUIBar follow mode

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
  QPointF m_press_pos;  // to tell a click (AIS pick) from a drag (pan)

  // World point + screen pos recorded at the last right-click, consumed by
  // the context-menu actions (centerViewHere / queryObjectsHere).
  QPointF m_ctx_pos;
  double m_ctx_lat = 0.0;
  double m_ctx_lon = 0.0;

  // Formatted cursor lat/lon for the status bar, updated on hover.
  QString m_cursor_text;

  // Hit-test a click (item-local px) against the live AIS targets and select
  // the nearest within a small radius (P3.9). Returns true if one was hit.
  bool pickAisAt(const QPointF& screen_pos);
  // Hit-test a click against the loaded S-52 chart objects and populate the
  // object-query result (P3.9).
  void pickObjectsAt(const QPointF& screen_pos);
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_CANVAS_H_
