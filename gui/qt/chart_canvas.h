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
#include <QUrl>
#include <QVariantMap>

#include "ais_selection_view_model.h"  // complete type needed for Q_PROPERTY
#include "tide_graph_view_model.h"     // complete type needed for Q_PROPERTY
#include "chart_extent.h"  // CellExtent -- catalog entry (value type)
#include "chart_spatial_index.h"  // grid index over the catalog (value member)
#include "chart_source_model.h"  // complete type needed for Q_PROPERTY
#include "connections_view_model.h"  // complete type needed for Q_PROPERTY
#include "nmea_monitor_model.h"  // complete type needed for Q_PROPERTY
#include "nav_state_view_model.h"  // complete type needed for Q_PROPERTY
#include "object_query_view_model.h"  // complete type needed for Q_PROPERTY
#include "alert_engine.h"  // complete type needed for Q_PROPERTY
#include "route_list_view_model.h"    // complete type needed for Q_PROPERTY
#include "route_follower.h"           // complete type needed for Q_PROPERTY
#include "sim_ship_controller.h"      // complete type needed for Q_PROPERTY
#include "peer_send_controller.h"     // complete type needed for Q_PROPERTY
#include "gps_upload_controller.h"    // complete type needed for Q_PROPERTY
#include "plugin/plugin_registry.h"   // complete type needed for Q_PROPERTY
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
class PickHighlightProvider;
class ShapefileBasemapProvider;
class ChartWorker;
class DemoNavDataProvider;
class ModelNavDataProvider;
class SwitchableNavDataProvider;
class AisLayer;
class OwnShipLayer;
class RouteLayer;
class RouteFollowLayer;
class WaypointLayer;
class TrackLayer;
class MeasureLayer;
class TideLayer;
class RouteFollower;
class SimShipController;

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
  // S-57 class acronyms hidden while displayCategory == 3 (Mariner's
  // Standard) -- the wx "User Standard Objects" per-class filter (P3.6).
  Q_PROPERTY(QStringList hiddenObjectClasses READ hiddenObjectClasses
                 WRITE setHiddenObjectClasses NOTIFY hiddenObjectClassesChanged)
  // wx menu parity (ID_MENU_ENC_ANCHOR): show/hide the anchoring-info
  // classes (the wx SetAnchorOn set) on top of the user's own hidden set.
  Q_PROPERTY(bool showEncAnchoring READ showEncAnchoring WRITE
                 setShowEncAnchoring NOTIFY hiddenObjectClassesChanged)

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
  // Over-scale factor: how many times more zoomed-in the display is than the
  // finest chart covering the view was compiled for (display 1:N / chart 1:N).
  // <= ~1 means at/within native scale; > 1 means the chart is magnified beyond
  // its survey detail (S-52 overscale). 0 when no chart is displayed. The QML
  // HUD shows an "OVERSCALE xN" warning when this exceeds the threshold.
  Q_PROPERTY(double overscaleFactor READ overscaleFactor NOTIFY
                 overscaleChanged)

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

  // Navigation alert engine (AIS CPA/TCPA danger, ...) for the alert banner
  // (P3.15).
  Q_PROPERTY(ocpn::qtui::AlertEngine* alerts READ alerts CONSTANT)

  // Route & mark manager (P3.7): the route/waypoint lists + per-layer
  // visibility toggles.
  Q_PROPERTY(ocpn::qtui::RouteListViewModel* routeList READ routeList CONSTANT)

  // Active-route following (P3.16): the live nav solution (BTW/DTW/XTE/ETA,
  // active waypoint, arrival) for the nav strip + arrival banner.
  Q_PROPERTY(ocpn::qtui::RouteFollower* routeFollower READ routeFollower CONSTANT)

  // Test ship (P3.16): a synthetic, mouse-placed + cursor-key-steered GPS for
  // simulating a voyage; binds the on-chart sim panel.
  Q_PROPERTY(ocpn::qtui::SimShipController* simShip READ simShip CONSTANT)

  // Send-to-Peer (P3.18 tier 4): mDNS peer discovery + transfer.
  Q_PROPERTY(ocpn::qtui::PeerSendController* peerSend READ peerSend CONSTANT)
  // Send-to-GPS (P3.18 tier 4): serial NMEA-0183 route/mark upload.
  Q_PROPERTY(ocpn::qtui::GpsUploadController* gpsUpload READ gpsUpload CONSTANT)
  // Qt plugin host (P4.2): catalogue + contributions.
  Q_PROPERTY(ocpn::qtui::PluginRegistry* pluginRegistry READ pluginRegistry
                 CONSTANT)

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
  // Bus-derived stats for the HUD stats panel (DPT depth / MTW water
  // temperature parsed from the decoded NMEA stream; "--" until seen).
  Q_PROPERTY(QString depthText READ depthText NOTIFY busStatsChanged)
  Q_PROPERTY(QString waterTempText READ waterTempText NOTIFY busStatsChanged)
  // Raw cursor position for overlays that sample fields at the cursor
  // (e.g. the GRIB readout). NaN until the first move.
  Q_PROPERTY(double cursorLat READ cursorLat NOTIFY cursorMoved)
  Q_PROPERTY(double cursorLon READ cursorLon NOTIFY cursorMoved)
  // Bearing + range from own ship to the cursor (wx STAT_FIELD_CURSOR_BRGRNG),
  // formatted per the Display units. Empty when own-ship fix is invalid.
  Q_PROPERTY(QString cursorBrgRngText READ cursorBrgRngText NOTIFY cursorMoved)

  // Interactive route-building mode (Create Route, #28). While on, a left
  // click drops a route vertex and a right click finishes the route.
  Q_PROPERTY(bool routeBuildMode READ routeBuildMode WRITE setRouteBuildMode
                 NOTIFY routeBuildModeChanged)
  // True while a selected route is editable (drag nodes / insert / delete);
  // entered via the drawer's Edit action, exited by deselecting.
  Q_PROPERTY(bool routeEditMode READ routeEditMode WRITE setRouteEditMode NOTIFY
                 routeEditModeChanged)
  // Bumped whenever a route's visibility "eye" changes, so QML eye bindings
  // (chart.routeVisible(index)) re-evaluate. Visibility is independent of
  // selection: a route draws when its eye is on or it is selected (P3.7).
  Q_PROPERTY(int routeVisibilityRevision READ routeVisibilityRevision NOTIFY
                 routeVisibilityChanged)

  // Measure tool (P3.18, wx F4/"Measure"). While active a left click drops a
  // measure point, the cursor trails a dashed rubber-band, and measureText
  // carries the running leg bearing/distance + total for the QML readout.
  Q_PROPERTY(bool measureActive READ measureActive NOTIFY measureChanged)
  Q_PROPERTY(QString measureText READ measureText NOTIFY measureChanged)

  // Undo/redo of mark create/delete (P3.18 tier 4, wx undo.cpp scope).
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)

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
  double overscaleFactor() const { return m_overscale_factor; }
  void setOverzoomFactor(double k);

  bool demoMode() const { return m_demo_mode; }
  void setDemoMode(bool on);

  NavStateViewModel* navState() const { return m_nav_state.get(); }
  AisSelectionViewModel* selectedAis() const { return m_ais_selection.get(); }
  TideGraphViewModel* tideGraph() const { return m_tide_graph.get(); }
  ObjectQueryViewModel* objectQuery() const { return m_object_query.get(); }
  AlertEngine* alerts() const { return m_alert_engine.get(); }
  RouteListViewModel* routeList() const { return m_route_list.get(); }
  RouteFollower* routeFollower() const { return m_route_follower.get(); }
  SimShipController* simShip() const { return m_sim_ship.get(); }
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
  QString depthText() const { return m_depth_text; }
  QString waterTempText() const { return m_wtemp_text; }
  double cursorLat() const { return m_cursor_pos_lat; }
  double cursorLon() const { return m_cursor_pos_lon; }
  QString cursorBrgRngText() const { return m_cursor_brgrng_text; }
  bool routeBuildMode() const { return m_route_build_mode; }
  void setRouteBuildMode(bool on);
  bool trackRecording() const { return m_track_recording; }
  void setTrackRecording(bool on);

  // --- Tracks (own-vessel), P3.7 ---
  Q_INVOKABLE void resetTrack();   // finalize current + start a fresh one
  Q_INVOKABLE void showTrack(const QString& guid);   // select + zoom to extent
  Q_INVOKABLE void renameTrack(const QString& guid, const QString& name);
  Q_INVOKABLE void deleteTrack(const QString& guid);
  Q_INVOKABLE void setTrackVisible(const QString& guid, bool on);
  int colorScheme() const { return m_color_scheme; }
  void setColorScheme(int scheme);
  int selectedRoute() const { return m_selected_route; }

  // Route-edit actions (bound from the node context menu / chrome, #31).
  Q_INVOKABLE void clearRouteSelection();
  Q_INVOKABLE void deleteRoutePointAtMenu();  // node the menu opened on
  Q_INVOKABLE void deleteSelectedRoute();

  // Route-manager actions on a route by index (#33).
  Q_INVOKABLE void reverseRoute(int index);
  Q_INVOKABLE void duplicateRoute(int index);
  Q_INVOKABLE void renameRoute(int index, const QString& name);
  Q_INVOKABLE void deleteRoute(int index);
  // Per-route mark icon (route-details dialog): set all the route's points to
  // `icon`; routePointIcon reads the current one ("" = plain dots).
  Q_INVOKABLE void setRoutePointIcon(int index, const QString& icon);
  Q_INVOKABLE QString routePointIcon(int index) const;
  // Route-following actions (P3.16), mirroring the wx Route Manager's
  // Activate/Deactivate. activateRoute makes the route visible, zooms to it,
  // and starts following from the best waypoint for the current fix.
  Q_INVOKABLE void activateRoute(int index);
  Q_INVOKABLE void deactivateRoute();
  Q_INVOKABLE void skipWaypoint();      // advance past the current waypoint

  // --- "User Standard Objects" per-class filter (P3.6) ---
  QStringList hiddenObjectClasses() const { return m_hidden_classes; }
  bool showEncAnchoring() const { return m_show_enc_anchoring; }
  void setShowEncAnchoring(bool on);
  /** wx menu parity (ID_MENU_SCALE_IN/OUT): jump the view to the next
   *  finer (+1) / coarser (-1) chart scale available at the centre. */
  Q_INVOKABLE void scaleChartStep(int dir);
  void setHiddenObjectClasses(const QStringList& classes);
  // The full S-57 class catalogue for the checklist UI: a list of
  // {acronym, description} maps, sorted by acronym.
  Q_INVOKABLE QVariantList s57ClassCatalog() const;

  // --- MMSI properties (P3.6, wx MmsiProperties): per-vessel AIS handling
  //     (ignore / always-never track / MOB / persist track). Edits apply to
  //     the live decoder (g_MMSI_Props_Array) and persist in the config. ---
  Q_INVOKABLE QVariantList mmsiProperties() const;
  Q_INVOKABLE void saveMmsiProperty(const QVariantMap& row);  // keyed by mmsi
  Q_INVOKABLE void deleteMmsiProperty(int mmsi);

  // --- GPX import / export (P3.19, wx Route Manager Import/Export) ---
  // QML FileDialogs hand over file:// URLs; converted here. importGpx
  // returns {routes, tracks, waypoints, duplicates} counts (empty = parse
  // failure); the export calls return success.
  Q_INVOKABLE QVariantMap importGpx(const QUrl& url);
  Q_INVOKABLE bool exportGpxAll(const QUrl& url) const;
  Q_INVOKABLE bool exportGpxRoute(int index, const QUrl& url) const;
  Q_INVOKABLE bool exportGpxTrack(const QString& guid, const QUrl& url) const;
  Q_INVOKABLE bool exportGpxWaypoint(const QString& guid,
                                     const QUrl& url) const;

  // --- AIS context-menu / target-list actions (P3.18 tier 2) ---
  // Select the target by MMSI (opens the AIS info popup, as a click would).
  Q_INVOKABLE void selectAisTarget(int mmsi);
  // Centre the view on the target's current position.
  Q_INVOKABLE void centerOnAis(int mmsi);
  // System clipboard (Copy MMSI).
  Q_INVOKABLE void copyToClipboard(const QString& text) const;
  // Copy a nav object to the clipboard as KML (P3.18 tier 4, wx
  // Kml::Copy*ToClipboard). Returns false if the object wasn't found.
  Q_INVOKABLE bool copyRouteAsKml(int index) const;
  Q_INVOKABLE bool copyTrackAsKml(const QString& guid) const;
  Q_INVOKABLE bool copyMarkAsKml(const QString& guid) const;
  // Paste KML from the clipboard: Point Placemarks become marks,
  // LineStrings become routes. Returns {routes, waypoints} added.
  Q_INVOKABLE QVariantMap pasteKmlFromClipboard();
  // Snapshot of the live AIS targets for the Target List window, sorted by
  // range (unknown-range targets last). Each entry: mmsi, name, rangeText,
  // bearingText, sogText, cogText, cpaText, tcpaText, dangerous, isSart.
  Q_INVOKABLE QVariantList aisTargetSnapshot() const;

  // --- Canvas context-menu actions (P3.18) ---
  // Build + activate a temporary GOTO route from the own-ship fix to the
  // right-click point / to a mark (wx "Navigate To Here" / "Navigate To
  // This"). The route is deleted automatically on arrival at its end.
  Q_INVOKABLE void navigateToHere();
  Q_INVOKABLE void navigateToWaypoint(const QString& guid);
  // Reset the XTE origin for the active leg (wx "Zero XTE").
  Q_INVOKABLE void zeroXte();
  // Insert a waypoint into the segment the route context menu opened on.
  Q_INVOKABLE void insertRoutePointAtMenu();
  // Extend a route by clicking points (wx "Append waypoint"): enters the
  // normal route-build mouse flow, appending to the existing route.
  Q_INVOKABLE void appendToRoute(int index);
  // Split the route around the leg the menu opened on (wx "Split around
  // Leg"): "<name> A" + "<name> B" replace the original.
  Q_INVOKABLE void splitRouteAtMenu();

  // --- Undo / redo (P3.18 tier 4) ---
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  bool canUndo() const { return !m_undo_stack.isEmpty(); }
  bool canRedo() const { return !m_redo_stack.isEmpty(); }

  // --- Measure tool (P3.18) ---
  Q_INVOKABLE void startMeasure();
  Q_INVOKABLE void stopMeasure();
  bool measureActive() const { return m_measure_active; }
  QString measureText() const { return m_measure_text; }
  // Drop the test ship at the last right-click point (m_ctx_lat/lon) and make
  // it the live position source (P3.16).
  PeerSendController* peerSend() const { return m_peer_send.get(); }
  GpsUploadController* gpsUpload() const { return m_gps_upload.get(); }
  PluginRegistry* pluginRegistry() const { return m_plugin_registry.get(); }

  Q_INVOKABLE void placeSimShipHere();
  // Select (highlight) the route and zoom the viewport to its extent -- the
  // route-drawer tile click (P3.7).
  Q_INVOKABLE void showRoute(int index);
  // Like showRoute, then put the route into edit mode (drag nodes / insert /
  // delete) -- the drawer's per-tile "Edit" action.
  Q_INVOKABLE void editRoute(int index);
  bool routeEditMode() const { return m_route_edit_mode; }
  void setRouteEditMode(bool on);
  // Per-route visibility "eye" (independent of selection). index is into the
  // route list (RouteListViewModel order); resolved to a stable GUID inside.
  Q_INVOKABLE bool routeVisible(int index) const;
  Q_INVOKABLE void setRouteVisible(int index, bool on);
  int routeVisibilityRevision() const { return m_route_vis_rev; }

  // --- Marks (free waypoints), P3.7 ---
  // Drop a mark at the last right-click point (m_ctx_lat/lon); the QML New Mark
  // dialog supplies name / comment / icon. markDropLat/Lon expose that point so
  // the dialog can show it.
  Q_INVOKABLE void dropMarkHere(const QString& name, const QString& comment,
                                const QString& icon);
  /** wx menu parity (ID_MENU_MARK_CURSOR / ID_MENU_MARK_BOAT): instant
   *  drops with a dated default name -- no dialog; edit to rename. */
  Q_INVOKABLE void dropMarkAtCursor();
  Q_INVOKABLE void dropMarkAtBoat();
  /** Auto-anchor mark (wx parity): drop an anchor-icon mark at the given
   *  position, named by drop time. Used when the anchor watch is set. */
  Q_INVOKABLE void dropAnchorMark(double lat, double lon);
  /** Current viewport corners as {north, south, east, west} — for
   *  overlays/plugins that need the visible area (e.g. GRIB requests). */
  Q_INVOKABLE QVariantMap viewBounds() const;
  Q_INVOKABLE double markDropLat() const { return m_ctx_lat; }
  Q_INVOKABLE double markDropLon() const { return m_ctx_lon; }
  Q_INVOKABLE void showMark(const QString& guid);   // select + centre
  Q_INVOKABLE void setMarkVisible(const QString& guid, bool on);
  Q_INVOKABLE void renameMark(const QString& guid, const QString& name);
  Q_INVOKABLE void setMarkComment(const QString& guid, const QString& comment);
  Q_INVOKABLE void setMarkIcon(const QString& guid, const QString& icon);
  Q_INVOKABLE void deleteMark(const QString& guid);
  // Per-mark range rings + SCAMIN override (P3.6, persisted). units:
  // 0 = NM, 1 = km; scamin 0 = always show.
  Q_INVOKABLE void setMarkRangeRings(const QString& guid, bool show, int count,
                                     double step, int units);
  Q_INVOKABLE void setMarkScamin(const QString& guid, int scamin);
  // All waypoint-icon keys (for the editor's icon picker; images come from the
  // wpicon image provider).
  Q_INVOKABLE QStringList markIconNames() const;
  // Drop a man-overboard mark at the live own-ship fix (or the view centre if
  // there's no fix). Mirrors wx ActivateMOB's marker drop; wired to the MOB
  // toolbar button.
  Q_INVOKABLE void dropMob();
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
  // Set the chart scale to a 1:N denominator directly (the clickable status-bar
  // scale entry). Free-form like wx (mui_bar.cpp OnScaleSelected): the value is
  // clamped to 1:1,000 .. 1:3,000,000, not snapped to standard scales.
  Q_INVOKABLE void setScaleDenominator(double n);

  // Right-click context-menu actions (operate on the world point recorded at
  // the last right-click). centerViewHere recentres; queryObjectsHere fills
  // the objectQuery view-model (the QML query window binds to it).
  Q_INVOKABLE void centerViewHere();
  Q_INVOKABLE void queryObjectsHere();

  // AIS trail toggle for a vessel (driven by the "Show trail" check in the AIS
  // info popup). The trail draws its recorded path; see AisLayer (P3.15/tail).
  Q_INVOKABLE void setAisTrail(int mmsi, bool on);
  Q_INVOKABLE bool aisTrailEnabled(int mmsi) const;

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

signals:
  void s52EngineChanged();
  void displayCategoryChanged();
  void hiddenObjectClassesChanged();
  void showSoundingsChanged();
  void showTextChanged();
  void showLightsChanged();
  void showBuoysChanged();
  void detailScaleChanged();
  void overzoomFactorChanged();
  void overscaleChanged();
  void demoModeChanged();
  void overlayVisibilityChanged();
  void viewChanged();
  void followOwnShipChanged();
  void perfTextChanged();
  void cursorMoved();
  void busStatsChanged();
  void routeBuildModeChanged();
  void routeEditModeChanged();
  void routeVisibilityChanged();
  void trackRecordingChanged();
  void colorSchemeChanged();
  void selectedRouteChanged();
  // Right-click on the chart at item-local (x, y); QML pops the context menu.
  void contextMenuRequested(qreal x, qreal y);
  // Right-click on a route node; QML pops the node menu (delete point/route).
  void routeNodeMenuRequested(qreal x, qreal y);
  // Right-click on a route's line or node outside edit mode; QML pops the
  // route menu (P3.18). canInsert: the click hit a segment (not a node), so
  // "Insert waypoint here" is meaningful.
  void routeMenuRequested(qreal x, qreal y, int routeIndex, bool isActive,
                          bool canInsert);
  // Right-click on a free mark; QML pops the mark menu (P3.18).
  void markMenuRequested(qreal x, qreal y, const QString& guid,
                         const QString& name);
  // Right-click on an AIS target; QML pops the AIS menu (P3.18 tier 2).
  void aisMenuRequested(qreal x, qreal y, int mmsi, const QString& name);
  // Right-click on a track's line; QML pops the track menu (P3.18 tier 3).
  void trackMenuRequested(qreal x, qreal y, const QString& guid,
                          const QString& name);
  // Measure tool state / readout changed (P3.18).
  void measureChanged();
  void undoChanged();
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
  // Canvas keyboard layer (P3.20, wx chcanv::OnKeyDown): arrow-key pan,
  // +/-/= zoom, M measure toggle, Esc/Enter per mode. The canvas takes
  // focus on click; the QML test-ship overlay borrows it while simming.
  void keyPressEvent(QKeyEvent* event) override;

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
  void onRasterCellLoaded(const QString& id, const QImage& image,
                          double north, double south, double east, double west,
                          double worldYTop, double worldYBottom);
  void onCellLoaded(const QString& id, const s52sg::Buffer& buffer,
                    double north, double south, double east, double west);
  // A requested cell will not arrive: clear its in-flight flag, and if it
  // decoded to nothing (genuineEmpty) remember that so it isn't re-requested
  // every selection pass (which would busy-loop the decode thread).
  void onCellUnavailable(const QString& id, bool genuineEmpty);
  // Reconcile the set of loaded cells with the current view: request
  // catalogued cells that have come into view (and grown large enough on
  // screen to be worth decoding), and evict loaded cells that have left the
  // view or shrunk too small. Debounced off Viewport::changed.
  void updateVisibleCells();
  // Push each loaded cell its FINER-cell coverage (every other loaded cell of a
  // smaller native scale that overlaps it) so the provider can suppress point
  // annotations a finer cell owns -- one copy per feature, drawn by the finest
  // owner. The scene-graph analogue of wx's m_covered_region.Subtract. Called
  // whenever the loaded set changes (a cell loads or is evicted).
  void updateFinerCoverage();
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
  // Navigation alert engine (AIS CPA/TCPA danger, ...) (P3.15).
  std::unique_ptr<AlertEngine> m_alert_engine;
  // Route/waypoint lists for the route & mark manager (P3.7).
  std::unique_ptr<RouteListViewModel> m_route_list;
  // Active-route follower (P3.16): drives g_pRouteMan->UpdateProgress() each
  // nav tick and publishes the solution to QML.
  std::unique_ptr<RouteFollower> m_route_follower;
  // Test ship (P3.16): synthetic GPS for simulating a voyage.
  std::unique_ptr<SimShipController> m_sim_ship;
  std::unique_ptr<PeerSendController> m_peer_send;  // Send-to-Peer (P3.18)
  std::unique_ptr<GpsUploadController> m_gps_upload;  // Send-to-GPS (P3.18)
  std::unique_ptr<PluginRegistry> m_plugin_registry;   // Qt plugins (P4.2)
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
  RouteFollowLayer* m_route_follow_layer = nullptr;  // active-route accent
  WaypointLayer* m_waypoint_layer = nullptr;  // for mark selection highlight
  QString m_selected_waypoint_guid;           // selected mark (drawer / chart)
  TrackLayer* m_track_layer = nullptr;        // for track selection highlight
  QString m_selected_track_guid;              // selected track (drawer / chart)
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
  double m_overscale_factor = 0.0;  // display 1:N / finest displayed chart 1:N
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
  // Pick-highlight overlay: outlines the feature shown in the object-query
  // popup (owned by its ChartLayer in the compositor).
  PickHighlightProvider* m_pick_highlight = nullptr;
  // World basemap provider (owned by its ChartLayer); kept for colour-scheme
  // re-tinting.
  ShapefileBasemapProvider* m_basemap = nullptr;
  // The decode-free catalog, keyed by cell name.
  QHash<QString, CellExtent> m_catalog;
  // Grid index over m_catalog for O(cells-in-view) candidate gather. Rebuilt
  // whenever m_catalog changes (it holds pointers into m_catalog's values).
  ChartSpatialIndex m_spatial_index;
  // Cells already asked of the worker (loaded or in flight) -- never twice.
  QSet<QString> m_requested;
  // Cells that decoded to nothing under the current display settings: skipped
  // when building loads so a content-less cell isn't re-decoded every pass.
  // Cleared whenever a re-decode could change the outcome (display-setting or
  // colour-scheme change, catalog reload).
  QSet<QString> m_known_empty;
  // Coalesces a burst of pan/zoom into one visible-cell evaluation.
  QTimer* m_load_debounce = nullptr;
  QTimer* m_chart_cfg_debounce = nullptr;  // coalesces ChartConfig edits
  // Coalesces the finest-owner coverage re-derivation (O(loaded^2)) + chart-bar
  // refresh across a BURST of cellLoaded arrivals into one pass once they
  // settle. Without it, draining a backlog of K arrivals cost O(K * loaded^2)
  // on the GUI thread -- the "never recovers when zoomed out over many small
  // charts" stall, since each arrival re-touched every loaded provider.
  QTimer* m_finer_debounce = nullptr;
  // Edge auto-pan while route-building / measuring (P3.13, wx CheckEdgePan):
  // the cursor inside a 5%-margin edge band pans the view a small step per
  // 200 ms tick, so a route extends past the current view without stopping.
  QTimer* m_edge_pan_timer = nullptr;
  QPointF m_edge_pan_step;  // px per tick (screen-drag convention)
  void checkEdgePan(const QPointF& pos);
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
  QString m_depth_text = QStringLiteral("--");
  QString m_wtemp_text = QStringLiteral("--");
  double m_cursor_pos_lat = qQNaN();
  double m_cursor_pos_lon = qQNaN();
  QString m_cursor_brgrng_text;
  bool m_route_build_mode = false;
  bool m_route_edit_mode = false;  // selected route is editable (P3.7)
  QStringList m_hidden_classes;    // Mariner's Standard hidden classes (P3.6)
  bool m_show_enc_anchoring = true;
  QSet<QString> m_visible_routes;  // route GUIDs with the visibility eye on
  int m_route_vis_rev = 0;         // bumps on any eye change (QML re-eval)
  bool m_track_recording = false;
  int m_color_scheme = 0;  // 0 day, 1 dusk, 2 night

  // Route editing (#31).
  int m_selected_route = -1;     // selected user route, or -1
  bool m_dragging_node = false;  // a node drag is in progress
  int m_drag_node = -1;          // node index being dragged
  int m_menu_route = -1;         // route/node a right-click node menu targets
  int m_menu_node = -1;
  // Segment + insertion point the route context menu opened on (P3.18);
  // m_menu_seg is -1 when the menu opened on a node rather than a segment.
  int m_menu_seg = -1;
  double m_menu_ins_lat = 0.0;
  double m_menu_ins_lon = 0.0;
  // GUID of the temporary GOTO route ("Navigate to here"), deleted when the
  // follower reports arrival at its end. Empty = none outstanding.
  QString m_goto_guid;
  // Build + persist + activate a 2-point GOTO route from the own-ship fix to
  // (lat, lon). Shared by navigateToHere / navigateToWaypoint.
  void startGotoRoute(double lat, double lon, const QString& name);

  // Measure tool (P3.18). Points are (lon, lat); the layer renders them, the
  // text summarises the rubber-band leg + running total.
  bool m_measure_active = false;
  QList<QPointF> m_measure_pts;
  QString m_measure_text;
  MeasureLayer* m_measure_layer = nullptr;  // owned by the compositor
  void updateMeasure(double cur_lat, double cur_lon, bool has_cursor);

  // MMSI-properties persistence (P3.6): ConfigStore <-> g_MMSI_Props_Array.
  void loadMmsiProperties();
  void persistMmsiProperties() const;

  // Undo machinery (P3.18 tier 4): one op per user mark action. Recreating
  // a mark assigns a fresh GUID, so each apply records the live guid.
  struct UndoOp {
    bool created = false;  // true: op was "mark created" (undo deletes it)
    QString guid;          // the mark's CURRENT guid
    QVariantMap snap;      // name/comment/icon/lat/lon for recreation
  };
  QList<UndoOp> m_undo_stack;
  QList<UndoOp> m_redo_stack;
  static constexpr int kMaxUndo = 32;
  void pushUndo(const UndoOp& op);            // clears the redo stack
  QVariantMap snapshotMark(const QString& guid) const;
  QString recreateMark(const QVariantMap& snap);

  // Hit-test a click against the visible free marks; fills guid/name of the
  // nearest within a small radius (P3.18).
  bool hitWaypointAt(const QPointF& sp, QString* guid, QString* name) const;
  // Hit-test a click against the live AIS targets without selecting; fills
  // mmsi/name of the nearest within a small radius (P3.18 tier 2).
  bool hitAisAt(const QPointF& sp, int* mmsi, QString* name) const;
  // Hit-test a click against the visible tracks' polylines (P3.18 tier 3).
  bool hitTrackAt(const QPointF& sp, QString* guid, QString* name) const;
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
