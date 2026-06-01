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

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QQuickItem>
#include <QSet>
#include <QVariantMap>

#include "ais_selection_view_model.h"  // complete type needed for Q_PROPERTY
#include "tide_graph_view_model.h"     // complete type needed for Q_PROPERTY
#include "chart_extent.h"  // CellExtent -- catalog entry (value type)
#include "chart_source_model.h"  // complete type needed for Q_PROPERTY
#include "connections_view_model.h"  // complete type needed for Q_PROPERTY
#include "nmea_monitor_model.h"  // complete type needed for Q_PROPERTY
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
class ShapefileBasemapProvider;
class ChartWorker;
class DemoNavDataProvider;
class ModelNavDataProvider;
class SwitchableNavDataProvider;
class AisLayer;
class OwnShipLayer;
class RouteLayer;
class TideLayer;

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
  // 1:N below which (more zoomed out) objects lacking a SCAMIN are hidden, so
  // un-SCAMIN'd buoys/lights/sector arcs thin out at small scale. Configurable.
  Q_PROPERTY(double detailScale READ detailScale WRITE setDetailScale NOTIFY
                 detailScaleChanged)
  // Quilt over-zoom factor k: a chart's content renders once the view is zoomed
  // in to within k x of its natural scale (1 = strictly at native scale .. 5 =
  // wx's range). Lower keeps the scale-appropriate chart longer; higher pulls in
  // detail earlier. Configurable in the chart-settings dialog.
  Q_PROPERTY(double overzoomFactor READ overzoomFactor WRITE setOverzoomFactor
                 NOTIFY overzoomFactorChanged)

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
  // Selected tide/current station behind the bottom graph drawer (P3.14 F).
  Q_PROPERTY(ocpn::qtui::TideGraphViewModel* tideGraph READ tideGraph CONSTANT)

  // S-57 features under the last click, for the object-query popup (P3.9).
  Q_PROPERTY(ocpn::qtui::ObjectQueryViewModel* objectQuery READ objectQuery
                 CONSTANT)

  // Route & mark manager (P3.7): the route/waypoint lists + per-layer
  // visibility toggles.
  Q_PROPERTY(ocpn::qtui::RouteListViewModel* routeList READ routeList CONSTANT)

  // Data-source connections for the Options > Connections tab (#34).
  Q_PROPERTY(ocpn::qtui::ConnectionsViewModel* connections READ connections
                 CONSTANT)

  // Chart directories for the Options > Charts > Chart Files page.
  Q_PROPERTY(ocpn::qtui::ChartSourceModel* chartSource READ chartSource
                 CONSTANT)

  // Decoded-message stream for the Data Monitor view.
  Q_PROPERTY(ocpn::qtui::NmeaMonitorModel* nmeaMonitor READ nmeaMonitor
                 CONSTANT)
  Q_PROPERTY(bool showRoutes READ showRoutes WRITE setShowRoutes NOTIFY
                 overlayVisibilityChanged)
  Q_PROPERTY(bool showTracks READ showTracks WRITE setShowTracks NOTIFY
                 overlayVisibilityChanged)
  Q_PROPERTY(bool showWaypoints READ showWaypoints WRITE setShowWaypoints
                 NOTIFY overlayVisibilityChanged)

  // Cursor geographic position for the window status bar (formatted lat/lon),
  // updated on hover. Empty until the cursor enters the canvas.
  Q_PROPERTY(QString cursorText READ cursorText NOTIFY cursorMoved)
  // Bearing + range from own ship to the cursor (wx STAT_FIELD_CURSOR_BRGRNG),
  // formatted per the Display units. Empty when own-ship fix is invalid.
  Q_PROPERTY(QString cursorBrgRngText READ cursorBrgRngText NOTIFY cursorMoved)

  // Interactive route-building mode (Create Route, #28). While on, a left
  // click drops a route vertex and a right click finishes the route.
  Q_PROPERTY(bool routeBuildMode READ routeBuildMode WRITE setRouteBuildMode
                 NOTIFY routeBuildModeChanged)

  // Own-ship track recording (#29). While on, each own-ship fix is appended
  // to the active track and drawn by the track overlay.
  Q_PROPERTY(bool trackRecording READ trackRecording WRITE setTrackRecording
                 NOTIFY trackRecordingChanged)

  // Display colour scheme (#30): 0=day, 1=dusk, 2=night. Re-decodes loaded
  // S-52 cells through the new palette and re-tints the basemap; the QML
  // chrome dims to match.
  Q_PROPERTY(int colorScheme READ colorScheme WRITE setColorScheme NOTIFY
                 colorSchemeChanged)

  // Index of the user route currently selected for editing, or -1 (#31).
  // While >= 0 the route is emphasised, its nodes are draggable, a click on
  // a segment inserts a point and a right-click on a node opens a menu.
  Q_PROPERTY(int selectedRoute READ selectedRoute NOTIFY selectedRouteChanged)

  // Debug perf readout for the HUD: smoothed fps + last frame time, updated
  // from the render thread (throttled). Frozen while idle (render-on-demand).
  Q_PROPERTY(QString perfText READ perfText NOTIFY perfTextChanged)

  // MUIBar (P3.x): current chart scale "1:N" + follow-own-ship mode.
  Q_PROPERTY(QString scaleText READ scaleText NOTIFY viewChanged)
  Q_PROPERTY(bool followOwnShip READ followOwnShip WRITE setFollowOwnShip
                 NOTIFY followOwnShipChanged)
  // Current chart rotation (degrees), for the compass overlay. 0 = north-up;
  // non-zero in Course-Up / Head-Up. NOTIFY viewChanged (fires on rotation).
  Q_PROPERTY(double chartRotationDeg READ chartRotationDeg NOTIFY viewChanged)

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
  double detailScale() const { return m_detail_scale; }
  void setDetailScale(double n);
  double overzoomFactor() const { return m_overzoom_k; }
  void setOverzoomFactor(double k);

  bool demoMode() const { return m_demo_mode; }
  void setDemoMode(bool on);

  NavStateViewModel* navState() const { return m_nav_state.get(); }
  AisSelectionViewModel* selectedAis() const { return m_ais_selection.get(); }
  TideGraphViewModel* tideGraph() const { return m_tide_graph.get(); }
  ObjectQueryViewModel* objectQuery() const { return m_object_query.get(); }
  RouteListViewModel* routeList() const { return m_route_list.get(); }
  ConnectionsViewModel* connections() const { return m_connections.get(); }
  ChartSourceModel* chartSource() const { return m_chart_source.get(); }
  NmeaMonitorModel* nmeaMonitor() const { return m_nmea_monitor.get(); }

  bool showRoutes() const;
  void setShowRoutes(bool on);
  bool showTracks() const;
  void setShowTracks(bool on);
  bool showWaypoints() const;
  void setShowWaypoints(bool on);

  QString scaleText() const;
  QString perfText() const { return m_perf_text; }
  QString cursorText() const { return m_cursor_text; }
  QString cursorBrgRngText() const { return m_cursor_brgrng_text; }
  bool routeBuildMode() const { return m_route_build_mode; }
  void setRouteBuildMode(bool on);
  bool trackRecording() const { return m_track_recording; }
  void setTrackRecording(bool on);
  int colorScheme() const { return m_color_scheme; }
  void setColorScheme(int scheme);
  int selectedRoute() const { return m_selected_route; }

  // Route-edit actions (bound from the node context menu / chrome, #31).
  Q_INVOKABLE void clearRouteSelection();
  Q_INVOKABLE void deleteRoutePointAtMenu();  // node the menu opened on
  Q_INVOKABLE void deleteSelectedRoute();

  // Route-manager actions on a route by index (#33).
  Q_INVOKABLE void reverseRoute(int index);
  Q_INVOKABLE void renameRoute(int index, const QString& name);
  Q_INVOKABLE void deleteRoute(int index);
  bool followOwnShip() const { return m_follow_own_ship; }
  void setFollowOwnShip(bool on);
  double chartRotationDeg() const;

  // Center + zoom the viewport to a lat/lon bounding box (route/mark "zoom
  // to"). A near-zero span zooms in to a sensible harbour scale.
  Q_INVOKABLE void fitBounds(double north, double south, double east,
                             double west);

  // Toolbar actions (bound from the QML chrome). Zoom about the canvas
  // centre; fitWorld zooms out to show the whole scanned chart set.
  Q_INVOKABLE void zoomIn();
  Q_INVOKABLE void zoomOut();
  Q_INVOKABLE void fitWorld();

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

  // Scale bar (mirrors wx ScaleBarDraw, simplified form): a "nice" round
  // distance for the current zoom + its on-screen pixel length. Returns
  // { "length": <px>, "label": "<n> <unit>" }, using the Display units. The
  // QML scale bar binds to this and refreshes on viewChanged. Empty if the
  // viewport is not ready.
  Q_INVOKABLE QVariantMap scaleBar() const;

Q_SIGNALS:
  void s52EngineChanged();
  void displayCategoryChanged();
  void showSoundingsChanged();
  void showTextChanged();
  void showLightsChanged();
  void showBuoysChanged();
  void detailScaleChanged();
  void overzoomFactorChanged();
  void demoModeChanged();
  void overlayVisibilityChanged();
  void viewChanged();
  void followOwnShipChanged();
  void perfTextChanged();
  void cursorMoved();
  void routeBuildModeChanged();
  void trackRecordingChanged();
  void colorSchemeChanged();
  void selectedRouteChanged();
  // Right-click on the chart at item-local (x, y); QML pops the context menu.
  void contextMenuRequested(qreal x, qreal y);
  // Right-click on a route node; QML pops the node menu (delete point/route).
  void routeNodeMenuRequested(qreal x, qreal y);
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
  // Enumerate .000 cells under the configured chart directories and (re)run
  // the catalog scan. Creates the worker on first use; evicts cells that fall
  // out of the catalog on a later rescan. Bound to ChartSourceModel changes.
  void reloadCharts();
  // Push the ChartConfig (Vector Chart Display options: depth shading/contours,
  // symbol/boundary style, important-text, SCAMIN) to the decode thread and
  // re-decode resident cells. Debounced off ChartConfig::changed.
  void applyChartConfig();
  // Evict every resident S-52 cell layer and re-run the quilt so they
  // re-decode with the current global s52plib settings. Shared by
  // setColorScheme / applyChartConfig.
  void reloadResidentCells();
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
  static double displayScaleN(double scale, double centerLat = 0.0);
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
  // "Demo" is the standard OpenCPN Hakefjord NMEA-log replay; "live" reads
  // the model fed by user connections. Both are ModelNavDataProviders.
  std::unique_ptr<ModelNavDataProvider> m_demo_provider;
  std::unique_ptr<ModelNavDataProvider> m_model_provider;
  std::unique_ptr<SwitchableNavDataProvider> m_nav_provider;
  // View-model over m_nav_provider, exposed to the QML HUD (P3.2).
  std::unique_ptr<NavStateViewModel> m_nav_state;
  // Selected AIS target for the info popup (P3.9).
  std::unique_ptr<AisSelectionViewModel> m_ais_selection;
  // Selected tide/current station for the bottom graph drawer (P3.14 F).
  std::unique_ptr<TideGraphViewModel> m_tide_graph;
  // S-57 object-query result for the query popup (P3.9).
  std::unique_ptr<ObjectQueryViewModel> m_object_query;
  // Route/waypoint lists for the route & mark manager (P3.7).
  std::unique_ptr<RouteListViewModel> m_route_list;
  // Data-source connections (Options > Connections, #34).
  std::unique_ptr<ConnectionsViewModel> m_connections;
  // Chart directories (Options > Charts > Chart Files); drives the scan.
  std::unique_ptr<ChartSourceModel> m_chart_source;
  // Decoded-message stream for the Data Monitor view.
  std::unique_ptr<NmeaMonitorModel> m_nmea_monitor;

  std::unique_ptr<LayerCompositor> m_compositor;
  std::unique_ptr<Viewport> m_viewport;
  // Backs per-Layer visible/zOrder/opacity persistence (P2.10). Handed to
  // the compositor; saved on destruction.
  std::unique_ptr<OcpnConfig> m_layer_config;

  // Nav overlays (owned by their ChartLayer-less Layer entries in the
  // compositor; pointers kept only to forward state). World-anchored, on top.
  AisLayer* m_ais_layer = nullptr;
  OwnShipLayer* m_own_ship_layer = nullptr;
  RouteLayer* m_route_layer = nullptr;  // for colour-scheme line tinting
  TideLayer* m_tide_layer = nullptr;    // tide/current stations (P3.14 D)
  // Demo (Hakefjord replay) is OFF by default and opt-in only: the app boots
  // into the live setup (persisted connections auto-start). Persisted in
  // ConfigStore ("display/demoMode"); the constructor reads it.
  bool m_demo_mode = false;
  bool m_live_centered = false;  // recentre on own ship once per live session
  bool m_follow_own_ship = false;  // MUIBar follow mode

  // Chart orientation (North-Up / Course-Up / Head-Up). Recomputes the
  // viewport rotation from DisplayConfig.navMode + the own-ship COG/HDT.
  // Course-Up smooths COG into m_cog_avg (circular EMA, window from
  // DisplayConfig.chartRotationAveraging). Mirrors wx DoCanvasCOGSet.
  void updateChartRotation();
  double m_cog_avg = 0.0;
  bool m_cog_avg_valid = false;

  // Non-owning; set from QML. nullptr until bound.
  S52Engine* m_s52_engine = nullptr;
  int m_display_category = 1;  // 0 Base, 1 Standard, 2 All
  bool m_show_soundings = true;
  bool m_show_text = true;
  bool m_show_lights = true;
  bool m_show_buoys = true;
  // Last-applied object-height unit (DisplayConfig order: 0 m, 1 ft) and depth
  // unit (0 m, 1 ft, 2 fathoms). Both bake into the decode (height text + the
  // SNDFRM obstruction soundings + the provider's SOUNDG formatter), so a change
  // to either triggers applyChartConfig().
  int m_height_unit = 0;
  int m_depth_unit = 0;
  double m_detail_scale = 100000.0;  // default min display scale (no SCAMIN)
  double m_overzoom_k = 2.0;  // quilt over-zoom factor (render at <= native*k)
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
  // World basemap provider (owned by its ChartLayer); kept for colour-scheme
  // re-tinting.
  ShapefileBasemapProvider* m_basemap = nullptr;
  // The decode-free catalog, keyed by cell name.
  QHash<QString, CellExtent> m_catalog;
  // Cells already asked of the worker (loaded or in flight) -- never twice.
  QSet<QString> m_requested;
  // Coalesces a burst of pan/zoom into one visible-cell evaluation.
  QTimer* m_load_debounce = nullptr;
  QTimer* m_chart_cfg_debounce = nullptr;  // coalesces ChartConfig edits
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

  // Debug perf readout (fps + frame ms). m_frame_clock + m_fps_smooth +
  // m_frame_count are touched ONLY on the render thread (updatePaintNode);
  // m_perf_text ONLY on the GUI thread (published via a queued invoke), so
  // there's no shared-state race.
  QElapsedTimer m_frame_clock;
  double m_fps_smooth = 0.0;
  quint64 m_frame_count = 0;
  QString m_perf_text;

  // Formatted cursor lat/lon for the status bar, updated on hover.
  QString m_cursor_text;
  QString m_cursor_brgrng_text;
  bool m_route_build_mode = false;
  bool m_track_recording = false;
  int m_color_scheme = 0;  // 0 day, 1 dusk, 2 night

  // Route editing (#31).
  int m_selected_route = -1;     // selected user route, or -1
  bool m_dragging_node = false;  // a node drag is in progress
  int m_drag_node = -1;          // node index being dragged
  int m_menu_route = -1;         // route/node a right-click node menu targets
  int m_menu_node = -1;
  // Hit-test the user routes (screen px). Return the route + node within a
  // small radius, or the nearest segment + the cursor's lat/lon for insert.
  bool hitRouteNode(const QPointF& sp, int& route, int& node) const;
  bool hitRouteSegment(const QPointF& sp, int& route, int& seg, double& lat,
                       double& lon) const;
  void selectRoute(int route);  // set selection + sync the overlay highlight

  // Hit-test a click (item-local px) against the live AIS targets and select
  // the nearest within a small radius (P3.9). Returns true if one was hit.
  bool pickAisAt(const QPointF& screen_pos);
  // Select the nearest tide/current station within a small radius (P3.14 F).
  bool pickTideStationAt(const QPointF& screen_pos);
  // Hit-test a click against the loaded S-52 chart objects and populate the
  // object-query result (P3.9).
  void pickObjectsAt(const QPointF& screen_pos);
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_CANVAS_H_
