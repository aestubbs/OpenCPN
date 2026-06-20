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
 * Implement chart_canvas.h.
 *
 * Owns the Viewport and the LayerCompositor. The compositor is fed a single
 * ChartLayer wrapping a RasterChartProvider (test chart for the prototype).
 * Mouse drag pans the Viewport; mouse wheel zooms about the cursor.
 *
 * Each frame, the WorldAnchored root's matrix is set from
 * Viewport::transformMatrix(width, height) so all world-anchored Layers
 * (chart, AIS, routes, ...) get the chart pan/zoom for free.
 */

#include "chart_canvas.h"

#include <QPointer>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#if defined(__GLIBC__)
#include <malloc.h>  // malloc_trim -- return freed cell heap to the OS on evict
#endif

#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QVarLengthArray>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGTransformNode>
#include <QThread>
#include <QFile>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWheelEvent>

#include "ais_layer.h"
#include "chart_boundary_provider.h"
#include "chart_config.h"
#include "chart_layer.h"
#include "chart_worker.h"
#include "config_store.h"
#include "demo_nav_data_provider.h"
#include "pick_highlight_provider.h"
#include "shapefile_basemap_provider.h"
#include "own_ship_config.h"
#include "ais_config.h"
#include "route_defaults_config.h"
#include "own_ship_layer.h"
#include "anchor_watch_layer.h"
#include "ui_config.h"
#include "route_overlay_layers.h"
#include "tide_layer.h"
#include "tcmgr.h"            // ptcmgr (libs/tides) -- tide-station hit-testing
#include "idx_entry.h"
#include "display_config.h"   // showTides gate for the tide pick
#include "layer_compositor.h"
#include "model/ocpn_config.h"
#include "model/georef.h"  // DistanceBearingMercator -- cursor brg/rng
#include "model/track.h"  // g_pActiveTrack -- own-ship track recording
#include "model/routeman.h"  // pWayPointMan -- waypoint icon catalogue
#include "display_config.h"
#include "model_nav_data_provider.h"
#include "nav_state_view_model.h"
#include "ocharts_service.h"
#include "layer.h"
#include "object_query_view_model.h"
#include "raster_chart_provider.h"
#include "route_list_view_model.h"
#include "cm93_scanner.h"
#include "grid_layer.h"
#include "raster_chart_layer.h"
#include "time_controller.h"
#include "waypoint_icons.h"
#include "s57_dictionary.h"
#include "model/ais_decoder.h"   // g_MMSI_Props_Array (MMSI properties, P3.6)
#include "model/ais_defs.h"      // TRACKTYPE_*
#include "model/wx_qt_string.h"  // wxString <-> QString (MmsiProperties)
#include "switchable_nav_provider.h"
#include "s52_engine.h"
#include "s52_vector_chart_provider.h"
#include "test_chart.h"
#include "viewport.h"

#ifndef OCPN_QT_BASEMAP_SHP
#define OCPN_QT_BASEMAP_SHP ""
#endif

// AIS model-decoder globals (model/ais_state_vars.h). Declared here rather than
// included because that header also declares wxString globals, which a Qt TU
// can't pull in; we touch only these bool/double prune thresholds.
extern bool g_bMarkLost;
extern double g_MarkLost_Mins;
extern bool g_bRemoveLost;
extern double g_RemoveLost_Mins;
extern double g_ShowMoored_Kts;

namespace ocpn::qtui {

namespace {
// Bounding box for the test chart -- a chunk of the North Sea.
constexpr double kTestNorth = 55.0;
constexpr double kTestSouth = 50.0;
constexpr double kTestWest = 0.0;
constexpr double kTestEast = 10.0;

// Push the AIS-target preferences the reused MODEL decoder (g_pAIS) honours
// into its global state. Most AIS display settings are consumed directly by the
// Qt layers (AisLayer reads show-names / predictor length; ais_cpa reads the
// CPA thresholds), but lost/remove-target pruning and the moored-speed
// classification live in the model decoder (ais_decoder.cpp), so the user's
// timeouts must reach its globals or they have no effect.
void syncAisModelGlobals() {
  const AisConfig& a = AisConfig::instance();
  g_bMarkLost = a.markLostMin() > 0.0;
  g_MarkLost_Mins = a.markLostMin();
  g_bRemoveLost = a.removeLostMin() > 0.0;
  g_RemoveLost_Mins = a.removeLostMin();
  g_ShowMoored_Kts = a.suppressAnchoredSpeedMax();
}
}  // namespace

ChartCanvas::ChartCanvas(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
  setAcceptHoverEvents(true);  // track cursor lat/lon for the status bar

  m_viewport = std::make_unique<Viewport>();
  // Centre on the test chart, with the scale ChartCanvas's QML host fits
  // to. (User can zoom from here.)
  m_viewport->setCenter((kTestNorth + kTestSouth) / 2.0,
                        (kTestWest + kTestEast) / 2.0);

  // wx parity: reopen on the same view (position + zoom) the app was
  // closed at. Saved DEBOUNCED on every viewport change rather than at
  // exit, so it survives a crash or kill; restored where the initial
  // chart fit would otherwise run (see m_world_fitted).
  {
    auto* save = new QTimer(this);
    save->setSingleShot(true);
    save->setInterval(2000);
    connect(m_viewport.get(), &Viewport::changed, save,
            QOverload<>::of(&QTimer::start));
    connect(save, &QTimer::timeout, this, [this] {
      // Only the visible canvas persists -- the hidden split-view pane
      // (P6.1) has its own viewport and must not overwrite the user's
      // view. (Both panes visible: last change wins, like wx's primary.)
      if (!isVisible()) return;
      ConfigStore& c = ConfigStore::instance();
      c.setDouble("view/lat", m_viewport->centerLat());
      c.setDouble("view/lon", m_viewport->centerLon());
      c.setDouble("view/scale", m_viewport->scale());
    });
  }

  m_compositor = std::make_unique<LayerCompositor>();

  // Per-Layer state store (visible/zOrder/opacity), an INI file under the
  // app config dir. Hand it to the compositor before any layer is added so
  // persistable layers restore their saved state on registration (P2.10).
  const QString cfg_dir =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if (!cfg_dir.isEmpty()) QDir().mkpath(cfg_dir);
  m_layer_config = std::make_unique<OcpnConfig>(cfg_dir + "/layers.ini");
  m_compositor->setConfig(m_layer_config.get());

  // Restore persisted chart-display preferences (P3.6) before the providers
  // are created, so the initial render reflects them.
  m_display_category =
      m_layer_config->value("display/category", m_display_category).toInt();
  // Mariner's Standard per-class filter (P3.6): the hidden-class set.
  m_hidden_classes = m_layer_config->value("display/hiddenClasses")
                         .toString()
                         .split(',', Qt::SkipEmptyParts);
  m_show_enc_anchoring =
      m_layer_config->value("display/showEncAnchoring", true).toBool();
  // Per-MMSI AIS handling (P3.6): populate the decoder's properties array
  // from the persisted set before any AIS traffic arrives.
  loadMmsiProperties();
  m_show_soundings =
      m_layer_config->value("display/soundings", m_show_soundings).toBool();
  // P2.16: text / lights / buoys symbol visibility is no longer a per-provider
  // live toggle (those were Qt-invented and duplicated the wx S-52 text flags
  // + Display Category). They stay true here; cartography text is governed by
  // the s52plib flags (ChartConfig -> applyDisplaySettings) and symbol
  // visibility by the Display Category, matching wx. The stale display/{text,
  // lights,buoys} keys are intentionally no longer loaded.

  // World background -- OpenCPN's shapefile basemap, always present under
  // everything (lowest z) so the canvas shows a land/sea world map at any
  // zoom. ENC cells and overlays composite on top.
  m_basemap =
      new ShapefileBasemapProvider(QString::fromUtf8(OCPN_QT_BASEMAP_SHP));
  auto* world = m_basemap;
  m_basemap->setViewport(m_viewport.get());  // PERF-6: visible-tile submit
  m_basemap->setNoDataMode(DisplayConfig::instance().showNoData());  // B (ECDIS)
  auto* world_layer = new ChartLayer(world, m_viewport.get());
  world_layer->setZOrder(-1000);
  m_compositor->addLayer(world_layer);

  // Nav overlays (P2.11): AIS targets + own ship + routes/tracks/waypoints,
  // world-anchored, on top of the charts. The overlay Layers bind to ONE
  // switchable provider so demo<->live is a runtime flag, not a layer
  // rebuild. Each is a stable-id Layer, so it also exercises P2.10.
  //
  // Live source mirrors the wx app: the comm/decoder pipeline feeds the model
  // and the canvas reads it. The nav-core singletons (g_pAIS, route/waypoint
  // managers, own-ship track, navobj DB) are brought up by initNavCore() in
  // main() before this canvas exists. A recorded NMEA log feeds the real
  // AisDecoder via the worker thread; swapping in a real CommDriver later
  // changes only the source.
  // Demo source = the standard OpenCPN Hakefjord NMEA-log replay (decodes to
  // its own store, independent of the live model). Live source = the real
  // model fed by user connections (Options > Connections -> MakeCommDriver);
  // it polls g_pAIS + the own-ship globals when active. No auto-connect and
  // no log in live mode.
  m_demo_provider = std::make_unique<ModelNavDataProvider>(
      QString::fromUtf8(OCPN_QT_NMEA_LOG), QString(), 0, /*persist_ais=*/false);
  // Live source persists AIS to SQLite so targets restore on restart (#40).
  m_model_provider = std::make_unique<ModelNavDataProvider>(
      QString(), QString(), 0, /*persist_ais=*/true);
  m_nav_provider = std::make_unique<SwitchableNavDataProvider>(
      m_demo_provider.get(), m_model_provider.get());
  // View-model over the active provider for the QML HUD (P3.2/P3.4).
  m_nav_state = std::make_unique<NavStateViewModel>(m_nav_provider.get());
  // Selected-AIS-target model for the info popup (P3.9).
  m_ais_selection = std::make_unique<AisSelectionViewModel>();
  m_tide_graph = std::make_unique<TideGraphViewModel>();
  // S-57 object-query result model (P3.9).
  m_object_query = std::make_unique<ObjectQueryViewModel>();
  // Navigation alert engine (AIS CPA/TCPA danger) (P3.15).
  m_alert_engine = std::make_unique<AlertEngine>();
  // Route/waypoint list model for the manager (P3.7).
  m_route_list = std::make_unique<RouteListViewModel>(m_nav_provider.get());

  // Active-route follower (P3.16): runs the model nav engine each fix and
  // publishes the solution to QML. Test ship: a synthetic GPS for simulating
  // a voyage so following can be exercised without a live feed.
  m_route_follower = std::make_unique<RouteFollower>();
  m_sim_ship = std::make_unique<SimShipController>();
  // Send-to-Peer (P3.18 tier 4): discovery + transfer controller.
  m_peer_send = std::make_unique<PeerSendController>();
  // Send-to-GPS (P3.18 tier 4): serial route/mark upload controller.
  m_gps_upload = std::make_unique<GpsUploadController>();
  // Re-run the follow solution on every own-ship tick (cheap no-op when no
  // route is active).
  connect(m_nav_provider.get(), &NavDataProvider::dynamicChanged, this,
          [this]() {
            if (m_route_follower) m_route_follower->update();
          });
  // Activating / deactivating a route changes which route draws as "active";
  // repaint so the accent appears at once rather than on the next tick.
  connect(m_route_follower.get(), &RouteFollower::activeRouteChanged, this,
          [this]() { update(); });
  // Surface waypoint-arrival / route-end through the alert banner (P3.16).
  connect(m_route_follower.get(), &RouteFollower::arrived, this,
          [this](const QString& wp) {
            if (m_alert_engine)
              m_alert_engine->noteRouteEvent(tr("Arrived: %1").arg(wp));
          });
  connect(m_route_follower.get(), &RouteFollower::ended, this,
          [this](const QString& route) {
            if (m_alert_engine)
              m_alert_engine->noteRouteEvent(tr("Route complete: %1").arg(route));
          });
  // A "Navigate to here" GOTO route is temporary (wx m_bDeleteOnArrival):
  // delete it once the follower reports arrival at its end (P3.18).
  connect(m_route_follower.get(), &RouteFollower::ended, this, [this]() {
    if (m_goto_guid.isEmpty() || !m_nav_provider) return;
    const QList<NavRoute> routes = m_nav_provider->userRoutes();
    for (int i = 0; i < routes.size(); ++i) {
      if (routes[i].guid == m_goto_guid) {
        m_nav_provider->deleteRoute(i);
        break;
      }
    }
    m_goto_guid.clear();
  });

  // OCPN_QT_PAN_TEST=<seconds>: drive a continuous synthetic pan on the
  // visible canvas -- the deterministic exerciser behind the PERF-0
  // measurements (pair with QSG_RENDER_TIMING=1). Sweeps east/west,
  // flipping every 3 s.
  if (qEnvironmentVariableIsSet("OCPN_QT_PAN_TEST")) {
    const int secs = qMax(1, qEnvironmentVariable("OCPN_QT_PAN_TEST").toInt());
    auto* pan = new QTimer(this);
    pan->setInterval(16);
    auto t0 = std::make_shared<qint64>(0);
    connect(pan, &QTimer::timeout, this, [this, pan, t0, secs] {
      if (!isVisible()) return;
      if (*t0 == 0) *t0 = QDateTime::currentMSecsSinceEpoch();
      const qint64 el = QDateTime::currentMSecsSinceEpoch() - *t0;
      if (el > secs * 1000) {
        qWarning("pan-test: DONE (%d s)", secs);
        pan->stop();
        pan->deleteLater();
        return;
      }
      const double dir = ((el / 3000) % 2) ? 1.0 : -1.0;
      m_viewport->panBy(dir * 6.0, 0.0);
      update();
    });
    pan->start();
  }

  // Demo (Hakefjord replay) is opt-in: read the persisted choice (default
  // off). The app otherwise boots into the live setup.
  m_demo_mode = ConfigStore::instance().getBool("display/demoMode", false);

  // Data-source connections (#34): enabling one creates a CommDriver that
  // feeds the model. The live/demo mode is governed solely by m_demo_mode
  // (the explicit toggle), so activating a connection never flips demo --
  // it just makes sure the model is being polled when we're live.
  m_connections = std::make_unique<ConnectionsViewModel>();
  connect(m_connections.get(), &ConnectionsViewModel::activated, this,
          [this]() {
            if (!m_demo_mode && m_model_provider)
              m_model_provider->setModelPolling(true);
          });
  // Auto-start the configured setup: re-open any connections enabled last
  // session (auto-reconnect) so live data flows on launch.
  m_connections->activatePersisted();

  // Chart directories (Options > Charts > Chart Files): a change or an
  // explicit rescan re-runs the catalog scan over the configured set.
  m_chart_source = std::make_unique<ChartSourceModel>();
  connect(m_chart_source.get(), &ChartSourceModel::rescanRequested, this,
          [this]() { reloadCharts(); });

  // Decoded-message stream for the Data Monitor (taps all comm messages).
  m_nmea_monitor = std::make_unique<NmeaMonitorModel>();
  // HUD bus stats: depth (DPT) + water temp (MTW) off the decoded stream.
  connect(m_nmea_monitor.get(), &NmeaMonitorModel::lineReceived, this,
          [this](const QString& line, const QString&) {
            static const QRegularExpression dpt(
                QStringLiteral("[A-Z]{2}DPT,([0-9.+-]+)"));
            static const QRegularExpression mtw(
                QStringLiteral("[A-Z]{2}MTW,([0-9.+-]+)"));
            bool changed = false;
            if (const auto m = dpt.match(line); m.hasMatch()) {
              m_depth_text = m.captured(1) + QStringLiteral(" m");
              changed = true;
            }
            if (const auto m = mtw.match(line); m.hasMatch()) {
              m_wtemp_text = m.captured(1) + QStringLiteral(" °C");
              changed = true;
            }
            if (changed) emit busStatsChanged();
          });

  // Edge auto-pan tick (P3.13, wx pPanTimer 200 ms / 2%-per-tick).
  m_edge_pan_timer = new QTimer(this);
  m_edge_pan_timer->setInterval(200);
  connect(m_edge_pan_timer, &QTimer::timeout, this, [this]() {
    if (!m_viewport ||
        (!m_route_build_mode && !m_measure_active && !m_dragging_node)) {
      m_edge_pan_timer->stop();
      return;
    }
    m_viewport->panBy(m_edge_pan_step.x(), m_edge_pan_step.y());
  });

  // Restore the persisted colour scheme (#35).
  setColorScheme(ConfigStore::instance().getInt("display/colorScheme", 0));
  // Force the route-defaults singleton up now so it seeds the model globals
  // (arrival-circle radius, persist-active-route, track precision/highlight,
  // default icons) before any route is followed -- it's otherwise a lazy QML
  // singleton (P3.16).
  RouteDefaultsConfig::instance();
  // Restore the route that was active last session, if "Persist active route"
  // is on (routes are already loaded by initNavCore()).
  if (m_route_follower) m_route_follower->restorePersisted();
  // Restore the persisted detail scale (default min display 1:N for un-SCAMIN'd
  // objects). Set the member directly; providers are seeded via
  // applyDisplaySettings as they load.
  m_detail_scale = ConfigStore::instance().getInt("display/detailScale", 100000);
  m_overzoom_k = ConfigStore::instance().getDouble("display/overzoomFactor", 2.0);
  if (m_overzoom_k < 1.0 || m_overzoom_k > 5.0) m_overzoom_k = 2.0;

  m_ais_layer = new AisLayer(m_nav_provider.get(), m_viewport.get());
  m_ais_layer->setZOrder(2000);
  m_compositor->addLayer(m_ais_layer);
  m_own_ship_layer = new OwnShipLayer(m_nav_provider.get(), m_viewport.get());
  m_own_ship_layer->setZOrder(2001);
  m_compositor->addLayer(m_own_ship_layer);

  // Anchor-watch circle (P3.15) -- world-anchored, driven by the AlertEngine.
  auto* anchor_watch = new AnchorWatchLayer(m_alert_engine.get());
  anchor_watch->setZOrder(1900);
  m_compositor->addLayer(anchor_watch);

  // Static nav overlays: tracks (under), routes, then waypoints on top.
  m_track_layer = new TrackLayer(m_nav_provider.get(), m_viewport.get());
  m_track_layer->setZOrder(1500);
  m_compositor->addLayer(m_track_layer);
  m_route_layer = new RouteLayer(m_nav_provider.get(), m_viewport.get());
  m_route_layer->setZOrder(1600);
  m_compositor->addLayer(m_route_layer);
  // The route layer is always enabled; which routes draw is governed per-route
  // by the eye toggle + selection (P3.7), so there's no master Routes switch to
  // strand it off. Force visible in case an old config persisted it hidden.
  m_route_layer->setVisible(true);
  // Active-route accent (P3.16): highlighted active leg + ship-to-active line,
  // above the plain route line, below the waypoints. Rebuilds per tick.
  m_route_follow_layer =
      new RouteFollowLayer(m_nav_provider.get(), m_viewport.get());
  m_route_follow_layer->setZOrder(1650);
  m_compositor->addLayer(m_route_follow_layer);
  m_route_follow_layer->setVisible(true);
  m_waypoint_layer = new WaypointLayer(m_nav_provider.get(), m_viewport.get());
  m_waypoint_layer->setZOrder(1700);
  m_compositor->addLayer(m_waypoint_layer);
  // Qt plugin host (P4.2): load OcpnQtPlugin modules from the app data
  // plugins dir; registered Layers join this compositor like built-ins.
  m_plugin_registry = std::make_unique<PluginRegistry>(
      [this](Layer* l) {
        if (l && m_compositor) m_compositor->addLayer(l);
      },
      m_nav_provider.get(),
      m_nmea_monitor.get(),  // navMsgTap: lineReceived(line, source)
      [this](const QString& dir) {
        if (m_chart_source) {
          m_chart_source->addDirectory(dir);
          reloadCharts();
        }
      },
      &OChartsService::instance(),  // shop plugin: fpr + daemon status
      m_viewport.get(),              // plugin layers: scale/screen sizing
      &TimeController::instance());  // time-aware plugins follow the bar

  // Measure-tool overlay (P3.18): above the nav overlays, below AIS.
  m_measure_layer = new MeasureLayer(m_nav_provider.get(), m_viewport.get());
  m_measure_layer->setZOrder(1800);
  m_compositor->addLayer(m_measure_layer);
  m_measure_layer->setVisible(true);
  // Lat/lon graticule (P2.20, wx "Show Grid"): above the charts, below the
  // nav overlays. Gated internally by DisplayConfig::showGrid.
  auto* grid = new GridLayer(m_nav_provider.get(), m_viewport.get());
  grid->setZOrder(1400);
  m_compositor->addLayer(grid);
  grid->setVisible(true);

  // Re-render the route/track overlays when the route/track style settings
  // change (Options > Routes: colour, line style, track colour) -- the layers
  // read RouteDefaultsConfig at draw time, so a config change just needs a
  // rebuild.
  for (Layer* l : {static_cast<Layer*>(m_route_layer),
                   static_cast<Layer*>(m_route_follow_layer),
                   static_cast<Layer*>(m_track_layer)})
    connect(&RouteDefaultsConfig::instance(), &RouteDefaultsConfig::changed, l,
            &Layer::dirty);

  // Tide/current stations (P3.14 D) -- world-anchored, queried from the engine
  // (ptcmgr) at the timeline's display time; hidden unless Show Tides is on.
  m_tide_layer = new TideLayer(m_viewport.get());
  m_tide_layer->setZOrder(1750);
  m_compositor->addLayer(m_tide_layer);

  // Object-query pick highlight (F): outlines the feature currently shown in
  // the query popup. Above charts/nav overlays so it is never hidden; driven
  // by the query view-model below. Owned by its ChartLayer in the compositor.
  m_pick_highlight = new PickHighlightProvider(QStringLiteral("pick.highlight"),
                                               m_viewport.get());
  auto* pick_layer = new ChartLayer(m_pick_highlight, m_viewport.get());
  pick_layer->setZOrder(2100);
  m_compositor->addLayer(pick_layer);
  // Refresh the highlight whenever the query result or the stepped index
  // changes (so prev/next moves the outline to the newly-shown feature).
  connect(m_object_query.get(), &ObjectQueryViewModel::changed, this, [this]() {
    if (m_pick_highlight)
      m_pick_highlight->setShape(m_object_query->currentShape(),
                                 m_object_query->currentGeom());
  });

  // Recentre on the active source's own-ship fix once after each mode switch
  // (the Hakefjord demo and a live feed are both at their real positions,
  // not on the loaded charts). m_live_centered is reset in setDemoMode.
  connect(m_nav_provider.get(), &NavDataProvider::dynamicChanged, this,
          [this]() {
            if (m_live_centered) return;
            const OwnShipState s = m_nav_provider->ownShip();
            if (s.valid) {
              m_viewport->setCenter(s.lat, s.lon);
              m_live_centered = true;
            }
          });

  // Start in the configured source (demo = Hakefjord replay by default).
  m_nav_provider->setLive(!m_demo_mode);
  m_demo_provider->setRunning(m_demo_mode);
  if (!m_demo_mode) m_model_provider->setModelPolling(true);

  // Repaint when:
  //   - any Layer dirties (data change, visibility/z-order/opacity).
  //   - the Viewport pans / zooms (transform matrix changes).
  //   - the canvas resizes (transform depends on width/height).
  // Custom value types crossing the worker-thread -> main-thread queued
  // signal boundary must be registered.
  qRegisterMetaType<ocpn::qtui::CellExtent>();
  qRegisterMetaType<QList<ocpn::qtui::CellExtent>>();
  qRegisterMetaType<s52sg::Buffer>();
  qRegisterMetaType<ocpn::qtui::ChartDisplaySettings>();

  // S-52 decode-time display settings (depth shading/contours, symbol/boundary
  // style, important-text, SCAMIN): pushed to the decode thread + a resident
  // re-decode whenever the Vector Chart Display options change. Debounced so a
  // burst of edits (e.g. typing a contour depth) coalesces into one re-decode.
  m_chart_cfg_debounce = new QTimer(this);
  m_chart_cfg_debounce->setSingleShot(true);
  m_chart_cfg_debounce->setInterval(350);
  connect(m_chart_cfg_debounce, &QTimer::timeout, this, [this]() {
    applyChartConfig();
    // The CM93 detail slider changes which tier is ELIGIBLE, not how cells
    // decode -- re-run the visible-cell selection too (cheap).
    if (!m_catalog.isEmpty()) updateVisibleCells();
  });
  connect(&ChartConfig::instance(), &ChartConfig::changed, this,
          [this]() { m_chart_cfg_debounce->start(); });

  // Pan/zoom fires Viewport::changed at mouse-move / wheel rate; coalesce a
  // burst into one visible-cell evaluation once the gesture settles.
  m_load_debounce = new QTimer(this);
  m_load_debounce->setSingleShot(true);
  m_load_debounce->setInterval(250);
  connect(m_load_debounce, &QTimer::timeout, this,
          [this]() { updateVisibleCells(); });

  // A burst of cellLoaded arrivals (panning a chart-dense region) used to run
  // the O(loaded^2) finest-owner re-derivation + a chart-bar rebuild on EVERY
  // arrival, so the cost of draining the backlog grew with the backlog and the
  // GUI thread never caught up. Coalesce them: each arrival just (re)starts this
  // short timer and the heavy pass runs once after the arrivals settle.
  m_finer_debounce = new QTimer(this);
  m_finer_debounce->setSingleShot(true);
  m_finer_debounce->setInterval(60);
  connect(m_finer_debounce, &QTimer::timeout, this, [this]() {
    updateFinerCoverage();
    emit chartCoverageChanged();
  });

  // Return freed cell heap to the OS a short while after eviction (see
  // m_trim_timer in the header). Delayed + single-shot so it runs once the
  // render thread has released the evicted subtrees and off the pan/zoom hot
  // path. glibc only; a no-op build elsewhere.
  m_trim_timer = new QTimer(this);
  m_trim_timer->setSingleShot(true);
  m_trim_timer->setInterval(400);
  connect(m_trim_timer, &QTimer::timeout, this, [this]() {
#if defined(__GLIBC__)
    const bool kInstr = qEnvironmentVariableIsSet("OCPN_INSTR");
    const auto rssKB = []() -> long {
      QFile s(QStringLiteral("/proc/self/status"));
      if (!s.open(QIODevice::ReadOnly)) return 0;
      const QByteArray b = s.readAll();
      const int i = b.indexOf("VmRSS:");
      return i < 0 ? 0 : b.mid(i + 6, 24).simplified().split(' ').first().toLong();
    };
    const long before = kInstr ? rssKB() : 0;
    malloc_trim(0);  // releases the top of every arena back to the kernel
    if (kInstr)
      qWarning("INSTR malloc_trim: RSS %ldMB -> %ldMB", before / 1024,
               rssKB() / 1024);
#endif
  });

  connect(m_compositor.get(), &LayerCompositor::changed, this,
          [this]() { update(); });
  connect(m_viewport.get(), &Viewport::changed, this, [this]() {
    update();
    emit viewChanged();  // refresh the MUIBar scale readout
    // Only chase visible cells once a catalog exists (async ENC path).
    if (!m_catalog.isEmpty()) m_load_debounce->start();
  });
  // Follow mode: recentre on the own-ship fix as it updates. Also feed the
  // active track recorder (#29) -- append each fresh own-ship fix.
  connect(m_nav_provider.get(), &NavDataProvider::dynamicChanged, this,
          [this]() {
            // CPA-enriched target snapshot, shared by the popup + alert engine.
            const QList<AisTarget> targets = m_nav_provider->aisTargets();
            // Keep the open AIS info popup's CPA/TCPA live as the fix updates.
            if (m_ais_selection && m_ais_selection->valid())
              m_ais_selection->refresh(targets);
            // Raise / clear AIS CPA-TCPA danger + anchor-watch alerts (P3.15).
            const OwnShipState s = m_nav_provider->ownShip();
            if (m_alert_engine) {
              m_alert_engine->evaluateAis(targets);
              m_alert_engine->evaluateAnchor(s);
            }
            // Course-Up / Head-Up track the live COG/HDT.
            updateChartRotation();
            // Track recording is handled by the model ActiveTrack itself
            // (its own timer off the own-ship fix); here we only follow.
            if (!m_follow_own_ship) return;
            if (!s.valid) return;
            double clat = s.lat, clon = s.lon;
            // Look-ahead: shift the view ahead along the course so more water
            // is shown ahead of the boat (wx m_bLookAhead; >=2 kn, ~1/4 view).
            if (DisplayConfig::instance().lookAhead() && s.sog >= 2.0) {
              const double course =
                  (DisplayConfig::instance().navMode() == 2 && s.hdg < 360.0)
                      ? s.hdg : s.cog;
              const double chh = height() > 0 ? height() : 720.0;
              const double dist_deg = (chh * 0.25) / m_viewport->scale();
              const double r = course * M_PI / 180.0;
              const double coslat =
                  std::max(0.2, std::cos(s.lat * M_PI / 180.0));
              clat += dist_deg * std::cos(r);
              clon += dist_deg * std::sin(r) / coslat;
            }
            m_viewport->setCenter(clat, clon);
          });
  // Recompute the chart rotation when the orientation mode / averaging changes,
  // and push the (possibly changed) depth unit to resident chart providers so
  // soundings re-raster in the new unit. setDepthUnit short-circuits when the
  // unit is unchanged, so unrelated DisplayConfig edits cost nothing.
  connect(&DisplayConfig::instance(), &DisplayConfig::changed, this, [this]() {
    updateChartRotation();
    // Depth + height units both bake into the decode: height text + light
    // descriptions and the SNDFRM soundings on wrecks/rocks are unit-formatted
    // by s52plib at decode, and the reload re-creates providers with the new
    // depth unit (applyDisplaySettings) so SOUNDG figures re-format too. So a
    // change to either re-decodes the resident cells.
    if (DisplayConfig::instance().depthUnit() != m_depth_unit ||
        DisplayConfig::instance().heightUnit() != m_height_unit)
      applyChartConfig();
    // ECDIS NODATA fill (B): re-tint the world backdrop grey / restore it.
    if (m_basemap)
      m_basemap->setNoDataMode(DisplayConfig::instance().showNoData());
  });
  // Vessel safety depth -> ENC sounding bold threshold (render-time re-raster).
  connect(&OwnShipConfig::instance(), &OwnShipConfig::changed, this, [this]() {
    const double sd = OwnShipConfig::instance().safetyDepth();
    for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
      if (it.value().provider) it.value().provider->setSafetyDepth(sd);
  });
  // ENC sounding-size slider -> figure scale (render-time re-raster). The
  // -5..+5 slider maps to 0.5x..1.5x, mirroring wx m_SoundingsScaleFactor.
  connect(&UIConfig::instance(), &UIConfig::changed, this, [this]() {
    const double sc = 1.0 + 0.1 * UIConfig::instance().encSoundingScaleFactor();
    for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
      if (it.value().provider) it.value().provider->setSoundingScale(sc);
  });
  // AIS lost/remove-target timeouts + moored-speed -> the model decoder globals
  // it prunes by; applied now and whenever the AIS preferences change.
  syncAisModelGlobals();
  connect(&AisConfig::instance(), &AisConfig::changed, this,
          []() { syncAisModelGlobals(); });
  // On resize, repaint AND re-evaluate visible cells: the initial fit +
  // selection can run before the canvas has its real size (the catalog scan
  // starts at launch), sampling a too-small view rect and missing cells in
  // the part of the real window beyond it. Re-running on the real size fills
  // them in without waiting for a user pan/zoom.
  auto onResize = [this]() {
    // Keep the viewport's canvas size current so layers can compute the visible
    // world rect for view-frustum culling (Viewport::visibleWorldBounds).
    if (m_viewport)
      m_viewport->setCanvasSize(static_cast<int>(width()),
                                static_cast<int>(height()));
    update();
    if (!m_catalog.isEmpty()) m_load_debounce->start();
  };
  connect(this, &QQuickItem::widthChanged, this, onResize);
  connect(this, &QQuickItem::heightChanged, this, onResize);
}

ChartCanvas::~ChartCanvas() {
  // Persist per-Layer state while the compositor (and its config) are still
  // alive (P2.10).
  if (m_compositor) m_compositor->saveState();
  // Drop our cancellation entry from a shared worker (split view) we don't own;
  // the owner tears its worker down entirely just below.
  if (m_worker && !m_worker_thread) m_worker->forgetWanted(this);
  // Stop the worker thread before the QObject teardown chain runs.
  if (m_worker_thread) {
    m_worker_thread->quit();
    m_worker_thread->wait();
  }
}

#ifndef OCPN_QT_TEST_ENC
#define OCPN_QT_TEST_ENC ""
#endif
#ifndef OCPN_QT_S57DATA_DIR
#define OCPN_QT_S57DATA_DIR ""
#endif

void ChartCanvas::setS52Engine(S52Engine* engine) {
  if (m_s52_engine == engine) return;
  m_s52_engine = engine;
  emit s52EngineChanged();
  if (!m_s52_engine || !m_s52_engine->isOk()) return;

  // Plugins load once the canvas has a real engine: the hidden split
  // pane (null engine while split view is off) must NOT duplicate every
  // plugin instance.
  if (m_plugin_registry)
    m_plugin_registry->loadFrom(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/plugins-qt"));

  m_s57data_dir = QString::fromUtf8(OCPN_QT_S57DATA_DIR);

  // First run: migrate the build-time OCPN_QT_TEST_ENC path into the runtime
  // chart-directory list, so existing dev setups keep working. After that the
  // list is user-managed via Options > Charts > Chart Files.
  if (m_chart_source) m_chart_source->seedIfEmpty(QString::fromUtf8(OCPN_QT_TEST_ENC));

  // If any chart directories are configured, scan them; per-cell content
  // streams in on demand as the user zooms/pans.
  if (m_chart_source && !m_chart_source->activeDirectories().isEmpty()) {
    reloadCharts();
    return;
  }

  // Demo fallback: synthetic chart, decoded inline (tiny, no thread).
  s52sg::Buffer buf = m_s52_engine->buildDemoChart(kTestNorth, kTestSouth,
                                                   kTestEast, kTestWest);
  if (!buf.empty()) {
    auto* provider = new S52VectorChartProvider(
        "demo.s52-chart", std::move(buf), kTestNorth, kTestSouth, kTestWest,
        kTestEast, m_viewport.get());
    applyDisplaySettings(provider);
    m_compositor->addLayer(new ChartLayer(provider, m_viewport.get()));
    LoadedCell lc;
    lc.layerId = "demo.s52-chart";
    lc.provider = provider;  // no extent -> never evicted (not in catalog)
    m_loaded.insert("demo", lc);
    update();
  }
}

// P6.1 shared decode: when split view runs two canvases, they share ONE
// worker (one engine, one decode thread). The first canvas creates and
// owns it; the second connects to the same instance and filters results
// by its own m_needed set (exactly how stale results are dropped today).
static QPointer<ChartWorker> s_shared_worker;

void ChartCanvas::startAsyncLoad(const QStringList& cell_paths,
                                 const QString& s57data) {
  const bool share = ConfigStore::instance().getBool("ui/splitView", false);
  if (share && s_shared_worker) {
    // Second pane: ride the existing worker; no thread of our own.
    m_worker = s_shared_worker;
    connect(m_worker, &ChartWorker::extentsScanned, this,
            &ChartCanvas::onExtentsScanned);
    connect(m_worker, &ChartWorker::cellLoaded, this,
            &ChartCanvas::onCellLoaded);
    connect(m_worker, &ChartWorker::cellUnavailable, this,
            &ChartCanvas::onCellUnavailable);
    connect(m_worker, &ChartWorker::rasterCellLoaded, this,
            &ChartCanvas::onRasterCellLoaded);
    // The owner's scan broadcast also reaches this canvas; a re-scan here
    // is a cheap no-op (worker guards re-entry; the catalog cache hits).
    QMetaObject::invokeMethod(m_worker, "scanExtents", Qt::QueuedConnection,
                              Q_ARG(QStringList, cell_paths));
    return;
  }

  m_worker_thread = new QThread(this);
  m_worker = new ChartWorker(m_s52_engine, s57data);  // no parent (moved)
  m_worker->moveToThread(m_worker_thread);
  connect(m_worker_thread, &QThread::finished, m_worker,
          &QObject::deleteLater);
  if (share) s_shared_worker = m_worker;

  // Results arrive on the main thread (queued; receiver lives here).
  connect(m_worker, &ChartWorker::extentsScanned, this,
          &ChartCanvas::onExtentsScanned);
  connect(m_worker, &ChartWorker::cellLoaded, this,
          &ChartCanvas::onCellLoaded);
  connect(m_worker, &ChartWorker::cellUnavailable, this,
          &ChartCanvas::onCellUnavailable);
  connect(m_worker, &ChartWorker::rasterCellLoaded, this,
          &ChartCanvas::onRasterCellLoaded);

  m_worker_thread->start();

  // Push the persisted S-52 display options + colour scheme to the decode
  // thread BEFORE any cell decodes, so the first render already reflects them.
  QMetaObject::invokeMethod(m_worker, "setColorScheme", Qt::QueuedConnection,
                            Q_ARG(int, m_color_scheme));
  {
    const ChartConfig& c = ChartConfig::instance();
    ChartDisplaySettings s;
    s.importantTextOnly = c.importantTextOnly();
    s.useScamin = c.reducedDetailSmallScale();
    s.symbolStyle = c.graphicsStyle();
    s.boundaryStyle = c.boundaryStyle();
    s.twoShades = c.colourCount();
    s.safetyContour = c.safetyContour();
    s.shallowContour = c.shallowContour();
    s.deepContour = c.deepContour();
    m_height_unit = DisplayConfig::instance().heightUnit();
    s.heightUnit = m_height_unit;
    m_depth_unit = DisplayConfig::instance().depthUnit();
    s.depthUnit = m_depth_unit;
    s.chartInfoObjects = c.chartInfoObjects();          // P2.16
    s.dataQuality = c.dataQuality();                    // CATZOC overlay
    s.buoyLightLabels = c.buoyLightLabels();            // P2.16
    s.lightDescriptions = c.lightDescriptions();        // P2.16
    s.extendedLightSectors = c.extendedLightSectors();  // P2.16
    s.nationalText = c.nationalText();                  // P2.16
    s.declutterText = c.declutterText();                // P2.16
    s.superScamin = c.superScamin();                    // P2.16
    QMetaObject::invokeMethod(m_worker, "applyDisplaySettings",
                              Qt::QueuedConnection,
                              Q_ARG(ocpn::qtui::ChartDisplaySettings, s));
  }

  // Kick off the catalog scan on the worker.
  QMetaObject::invokeMethod(m_worker, "scanExtents", Qt::QueuedConnection,
                            Q_ARG(QStringList, cell_paths));
}

void ChartCanvas::reloadCharts() {
  if (!m_s52_engine || !m_s52_engine->isOk() || !m_chart_source) return;
  // Enumerate chart cells under each configured directory: raw S-57 (.000),
  // plaintext SENC (.S57), and o-charts (.oesu/.oesenc, decrypted on load).
  static const QStringList kCellGlobs = {
      QStringLiteral("*.000"), QStringLiteral("*.oesu"),
      QStringLiteral("*.oesenc"), QStringLiteral("*.S57"),
      QStringLiteral("*.kap"), QStringLiteral("*.KAP")};
  QStringList cells;
  // Reset the key map once, then accumulate each o-charts dir's keyList below
  // (loadKeyList merges; a keyList-less dir must not wipe another dir's keys).
  OChartsService::instance().clearKeys();
  for (const QString& path : m_chart_source->activeDirectories()) {
    QFileInfo fi(path);
    if (fi.isDir()) {
      // A CM93 set root (P2.19): pass the DIRECTORY itself; the worker
      // expands it via the CM93 scanner (cells are not .000-globbable).
      if (Cm93Scanner::isCm93Root(path)) {
        cells << path;
        continue;
      }
      // o-charts dirs carry a keyList *.XML; load it so the worker can
      // decrypt the cells it finds here.
      OChartsService::instance().loadKeyList(path);
      QDirIterator it(path, kCellGlobs, QDir::Files,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) cells << it.next();
    } else if (fi.isFile()) {
      cells << path;
    }
  }
  cells.removeDuplicates();
  cells.sort();

  if (cells.isEmpty()) {
    m_chart_source->setStatus(tr("No ENC cells found"), false);
    return;
  }
  // If any o-charts cells are present, kick off the decryption daemon now on
  // the GUI thread (reliable QProcess spawn) so the worker finds it ready.
  for (const QString& c : cells) {
    if (c.endsWith(QStringLiteral(".oesu"), Qt::CaseInsensitive) ||
        c.endsWith(QStringLiteral(".oesenc"), Qt::CaseInsensitive)) {
      OChartsService::instance().prespawnDaemon();
      break;
    }
  }
  m_chart_source->setStatus(tr("Scanning %1 cells…").arg(cells.size()), true);
  if (!m_worker) {
    startAsyncLoad(cells, m_s57data_dir);
  } else {
    // Re-scan on the existing worker; onExtentsScanned rebuilds the catalog
    // and evicts cells that are no longer part of it.
    QMetaObject::invokeMethod(m_worker, "scanExtents", Qt::QueuedConnection,
                              Q_ARG(QStringList, cells));
  }
}

void ChartCanvas::onExtentsScanned(const QList<CellExtent>& cells) {
  m_catalog.clear();
  for (const CellExtent& c : cells) m_catalog.insert(c.name, c);
  // Rebuild the spatial index over the new catalog (it holds pointers into
  // m_catalog, so it must be rebuilt here, before any updateVisibleCells use).
  m_spatial_index.build(m_catalog);

  // Evict any resident cell that is no longer in the catalog (e.g. its
  // directory was removed). "demo" has no catalog entry and is never evicted.
  const QStringList loaded_names = m_loaded.keys();
  for (const QString& name : loaded_names) {
    if (name == QStringLiteral("demo.s52-chart")) continue;
    if (!m_catalog.contains(name)) {
      m_compositor->removeLayer(m_loaded.value(name).layerId);
      m_loaded.remove(name);
      m_requested.remove(name);
      m_known_empty.remove(name);  // gone from the catalog -- forget it
    }
  }
  if (m_chart_source)
    m_chart_source->setStatus(
        tr("%1 cells in %2 director%3")
            .arg(cells.size())
            .arg(m_chart_source->activeDirectories().size())
            .arg(m_chart_source->activeDirectories().size() == 1 ? "y" : "ies"),
        false);

  // Boundary overlay: created once, drawn on top so the cell grid stays
  // visible over loaded chart content.
  if (!m_boundary_provider) {
    m_boundary_provider = new ChartBoundaryProvider("enc.boundaries");
    auto* layer = new ChartLayer(m_boundary_provider, m_viewport.get());
    layer->setZOrder(100);
    m_compositor->addLayer(layer);
    // wx "Show chart outlines": the toggle hides/shows the cell grid.
    layer->setVisible(DisplayConfig::instance().showChartOutlines());
    connect(&DisplayConfig::instance(), &DisplayConfig::changed, layer,
            [layer]() {
              layer->setVisible(
                  DisplayConfig::instance().showChartOutlines());
            });
  }
  // Feed the outline overlay a scale-culled subset (not the whole catalog), so
  // the small-scale view isn't swamped by every river/harbour chart. Force a
  // rebuild (the catalog just changed), then let updateVisibleCells refresh it
  // as the zoom band changes.
  m_boundary_count = -1;
  updateBoundaryExtents();

  // Fit the viewport to the set once (on the first batch). Fit to the dense
  // cluster of content cells, excluding area outliers: a few overview/ocean
  // cells (e.g. US1PO02M Pacific, US1EEZ1M) cover enormous areas and would
  // blow the fit out to a near-global view where every cell is too fine to
  // show. We take the median content-cell area and ignore cells more than 8x
  // larger. The GSHHS backdrop is always present, so the user can zoom out
  // freely from here.
  QList<double> areas;
  for (const CellExtent& c : cells)
    if (c.navFeatures > 0 && c.valid())
      areas.append((c.north - c.south) * (c.east - c.west));
  std::sort(areas.begin(), areas.end());
  const double medianArea = areas.isEmpty() ? 0.0 : areas[areas.size() / 2];
  const double areaCap = medianArea * 8.0;
  double n = -90.0, s = 90.0, e = -180.0, w = 180.0;
  for (const CellExtent& c : cells) {
    if (c.navFeatures <= 0 || !c.valid()) continue;
    if (areaCap > 0.0 && (c.north - c.south) * (c.east - c.west) > areaCap)
      continue;  // area outlier (overview/ocean cell)
    if (c.north > n) n = c.north;
    if (c.south < s) s = c.south;
    if (c.east > e) e = c.east;
    if (c.west < w) w = c.west;
  }
  // Defer the one-time fit until the canvas has its real size, so the
  // initial scale (and the view rect the selection samples) is correct.
  if (!m_world_fitted && width() > 0 && height() > 0 && e > w && n > s) {
    m_world_fitted = true;
    // DEBUG: open at a fixed view (OCPN_QT_VIEW_LAT/LON/SCALE) to reproduce a
    // specific location/zoom for diagnosis without manual navigation.
    const QByteArray dvlat = qgetenv("OCPN_QT_VIEW_LAT");
    const double saved_scale =
        ConfigStore::instance().getDouble("view/scale", 0.0);
    if (!dvlat.isEmpty()) {
      m_viewport->setCenter(dvlat.toDouble(),
                            qgetenv("OCPN_QT_VIEW_LON").toDouble());
      m_viewport->setScale(qgetenv("OCPN_QT_VIEW_SCALE").toDouble());
    } else if (saved_scale > 0.0) {
      // wx parity: reopen exactly where the app was closed (the saved
      // view wins over the whole-library fit), and don't yank to the
      // first GPS fix -- follow mode does that when asked.
      m_viewport->setCenter(ConfigStore::instance().getDouble("view/lat", 0.0),
                            ConfigStore::instance().getDouble("view/lon", 0.0));
      m_viewport->setScale(saved_scale);
      m_live_centered = true;
    } else {
      m_viewport->setCenter((n + s) / 2.0, (e + w) / 2.0);
      const double fit =
          std::min(width() / (e - w), height() / (n - s)) * 0.9;
      m_viewport->setScale(fit);
    }
  }
  update();

  // Pull in whatever's already big enough on screen -- but DEBOUNCED. The
  // worker re-emits the whole growing catalog every kBatch=25 cells, so a big
  // library delivers hundreds of extentsScanned batches; running the full
  // (576-grid-point x N-cell point-in-polygon) updateVisibleCells on every one
  // pegs the main thread and lets heavy cellLoaded events pile up unbounded ->
  // OOM on a Pi. Coalesce the batch storm into a single evaluation once the
  // catalog settles (the same 250ms debounce a pan/zoom burst uses).
  if (m_load_debounce)
    m_load_debounce->start();
  else
    updateVisibleCells();
}

void ChartCanvas::onRasterCellLoaded(const QString& id, const QImage& image,
                                     double north, double south, double east,
                                     double west, double worldYTop,
                                     double worldYBottom) {
  CellExtent c;
  c.north = north; c.south = south; c.east = east; c.west = west;
  c.name = id;
  CellExtent cat = m_catalog.value(id, c);
  if (m_loaded.contains(id) || image.isNull() || !m_needed.contains(id)) {
    m_requested.remove(id);
    return;
  }
  const QString layerId = "raster." + id;
  const QRectF rect(QPointF(west, qMin(worldYTop, worldYBottom)),
                    QPointF(east, qMax(worldYTop, worldYBottom)));
  auto* layer = new RasterChartLayer(layerId, image, rect);
  // Rasters underlay vector charts of the same tier (wx quilt behaviour).
  layer->setZOrder(zOrderForScale(cat.nativeScale) - 1);
  m_compositor->addLayer(layer);
  LoadedCell lc;
  lc.extent = cat;
  lc.layerId = layerId;
  lc.provider = nullptr;  // image layer -- no S-52 provider
  m_loaded.insert(id, lc);
  ++m_dbg_adds;
  qWarning("onRasterCellLoaded: ADD %s scale=%d %dx%d", qPrintable(id),
           cat.nativeScale, image.width(), image.height());
  m_finer_debounce->start();  // coalesce coverage re-derive + chart-bar refresh
  update();
}

void ChartCanvas::onCellLoaded(const QString& id, const s52sg::Buffer& buffer,
                               double north, double south, double east,
                               double west) {
  // A quick pan/zoom while this cell was decoding may have moved it out of
  // view; drop the result rather than add an off-screen layer.
  CellExtent c;
  c.north = north; c.south = south; c.east = east; c.west = west;
  c.name = id;
  // Use the catalog entry (it carries the native scale + accurate extent);
  // fall back to the loaded bounds if somehow absent.
  CellExtent cat = m_catalog.value(id, c);
  // Shared-worker mode (P6.1): the same cell may arrive twice (each pane
  // requests independently; broadcasts reach both). Already loaded -> done.
  if (m_loaded.contains(id)) {
    m_requested.remove(id);
    return;
  }
  // Dropped if the per-location selection moved off it while it was decoding.
  if (buffer.empty() || !m_needed.contains(id)) {
    qWarning("onCellLoaded: DROP %s (%s)", qPrintable(id),
             buffer.empty() ? "empty buffer"
                            : "no longer needed (panned off / out-competed)");
    m_requested.remove(id);  // allow a future re-request when needed again
    return;
  }

  // Hard OOM guard at the build point. These dense fine-scale cells build very
  // large scene graphs (hundreds of MB each) and arrive in async bursts faster
  // than the updateVisibleCells memory guard can run -- so check free RAM here
  // and SKIP the build when critically low (the basemap shows there; a later
  // pass retries once panning/eviction frees memory). Prevents the load-burst
  // OOM. Env: OCPN_QT_BUILD_FLOOR_MB.
  static const long kBuildFloorKB = [] {
    bool ok = false;
    const long v = qgetenv("OCPN_QT_BUILD_FLOOR_MB").toLong(&ok);
    return (ok ? v : 900) * 1024L;
  }();
  long memKB = 0;
  if (QFile mi(QStringLiteral("/proc/meminfo")); mi.open(QIODevice::ReadOnly)) {
    const QByteArray s = mi.readAll();
    const int i = s.indexOf("MemAvailable:");
    if (i >= 0)
      memKB = s.mid(i + 13, 24).simplified().split(' ').first().toLong();
  }
  if (memKB > 0 && memKB < kBuildFloorKB) {
    qWarning("onCellLoaded: SKIP %s -- free=%ldMB < %ldMB (OOM guard)",
             qPrintable(id), memKB / 1024, kBuildFloorKB / 1024);
    m_requested.remove(id);  // allow a retry once memory recovers
    return;
  }

  const QString layerId = "enc." + id;
  auto* provider = new S52VectorChartProvider(layerId, buffer, north, south,
                                              west, east, m_viewport.get());
  provider->setNativeScale(cat.nativeScale);  // drives the over-scale hatch (A)
  provider->setCoverage(cat.coverage);  // M_COVR render clip (P2.17)
  applyDisplaySettings(provider);
  auto* layer = new ChartLayer(provider, m_viewport.get());
  layer->setZOrder(zOrderForScale(cat.nativeScale));
  m_compositor->addLayer(layer);
  LoadedCell lc;
  lc.extent = cat;
  lc.layerId = layerId;
  lc.provider = provider;
  m_loaded.insert(id, lc);
  ++m_dbg_adds;
  qWarning("onCellLoaded: ADD %s scale=%d cov=%lld", qPrintable(id),
           cat.nativeScale, static_cast<long long>(cat.coverage.size()));
  // The new cell may own annotations in coarser cells (or be owned by finer
  // ones already loaded) -- re-derive every loaded cell's finer-coverage. A
  // burst of arrivals coalesces into ONE pass (see m_finer_debounce) instead of
  // an O(loaded^2) re-derive + chart-bar rebuild on every cell, which on a slow
  // core never drained while cells kept streaming in (the "never recovers"
  // stall). The cell is already in the scene; only the cross-cell duplicate
  // suppression waits the ~60ms settle.
  m_finer_debounce->start();
  update();
}

void ChartCanvas::onCellUnavailable(const QString& id, bool genuineEmpty) {
  // The worker decided this requested cell won't become a layer. Clear the
  // in-flight flag (it was leaking before -- a never-arriving cell stayed in
  // m_requested forever, since the evict loop only walks loaded cells).
  m_requested.remove(id);
  // A genuine empty decode: don't ask for it again under these settings, or the
  // 24x24 selection would re-request it every pan, pegging the decode thread.
  // A superseded/skipped request (genuineEmpty == false) stays eligible.
  if (genuineEmpty) m_known_empty.insert(id);
}

double ChartCanvas::displayScaleN(double scale, double centerLat) {
  // ~1:N display-scale denominator at a nominal 96 dpi (3.78 px/mm):
  //   N = (ground metres per degree of longitude) / (screen metres per pixel)
  // Under Mercator, scale is px per degree of LONGITUDE, whose ground distance
  // is 111320*cos(lat), so include the cos(centre_lat) factor to get the true
  // scale (else high-latitude charts read ~1/cos too coarse -> wrong quilt
  // tier + SCAMIN).
  if (scale <= 0.0) return 1.0e12;
  constexpr double kPpmm = 3.78;
  const double clat = std::max(0.05, std::cos(centerLat * M_PI / 180.0));
  return 111320.0 * clat * kPpmm * 1000.0 / scale;
}

int ChartCanvas::zOrderForScale(int native_scale) {
  // Finer (smaller 1:N) draws on top of coarser, so where they overlap the
  // coarse is hidden. Keep within [0, 90] -- under the boundary grid (100),
  // above the GSHHS world backdrop (-1000).
  if (native_scale <= 0) return 0;
  const double z = 90.0 - 10.0 * (std::log(static_cast<double>(native_scale)) -
                                  std::log(1000.0));
  return std::max(0, std::min(90, static_cast<int>(std::lround(z))));
}

void ChartCanvas::updateVisibleCells() {
  if (!m_worker || m_catalog.isEmpty()) return;

  // Diagnostics: time the whole pass (selection runs on the GUI thread) so we
  // can see if it grows as cells accumulate. Enabled by OCPN_INSTR.
  const bool kInstr = qEnvironmentVariableIsSet("OCPN_INSTR");
  QElapsedTimer dbgTimer;
  if (kInstr) dbgTimer.start();

  const double scale = m_viewport->scale();
  const double cw = width() > 0 ? width() : 1024.0;
  const double ch = height() > 0 ? height() : 720.0;
  // Half-extents of the visible rectangle in degrees. Under chart rotation the
  // axis-aligned bounding box of the (rotated) view is larger, so widen by the
  // rotated-corner extent: |hw·cos|+|hh·sin| etc. Plus 30% pan hysteresis.
  const double rot = m_viewport->rotation();
  const double ac = std::abs(std::cos(rot)), as = std::abs(std::sin(rot));
  const double hw = (cw / 2.0) / scale, hh = (ch / 2.0) / scale;
  const double half_lon = (hw * ac + hh * as) * 1.3;
  const double half_lat = (hw * as + hh * ac) * 1.3;
  // The viewport keeps longitude UN-normalised so the basemap can scroll
  // continuously across the antimeridian (it renders the world repeated at
  // k*360 shifts). The ENC catalog lives in [-180,180] and neither the spatial
  // index nor CellExtent::covers/intersects wrap, so once the view scrolls a
  // full lap past the seam (centerLon -> e.g. 283) every catalog query missed
  // and the charts vanished to basemap. Use a seam-NORMALISED centre longitude
  // for all catalog work below (the basemap keeps the raw continuous value).
  const auto wrapLon = [](double d) {
    d = std::fmod(d + 180.0, 360.0);
    if (d < 0.0) d += 360.0;
    return d - 180.0;
  };
  const double clatV = m_viewport->centerLat();
  const double clonV = wrapLon(m_viewport->centerLon());
  const double lat0 = clatV - half_lat;
  const double lat1 = clatV + half_lat;
  const double lon0 = clonV - half_lon;  // may exceed +/-180 (wide / seam-straddle)
  const double lon1 = clonV + half_lon;

  // Scale gates, computed up front because the candidate prune below needs them.
  // displayN = the 1:N the view is showing (cos-lat-corrected); cm93_bias tilts
  // which CM93 tier is eligible.
  const double displayN =
      displayScaleN(scale, m_viewport ? m_viewport->centerLat() : 0.0);
  const double cm93_bias =
      std::pow(3.0, ChartConfig::instance().cm93Detail() / 2.5);
  const auto effScaleOf = [&](const CellExtent* c) -> double {
    return c->nativeScale *
           (c->name.startsWith(QLatin1String("CM93-")) ? cm93_bias : 1.0);
  };
  // How far a chart may be underzoomed before we stop drawing it and let the
  // basemap show. Generous (well past the 4x content admit) but FINITE: a cell
  // beyond this can never win or be admitted as fallback, so it is pruned from
  // the candidate set here. Without this, an extreme zoom-out (e.g. 1:1.1M over
  // a coast charted only by 1:45k tiles) dragged the WHOLE fine-cell tile set --
  // none of it legible -- into the selection loop AND the decode/render set,
  // hanging the GUI thread. The SAME factor culls the boundary overlay
  // (updateBoundaryExtents), so decode and outline stay consistent; it is the
  // user-facing "chart cull factor" setting (default 8).
  const double kFallbackMaxUnderzoom =
      ChartConfig::instance().chartCullFactor();

  // Candidate cells: every catalogued cell overlapping the working view that
  // has a known scale and actual chart content (administrative coverage-only
  // cells -- no depth areas/soundings/land -- are excluded so they never win
  // a location and render nothing useful), AND that is drawable at this zoom
  // (within kFallbackMaxUnderzoom of its own scale -- a cell that could never be
  // selected is pure cost in the loop below and a wasted decode if it slipped in).
  // Spatial index gives a coarse superset of cells near the view (O(in-view),
  // not O(whole catalog) -- the catalog can hold thousands of cells); refine
  // with the precise scale/feature/intersects test, exactly as before.
  // Query the catalog within [-180,180]. If the (normalised) view straddles the
  // antimeridian -- lon0 < -180 or lon1 > 180 -- or is wider than the world,
  // split into 1-2 in-range longitude spans and union; a cell is in view if it
  // overlaps ANY span. (The common wrapped case -- a narrow view a full lap off
  // the seam -- normalises to a single in-range span, no split.)
  QVarLengthArray<QPair<double, double>, 2> lonSpans;
  if (lon1 - lon0 >= 360.0)
    lonSpans.push_back({-180.0, 180.0});
  else if (lon0 < -180.0)
    lonSpans.push_back({lon0 + 360.0, 180.0}), lonSpans.push_back({-180.0, lon1});
  else if (lon1 > 180.0)
    lonSpans.push_back({lon0, 180.0}), lonSpans.push_back({-180.0, lon1 - 360.0});
  else
    lonSpans.push_back({lon0, lon1});
  QList<const CellExtent*> near;
  for (const auto& s : lonSpans)
    m_spatial_index.query(lat0, lat1, s.first, s.second, near);
  const auto lonInView = [&](const CellExtent* c) {
    for (const auto& s : lonSpans)
      if (c->east >= s.first && c->west <= s.second) return true;
    return false;
  };
  QList<const CellExtent*> cands;
  cands.reserve(near.size());
  QSet<const CellExtent*> seenCand;  // dedup across the (up to 2) span queries
  for (const CellExtent* c : near) {
    if (seenCand.contains(c)) continue;
    seenCand.insert(c);
    if (c->nativeScale > 0 && c->navFeatures > 0 &&
        displayN <= effScaleOf(c) * kFallbackMaxUnderzoom &&
        c->north >= lat0 && c->south <= lat1 && lonInView(c))
      cands.append(c);
  }

  // Composite quilt (see Docs/QT_QUILT_VS_WX.md "Display rules"). At any zoom we
  // render the finest scale-appropriate chart PLUS coarser charts beneath it
  // (finer on top, by z-order), each clipped to its M_COVR coverage -- so a
  // coarser chart always fills any gap in a finer chart's coverage and the
  // basemap shows only where NO chart covers. A chart's CONTENT renders only
  // once the view is zoomed in to within k x of its natural scale; finer (not-
  // yet-reached) charts show only their bbox rectangle (ChartBoundaryProvider).
  // (displayN, cm93_bias + the kFallbackMaxUnderzoom prune were set up above.)
  const double k = m_overzoom_k > 0.0 ? m_overzoom_k : 2.0;  // 1 (at native)..5
  // Underzoom admit (wx Quilt parity): a cell joins the quilt while the
  // display is no more than ~4x coarser than its native scale -- wx shows
  // a 1:45k island chart at a 1:170k view and lets SCAMIN/declutter thin
  // it. The OVERZOOM factor k is a different knob (how far past native
  // you may zoom IN before the hatch) and stays out of this bound; tying
  // them left islands rendering only overview shapes until the view was
  // nearly at chart scale.
  constexpr double kUnderzoomAdmit = 4.0;

  // Candidates finest -> coarsest.
  std::sort(cands.begin(), cands.end(),
            [](const CellExtent* a, const CellExtent* b) {
              return a->nativeScale < b->nativeScale;
            });

  // Sample the view on a grid for the coverage tests below.
  constexpr int kGrid = 24;
  struct GridPt {
    double lat, lon;
  };
  QVarLengthArray<GridPt, kGrid * kGrid> pts;
  for (int gy = 0; gy < kGrid; ++gy) {
    const double plat = lat0 + (gy + 0.5) / kGrid * (lat1 - lat0);
    for (int gx = 0; gx < kGrid; ++gx)
      pts.push_back(
          {plat, wrapLon(lon0 + (gx + 0.5) / kGrid * (lon1 - lon0))});
  }
  const QSet<QString> prev_needed = m_needed;  // to detect a real change below
  m_needed.clear();
  // ---- wx-style "finest appropriate chart" selection ----
  // Per LOCATION, render the FINEST chart that covers it and is still scale-
  // appropriate -- not zoomed out more than kUnderzoomAdmit (4x, wx vector
  // GetNomScaleMin) past its native scale. This is the legacy wx Quilt rule:
  // show the largest-scale (finest) chart the zoom justifies, fill its coverage
  // gaps with progressively COARSER charts, and let SCAMIN/declutter thin the
  // detail -- never a chart FINER than the finest appropriate one (wx Compose
  // skips those as "scale too large"). cands are sorted finest -> coarsest, so
  // the first eligible covering cell IS the finest.
  //
  // The interim Qt build picked the chart whose scale was log-CLOSEST to the view
  // instead. That deliberately AVOIDED finer charts (to dodge a nav-aid "dead
  // band" where a finer cell shown underzoomed hid its own SCAMIN-culled aids) --
  // but at e.g. a 1:60k view it chose a 1:80k cell over an available 1:22k one,
  // which is exactly the "overscale / coarse chart while a finer one exists"
  // report. wx accepts the SCAMIN behaviour (aids follow the displayed chart);
  // the per-frame sounding SCAMIN fix (s52_vector_chart_provider) addresses the
  // related soundings symptom. Each point = exactly one chart (no stacking of
  // fully-overlapping cells, the old harbour-zoom overdraw cost); zOrderForScale
  // still puts finer on top where tiers meet. Where NO eligible chart covers a
  // point (zoomed out past every covering chart) keep coverage with the COARSEST
  // cover so the spot is never dropped to the basemap.
  // Overscale admit: how far the view may be zoomed IN past a chart's native
  // scale before we'd rather UNDERZOOM a finer chart than overscale this one
  // (wx GetNomScaleMax = scale/4). Crucial: the underzoom test alone (eligible)
  // lets a coarse OVERVIEW always qualify -- its native 1:N dwarfs the view -- so
  // a finer chart sitting just past the 4x underzoom limit was being rejected in
  // favour of a 1:350k overview overscaled ~7x (the bug). wx instead extends the
  // finer chart's range to fill that scale gap. So: prefer the finest chart that
  // is neither badly overscaled (ratio >= 1/kOverscaleAdmit) NOR underzoomed past
  // its appropriate band; only widen to the cull limit, and only as a coarse
  // overview overscaled past kOverscaleAdmit when nothing better covers.
  constexpr double kOverscaleAdmit = kUnderzoomAdmit;  // 4x (wx scale/4)
  // Fallback underzoom reach: a finer chart may be shown UNDERZOOMED up to this
  // to fill a scale gap (the "overscale while a finer chart exists" fix needs
  // ~4.2x), but NO further -- beyond it the basemap shows. This bound is what
  // stops a continental zoom from dragging in the memory-huge far-underzoomed
  // overview cells: they spike RSS to OOM and the freed heap never returns
  // (malloc_trim reclaims almost nothing -- it is GL/atlas memory), so the
  // build-floor guard then starves every cell and the whole quilt drops to
  // basemap at ALL scales until restart. Must be >= kUnderzoomAdmit; env
  // OCPN_QT_FALLBACK_ADMIT (restored after the wx-selection rewrite dropped it).
  static const double kFallbackAdmit = [] {
    bool ok = false;
    const double v = qgetenv("OCPN_QT_FALLBACK_ADMIT").toDouble(&ok);
    return (ok && v >= kUnderzoomAdmit) ? v : 6.0;
  }();
  for (const GridPt& p : pts) {
    const CellExtent* appropriate = nullptr;  // finest cover within [1/4 .. 4]
    const CellExtent* gapFill = nullptr;      // finest cover within [1/4 .. fallbackAdmit]
    const CellExtent* overscaled = nullptr;   // finest cover zoomed IN past (ratio < 1/4)
    for (const CellExtent* c : cands) {        // finest -> coarsest
      if (!c->covers(p.lat, p.lon)) continue;
      const double ratio = displayN / effScaleOf(c);  // >1 underzoom, <1 overscale
      if (ratio < 1.0 / kOverscaleAdmit) {     // zoomed IN past this chart
        if (!overscaled) overscaled = c;       // finest = least overscaled (cheap)
        continue;
      }
      if (!gapFill && ratio <= kFallbackAdmit) gapFill = c;
      if (!appropriate && ratio <= kUnderzoomAdmit) appropriate = c;
    }
    // Underzoom is CAPPED at kFallbackAdmit -> a cell more underzoomed than that
    // is NOT loaded (basemap shows; avoids the continental overview OOM). The
    // overscale last-resort is uncapped: you are zoomed IN so only a few small
    // cells cover, and showing the finest (least overscaled) beats the basemap.
    const CellExtent* pick =
        appropriate ? appropriate : (gapFill ? gapFill : overscaled);
    if (pick) m_needed.insert(pick->name);
  }

  // Safety backstop: bound how many cells the quilt loads at once. Even when all
  // are drawable, a dense band at a wide zoom can put dozens in the set -- each
  // an async decode + a per-frame render pass + (until #5) its own zoom timer.
  // Keep the cells nearest the view CENTRE (the centre stays fully charted; far
  // edges fall back to the coarser cell / basemap beneath, refilling as you pan
  // or zoom in). High default so normal use never trips it; env-tunable, 0 off.
  static const int kMaxCells = [] {
    bool ok = false;
    const int v = qgetenv("OCPN_QT_MAX_CELLS").toInt(&ok);
    return ok ? v : 50;
  }();
  if (kMaxCells > 0 && m_needed.size() > kMaxCells) {
    const double cLat = clatV, cLon = clonV;  // seam-normalised (see above)
    const auto centreDist2 = [&](const QString& n) -> double {
      auto it = m_catalog.constFind(n);
      if (it == m_catalog.cend()) return 1.0e18;
      // 0 if the view centre is inside the cell's bbox; else squared gap to it.
      const double dx = std::max({0.0, it->west - cLon, cLon - it->east});
      const double dy = std::max({0.0, it->south - cLat, cLat - it->north});
      return dx * dx + dy * dy;
    };
    QList<QString> names = m_needed.values();
    std::sort(names.begin(), names.end(),
              [&](const QString& a, const QString& b) {
                return centreDist2(a) < centreDist2(b);
              });
    QSet<QString> trimmed;
    for (int i = 0; i < kMaxCells; ++i) trimmed.insert(names[i]);
    qWarning("quilt: capped %lld -> %d cells nearest centre (OCPN_QT_MAX_CELLS)",
             static_cast<long long>(m_needed.size()), kMaxCells);
    m_needed = std::move(trimmed);
  }

  // Diagnostic: which candidates actually COVER the view centre (and at what
  // scale). Tells gap-vs-selection apart -- if the centre overscales on a coarse
  // cell, this shows whether any finer cell genuinely reaches it.
  if (kInstr && (m_needed != prev_needed)) {
    const double dcLat = clatV, dcLon = clonV;  // seam-normalised (see above)
    QStringList cov;
    for (const CellExtent* c : cands)
      if (c->covers(dcLat, dcLon))
        cov << QStringLiteral("%1@%2").arg(c->name).arg(c->nativeScale);
    qWarning("INSTR centreCover lat=%.5f lon=%.5f -> %s", dcLat, dcLon,
             cov.isEmpty() ? "NONE" : qPrintable(cov.join(QLatin1Char(','))));
  }

  // Only the displayed set actually changing warrants a log line + a chart-bar
  // refresh; a pan/zoom that lands on the same quilt is a no-op here.
  const bool needed_changed = (m_needed != prev_needed);
  if (needed_changed)
    qWarning("quilt: displayN=%.0f k=%.1f | %lld cells: %s", displayN, k,
             static_cast<long long>(m_needed.size()),
             qPrintable(QStringList(m_needed.values()).join(QLatin1Char(','))));

  // Publish the wanted set to the worker (out of band, thread-safe) BEFORE
  // queueing this pass's loads, so any still-queued decode for a cell that just
  // left m_needed is dropped at dequeue instead of decoded then discarded.
  // `this` identifies the canvas so a shared worker unions both panes' needs.
  if (m_worker) m_worker->setWanted(this, m_needed);

  // --- Load: needed cells not already requested. ---
  for (const QString& name : m_needed) {
    if (m_requested.contains(name)) continue;
    if (m_known_empty.contains(name)) continue;  // decoded to nothing already
    auto it = m_catalog.constFind(name);
    if (it == m_catalog.cend()) continue;
    m_requested.insert(name);
    QMetaObject::invokeMethod(m_worker, "loadCell", Qt::QueuedConnection,
                              Q_ARG(ocpn::qtui::CellExtent, it.value()));
  }

  // --- Resident chart cache (wx-style). SHOW the quilt selection, PARK (hide)
  // everything else but KEEP IT BUILT -- a cell is NOT destroyed just because it
  // left the view. That view-exit destroy was the "halt on reverse": panning
  // back over a just-departed chart rebuilt it from scratch (30 ms-1.5 s). Now a
  // reverse/revisit just re-shows the cached layer. Eviction is by LRU pressure
  // only (below), so memory stays bounded (we don't reopen the panning OOM).
  ++m_quilt_tick;
  for (auto it = m_loaded.begin(); it != m_loaded.end(); ++it) {
    if (!it.value().extent.valid()) continue;  // demo -- always visible
    const bool needed = m_needed.contains(it.key());
    if (needed) it.value().lastNeeded = m_quilt_tick;
    if (Layer* l = m_compositor->layer(it.value().layerId))
      l->setVisible(needed);
  }

  // LRU eviction: keep at most kMaxResident built cells; when over budget,
  // destroy the least-recently-needed cells that are not in the current quilt.
  static const int kMaxResident = [] {
    bool ok = false;
    const int v = qgetenv("OCPN_QT_MAX_RESIDENT").toInt(&ok);
    return ok ? v : 12;  // 0 disables the cap (unbounded -- debug only)
  }();
  // Memory-pressure guard: these overview/coastal cells can be HUNDREDS of MB
  // each (the over-detail decode -- 12 cells OOM'd at ~4 GB), so the resident
  // cache must never drive an OOM. When the system is low on free RAM, drop the
  // WHOLE parked cache and keep only the rendered quilt. Otherwise bound it by
  // the LRU count cap.
  static const long kMinFreeKB = [] {
    bool ok = false;
    const long v = qgetenv("OCPN_QT_MIN_FREE_MB").toLong(&ok);
    return (ok ? v : 1800) * 1024L;  // keep >= ~1.8 GB free by default
  }();
  long memAvailKB = 0;
  if (QFile mi(QStringLiteral("/proc/meminfo")); mi.open(QIODevice::ReadOnly)) {
    const QByteArray s = mi.readAll();
    const int i = s.indexOf("MemAvailable:");
    if (i >= 0)
      memAvailKB = s.mid(i + 13, 24).simplified().split(' ').first().toLong();
  }
  const bool memPressure = memAvailKB > 0 && memAvailKB < kMinFreeKB;

  QList<QString> parked;  // resident, not in the quilt -> evictable
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().extent.valid() && !m_needed.contains(it.key()))
      parked.append(it.key());
  QList<QString> evict;
  if (memPressure) {
    evict = parked;  // under pressure: keep only what is rendered
    if (kInstr && !evict.isEmpty())
      qWarning("INSTR memPressure: free=%ldMB < %ldMB -> drop %lld parked cells",
               memAvailKB / 1024, kMinFreeKB / 1024,
               static_cast<long long>(evict.size()));
  } else if (kMaxResident > 0 && m_loaded.size() > kMaxResident) {
    std::sort(parked.begin(), parked.end(),
              [this](const QString& a, const QString& b) {
                return m_loaded.value(a).lastNeeded < m_loaded.value(b).lastNeeded;
              });
    const int over = m_loaded.size() - kMaxResident;
    for (int i = 0; i < over && i < parked.size(); ++i) evict.append(parked[i]);
  }
  for (const QString& name : evict) {
    if (kInstr)
      qWarning("INSTR evict %s (LRU: resident=%lld > cap=%d)", qPrintable(name),
               static_cast<long long>(m_loaded.size()), kMaxResident);
    m_compositor->removeLayer(m_loaded.value(name).layerId);
    m_loaded.remove(name);
    m_requested.remove(name);  // eligible to reload when needed again
    ++m_dbg_evicts;
  }
  if (!evict.isEmpty()) {
    update();  // repaint; finer-coverage refresh below
    // Return the evicted cells' heap to the OS (delayed). Only (re)start when
    // idle so a SUSTAINED eviction run (e.g. a fast zoom-out storm) still trims
    // every ~interval instead of the timer being reset to never-fire by each
    // successive batch.
    if (!m_trim_timer->isActive()) m_trim_timer->start();
  }

  // Diagnostics: one line per re-quilt. Watch `loaded` and the gap between
  // `adds` and `evicts` over a pan -- if loaded climbs or adds outpaces evicts,
  // the resident set is growing (the suspected "increasingly heavy over time").
  // `dt` is the GUI-thread selection cost; pair with the compositor's
  // "freed N evicted subtree node(s)" line (also OCPN_INSTR) for frees.
  if (kInstr)
    qWarning(
        "VIS dt=%lldms needed=%d loaded=%d requested=%d knownEmpty=%d "
        "cands=%lld evicted=%d | cumAdds=%lld cumEvicts=%lld",
        static_cast<long long>(dbgTimer.elapsed()), m_needed.size(),
        m_loaded.size(), m_requested.size(), m_known_empty.size(),
        static_cast<long long>(cands.size()), evict.size(),
        static_cast<long long>(m_dbg_adds),
        static_cast<long long>(m_dbg_evicts));
  // Refresh the chart bar's coverage list only when the displayed set changed.
  if (needed_changed) emit chartCoverageChanged();

  // Over-scale (S-52): a chart is "overscaled" when the display is zoomed in
  // FINER than the chart's compilation scale -- factor = chart 1:N / display
  // 1:N (both are 1:N denominators; the display N shrinks as you zoom in, so
  // overscale is chart/display > 1, NOT the reciprocal). This is a per-chart
  // property, so the banner reflects the chart actually under the view CENTRE
  // (the one the mariner is looking at), not the finest cell anywhere in the
  // quilt -- a small overscaled cell in a corner must not drive the readout.
  // The hatch overlay (per provider) shows it on each overscaled cell directly.
  const double cLat = clatV, cLon = clonV;  // seam-normalised (see above)
  int centreN = 0;  // finest displayed cell covering the view centre
  for (const QString& name : m_needed) {
    auto it = m_catalog.constFind(name);
    if (it == m_catalog.cend() || it->nativeScale <= 0) continue;
    if (!it->covers(cLat, cLon)) continue;
    if (centreN == 0 || it->nativeScale < centreN) centreN = it->nativeScale;
  }
  // The raw ratio (chart 1:N / display 1:N): >1 means zoomed in finer than the
  // chart was compiled for. But the quilt INTENTIONALLY renders a chart
  // overzoomed up to m_overzoom_k (default 2, up to 5) -- that is its normal
  // display band, not "overscale". So, like wx (EmbossOverzoomIndicator fires
  // only when ref_scale/chart_scale > 3.9, well above any normal overzoom), the
  // warning must sit ABOVE that band: threshold = max(4, k). Below it -> report
  // 0 so the banner stays hidden during ordinary quilt overzoom (e.g. right
  // after a piano-bar select that autoscales a chart to ~its native scale).
  const double overscaleThr = std::max(4.0, k);
  const double raw = (centreN > 0 && displayN > 0.0) ? centreN / displayN : 0.0;
  const double newOverscale = raw > overscaleThr ? raw : 0.0;
  // Quantise relative to the current value (a fixed absolute step would churn
  // at large factors and barely move near 1). 5% change is the HUD threshold.
  if (std::abs(newOverscale - m_overscale_factor) >
      0.05 * std::max(1.0, m_overscale_factor)) {
    m_overscale_factor = newOverscale;
    emit overscaleChanged();
  }

  // Re-cull the chart-outline overlay for the (possibly new) zoom band. Cheap:
  // a no-op unless the eligible count changed (a pure pan keeps displayN).
  updateBoundaryExtents();

  // Refresh finest-owner suppression once this pan/zoom has settled. Ownership
  // now depends on BOTH the loaded set AND the zoom (the closest-scale crossover
  // in updateFinerCoverage is a function of displayN), so a pure zoom that
  // loads/evicts nothing still needs it. Debounced -> coalesces with any
  // in-flight cellLoaded arrival burst into one O(loaded^2) pass.
  m_finer_debounce->start();
}

void ChartCanvas::updateFinerCoverage() {
  if (!m_viewport) return;
  // For each loaded cell, gather the coverage of every OTHER loaded cell that is
  // strictly FINER *and has crossed its ownership threshold at the current zoom*,
  // and hand it to the provider. A point annotation (symbol, label, sounding,
  // light sector) whose ORIGIN falls in that coverage is then suppressed there --
  // drawn once, by the finest cell that actually OWNS the spot. This is the
  // scene-graph form of wx's quilt region-subtraction (gui/src/quilt.cpp:
  // ActiveRegion = quilt_region - m_covered_region), applied at the point-
  // annotation level so the fills/lines keep their cheap bbox clip.
  //
  // OWNERSHIP (not "finest loaded"): a finer cell only takes over -- and only
  // then suppresses the coarser chart's annotations -- once it is the CLOSEST-
  // scale chart where the two overlap, i.e. the same winner updateVisibleCells
  // picks. With the log-scale closest rule (dist = |ln(eff/displayN)|), a finer
  // cell F beats a coarser cell C exactly when displayN < sqrt(eff_C * eff_F)
  // (the geometric mean of their scales). Below that crossover the coarser chart
  // stays primary and keeps ALL its aids -- and its own SCAMINs are correct,
  // because it is being shown near its compilation scale. This removes the
  // "dead band" where a still-underzoomed finer cell blanked the coarser chart's
  // visible buoys/lights/soundings, leaving a hole.
  const double displayN =
      displayScaleN(m_viewport->scale(), m_viewport->centerLat());
  const double cm93_bias =
      std::pow(3.0, ChartConfig::instance().cm93Detail() / 2.5);
  const auto effScale = [&](const CellExtent& e) -> double {
    const bool is_cm93 = e.name.startsWith(QLatin1String("CM93-"));
    return e.nativeScale * (is_cm93 ? cm93_bias : 1.0);
  };
  for (auto it = m_loaded.begin(); it != m_loaded.end(); ++it) {
    LoadedCell& lc = it.value();
    if (!lc.provider || !lc.extent.valid() || lc.extent.nativeScale <= 0)
      continue;
    const double effC = effScale(lc.extent);
    QList<QPolygonF> finer;
    for (auto jt = m_loaded.cbegin(); jt != m_loaded.cend(); ++jt) {
      if (jt.key() == it.key()) continue;
      // Only RENDERED (selected) finer cells suppress -- a parked/hidden finer
      // cell (resident for hysteresis but not in the quilt) must not blank the
      // visible coarser chart's annotations.
      if (!m_needed.contains(jt.key())) continue;
      const CellExtent& f = jt.value().extent;
      // Strictly finer (so it draws on top here) and actually overlapping.
      if (!f.valid() || f.nativeScale <= 0 ||
          f.nativeScale >= lc.extent.nativeScale)
        continue;
      if (!f.intersects(lc.extent.south, lc.extent.north, lc.extent.west,
                        lc.extent.east))
        continue;
      // Ownership gate: only suppress once the finer cell has crossed the
      // closest-scale crossover with this (coarser) cell. Until then this chart
      // is primary -- keep its annotations.
      if (displayN >= std::sqrt(effC * effScale(f)))
        continue;  // finer cell not yet the owner here
      if (!f.coverage.isEmpty()) {
        finer += f.coverage;  // real M_COVR polygons (lon/lat)
      } else {
        // No M_COVR captured -> the finer cell owns its whole bounding box.
        QPolygonF bb;
        bb << QPointF(f.west, f.south) << QPointF(f.east, f.south)
           << QPointF(f.east, f.north) << QPointF(f.west, f.north);
        finer << bb;
      }
    }
    if (lc.provider) lc.provider->setFinerCoverage(finer);
  }
}

void ChartCanvas::updateBoundaryExtents() {
  if (!m_boundary_provider || !m_viewport || m_catalog.isEmpty()) return;

  const double displayN =
      displayScaleN(m_viewport->scale(), m_viewport->centerLat());
  const double cm93_bias =
      std::pow(3.0, ChartConfig::instance().cm93Detail() / 2.5);
  const double cull = ChartConfig::instance().chartCullFactor();
  const auto eligible = [&](const CellExtent& c) {
    if (c.nativeScale <= 0) return false;
    const bool is_cm93 = c.name.startsWith(QLatin1String("CM93-"));
    const double eff = c.nativeScale * (is_cm93 ? cm93_bias : 1.0);
    return displayN <= eff * cull;
  };

  // Count first (cheap O(catalog), no allocation). The eligible set is monotone
  // in zoom -- zooming in only ADDS charts -- so a changed count is the only
  // signal we need that the displayed set really changed; a pure pan keeps
  // displayN and the count, and we skip the (node-rebuilding) setExtents.
  int n = 0;
  for (auto it = m_catalog.cbegin(); it != m_catalog.cend(); ++it)
    if (eligible(it.value())) ++n;
  if (n == m_boundary_count) return;
  m_boundary_count = n;

  QList<CellExtent> shown;
  shown.reserve(n);
  for (auto it = m_catalog.cbegin(); it != m_catalog.cend(); ++it)
    if (eligible(it.value())) shown.append(it.value());
  m_boundary_provider->setExtents(shown);
}

QVariantList ChartCanvas::chartBarCells() const {
  QVariantList out;
  if (!m_viewport || m_catalog.isEmpty()) return out;
  // Like wx's chart bar, list every chart AVAILABLE over the view (all scale
  // bands), not just the rendered ones -- so the user can pick a different
  // scale from the bar. The "displayed" flag marks the cells actually in the
  // active quilt (m_needed) so QML can outline what's currently drawn.
  const double s = m_viewport->scale();
  if (s <= 0.0) return out;
  const double halfLon = (width() / 2.0) / s;
  const double halfLat = (height() / 2.0) / s;
  // Seam-normalise the centre longitude (the viewport keeps it un-normalised for
  // continuous antimeridian scroll; the catalog is in [-180,180]). Without this
  // the chart bar emptied once the view scrolled a lap past the seam -- the same
  // failure as updateVisibleCells. Split the query if it straddles +/-180.
  const auto wrapLon = [](double d) {
    d = std::fmod(d + 180.0, 360.0);
    if (d < 0.0) d += 360.0;
    return d - 180.0;
  };
  const double cLat = m_viewport->centerLat();
  const double cLon = wrapLon(m_viewport->centerLon());
  const double latMin = cLat - halfLat, latMax = cLat + halfLat;
  const double lon0 = cLon - halfLon, lon1 = cLon + halfLon;
  QVarLengthArray<QPair<double, double>, 2> lonSpans;
  if (lon1 - lon0 >= 360.0)
    lonSpans.push_back({-180.0, 180.0});
  else if (lon0 < -180.0)
    lonSpans.push_back({lon0 + 360.0, 180.0}), lonSpans.push_back({-180.0, lon1});
  else if (lon1 > 180.0)
    lonSpans.push_back({lon0, 180.0}), lonSpans.push_back({-180.0, lon1 - 360.0});
  else
    lonSpans.push_back({lon0, lon1});

  QList<const CellExtent*> near;
  for (const auto& sp : lonSpans)
    m_spatial_index.query(latMin, latMax, sp.first, sp.second, near);
  QList<const CellExtent*> cells;
  cells.reserve(near.size());
  QSet<const CellExtent*> seen;
  for (const CellExtent* c : near) {
    if (seen.contains(c)) continue;
    seen.insert(c);
    if (c->navFeatures <= 0) continue;  // administrative cell -- not a chart
    if (c->north < latMin || c->south > latMax) continue;
    bool lonOk = false;
    for (const auto& sp : lonSpans)
      if (c->east >= sp.first && c->west <= sp.second) { lonOk = true; break; }
    if (!lonOk) continue;
    cells.append(c);
  }
  // Coarse -> fine (largest 1:N first), like the wx chart bar.
  std::sort(cells.begin(), cells.end(),
            [](const CellExtent* a, const CellExtent* b) {
              if (a->nativeScale != b->nativeScale)
                return a->nativeScale > b->nativeScale;
              return a->name < b->name;
            });
  for (const CellExtent* c : cells) {
    QVariantMap m;
    m["name"] = c->name;
    m["band"] = c->band;
    m["scale"] = c->nativeScale;
    m["displayed"] = m_needed.contains(c->name);
    m["north"] = c->north; m["south"] = c->south;
    m["east"] = c->east; m["west"] = c->west;
    out.append(m);
  }
  return out;
}

void ChartCanvas::selectChart(const QString& name) {
  auto it = m_catalog.constFind(name);
  if (it == m_catalog.cend() || it->nativeScale <= 0 || !m_viewport) return;
  // Autoscale to the chart's native compilation scale, keeping the current
  // centre (mirrors wx SelectQuiltRefdbChart with autoscale). Setting the
  // viewport scale rebases the quilt so this chart's band becomes the one
  // rendered here. scale (px/deg-lon) is the inverse of displayScaleN:
  // N = K*cos(lat)/scale, so scale = K*cos(lat)/N.
  constexpr double kK = 111320.0 * 3.78 * 1000.0;  // see displayScaleN()
  const double clat = std::max(0.05, std::cos(m_viewport->centerLat() *
                                              M_PI / 180.0));
  m_viewport->setScale(kK * clat / it->nativeScale);
  emit viewChanged();
  update();  // viewport::changed also kicks the debounced quilt rebuild
}

QVariantMap ChartCanvas::scaleBar() const {
  QVariantMap out;
  if (!m_viewport || width() <= 1.0) return out;
  constexpr double kPi = 3.14159265358979323846;
  // Mercator: scale is px per degree of longitude. Ground NM per horizontal
  // pixel at the centre latitude (1° lon = 60·cos(lat) NM).
  const double coslat = std::max(0.05, std::cos(m_viewport->centerLat()
                                                * kPi / 180.0));
  const double nm_per_px = 60.0 * coslat / m_viewport->scale();
  if (!std::isfinite(nm_per_px) || nm_per_px <= 0.0) return out;

  // Aim the bar at ~a quarter of the canvas width, then round to a 1/2/5
  // "nice" number in the user's distance unit (dropping to m/ft when short).
  const double target_nm = (width() * 0.25) * nm_per_px;
  double per_nm;       // user-unit per NM
  QString suffix;
  switch (DisplayConfig::instance().distanceUnit()) {
    case 1: per_nm = 1.852;        suffix = QStringLiteral("km"); break;
    case 2: per_nm = 1.150779448;  suffix = QStringLiteral("mi"); break;
    default: per_nm = 1.0;         suffix = QStringLiteral("NM"); break;
  }
  double target_u = target_nm * per_nm;
  if (target_u < 0.5) {  // too short for the big unit -> metres / feet
    if (suffix == QStringLiteral("mi")) { per_nm = 6076.115; suffix = QStringLiteral("ft"); }
    else { per_nm = 1852.0; suffix = QStringLiteral("m"); }
    target_u = target_nm * per_nm;
  }
  if (!(target_u > 0.0)) return out;

  const double logd = std::log10(target_u);
  const double places = std::floor(logd);
  const double rem = logd - places;
  double nice_u = std::pow(10.0, places);
  if (rem < 0.2) nice_u /= 5.0;        // ... 0.2, 0.5, 1, 2, 5, 10 ...
  else if (rem < 0.5) nice_u /= 2.0;

  const double nice_nm = nice_u / per_nm;
  out[QStringLiteral("length")] = nice_nm / nm_per_px;  // pixels
  out[QStringLiteral("label")] =
      QStringLiteral("%1 %2").arg(nice_u, 0, 'g', 4).arg(suffix);
  return out;
}

void ChartCanvas::highlightChartCell(const QString& name) {
  if (m_boundary_provider) m_boundary_provider->setHighlight(name);
  update();
}

// --- Route editing (#31) ---------------------------------------------------
bool ChartCanvas::hitRouteNode(const QPointF& sp, int& route, int& node) const {
  if (!m_nav_provider || !m_viewport) return false;
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  constexpr double kR = 11.0;  // px
  double best = kR * kR;
  bool found = false;
  const QList<NavRoute>& rs = m_nav_provider->userRoutes();
  for (int ri = 0; ri < rs.size(); ++ri) {
    const NavRoute& r = rs[ri];
    // Only hit routes that are actually drawn: eye on, or the selected route
    // (matches RouteLayer's draw rule) -- an invisible route must not be
    // summoned by a click where it happens to lie.
    if (!m_visible_routes.contains(r.guid) && ri != m_selected_route) continue;
    for (int pi = 0; pi < r.points.size(); ++pi) {
      const QPointF s = m.map(QPointF(
          r.points[pi].x(), Viewport::latToWorldY(r.points[pi].y())));
      const double dx = s.x() - sp.x(), dy = s.y() - sp.y();
      const double d2 = dx * dx + dy * dy;
      if (d2 < best) {
        best = d2;
        route = ri;
        node = pi;
        found = true;
      }
    }
  }
  return found;
}

bool ChartCanvas::hitRouteSegment(const QPointF& sp, int& route, int& seg,
                                  double& lat, double& lon) const {
  if (!m_nav_provider || !m_viewport) return false;
  const int w = static_cast<int>(width()), h = static_cast<int>(height());
  const QMatrix4x4 m = m_viewport->transformMatrix(w, h);
  constexpr double kR = 8.0;  // px
  double best = kR * kR;
  bool found = false;
  const QList<NavRoute>& rs = m_nav_provider->userRoutes();
  for (int ri = 0; ri < rs.size(); ++ri) {
    const NavRoute& r = rs[ri];
    // Only hit routes that are actually drawn (eye on, or selected) -- don't
    // let a click on empty water select a hidden route lying under it.
    if (!m_visible_routes.contains(r.guid) && ri != m_selected_route) continue;
    for (int si = 0; si + 1 < r.points.size(); ++si) {
      const QPointF a = m.map(QPointF(
          r.points[si].x(), Viewport::latToWorldY(r.points[si].y())));
      const QPointF bp = m.map(QPointF(
          r.points[si + 1].x(), Viewport::latToWorldY(r.points[si + 1].y())));
      const QPointF ab = bp - a;
      const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
      double t = l2 > 0.0 ? ((sp.x() - a.x()) * ab.x() +
                             (sp.y() - a.y()) * ab.y()) / l2
                          : 0.0;
      t = std::clamp(t, 0.0, 1.0);
      const QPointF proj = a + ab * t;
      const double dx = proj.x() - sp.x(), dy = proj.y() - sp.y();
      const double d2 = dx * dx + dy * dy;
      if (d2 < best) {
        best = d2;
        route = ri;
        seg = si;
        found = true;
      }
    }
  }
  if (found) m_viewport->screenToLatLon(sp.x(), sp.y(), w, h, lat, lon);
  return found;
}

void ChartCanvas::setRouteEditMode(bool on) {
  if (on == m_route_edit_mode) return;
  m_route_edit_mode = on;
  if (m_route_layer) m_route_layer->setEditing(on);  // big handles only in edit
  emit routeEditModeChanged();
  update();
}

void ChartCanvas::editRoute(int index) {
  showRoute(index);          // select + solo + zoom to extent
  setRouteEditMode(true);    // now nodes are draggable / legs insertable
}

void ChartCanvas::selectRoute(int route) {
  if (route == m_selected_route) return;
  m_selected_route = route;
  setRouteEditMode(false);  // changing the selection leaves edit mode

  if (m_route_layer) {
    QString guid;
    if (m_nav_provider && route >= 0) {
      const QList<NavRoute> rs = m_nav_provider->userRoutes();
      if (route < rs.size()) guid = rs[route].guid;
    }
    m_route_layer->setSelectedRouteGuid(guid);
  }
  emit selectedRouteChanged();
  update();
}

void ChartCanvas::clearRouteSelection() { selectRoute(-1); }

void ChartCanvas::deleteRoutePointAtMenu() {
  if (m_nav_provider && m_menu_route >= 0 && m_menu_node >= 0)
    m_nav_provider->deleteRoutePoint(m_menu_route, m_menu_node);
  m_menu_route = m_menu_node = -1;
  clearRouteSelection();  // indices may have shifted -- drop selection
}

void ChartCanvas::deleteSelectedRoute() {
  if (m_nav_provider && m_selected_route >= 0)
    m_nav_provider->deleteRoute(m_selected_route);
  clearRouteSelection();
}

void ChartCanvas::reverseRoute(int index) {
  if (m_nav_provider) m_nav_provider->reverseRoute(index);
}

void ChartCanvas::duplicateRoute(int index) {
  if (m_nav_provider) m_nav_provider->duplicateRoute(index);
}

void ChartCanvas::setRoutePointIcon(int index, const QString& icon) {
  if (m_nav_provider) m_nav_provider->setRoutePointIcon(index, icon);
  update();
}

QString ChartCanvas::routePointIcon(int index) const {
  if (!m_nav_provider) return QString();
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size()) return QString();
  return rs[index].pointIcon;
}

void ChartCanvas::showRoute(int index) {
  if (!m_nav_provider) return;
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size() || rs[index].points.isEmpty()) return;
  double n = -90, s = 90, e = -180, w = 180;
  for (const QPointF& p : rs[index].points) {  // (lon, lat)
    n = std::max(n, p.y());
    s = std::min(s, p.y());
    e = std::max(e, p.x());
    w = std::min(w, p.x());
  }
  selectRoute(index);   // highlight it
  fitBounds(n, s, e, w);  // zoom to its extent
}

void ChartCanvas::activateRoute(int index) {
  if (!m_route_follower || !m_nav_provider) return;
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size()) return;
  // Make sure the route's "eye" is on so the followed route is visible.
  if (!rs[index].guid.isEmpty() && !m_visible_routes.contains(rs[index].guid))
    setRouteVisible(index, true);
  if (!m_route_follower->activate(index)) return;
  // Zoom to the route's extent (wx ZoomtoRoute), leaving the edit selection
  // untouched (following is not editing).
  if (!rs[index].points.isEmpty()) {
    double n = -90, s = 90, e = -180, w = 180;
    for (const QPointF& p : rs[index].points) {  // (lon, lat)
      n = std::max(n, p.y());
      s = std::min(s, p.y());
      e = std::max(e, p.x());
      w = std::min(w, p.x());
    }
    fitBounds(n, s, e, w);
  }
  update();
}

void ChartCanvas::deactivateRoute() {
  if (m_route_follower) m_route_follower->deactivate();
  update();
}

void ChartCanvas::skipWaypoint() {
  if (m_route_follower) m_route_follower->skip();
  update();
}

void ChartCanvas::startGotoRoute(double lat, double lon, const QString& name) {
  if (!m_nav_provider || !m_route_follower) return;
  const OwnShipState own = m_nav_provider->ownShip();
  if (!own.valid) return;
  // Only one outstanding GOTO at a time: drop a previous, unfinished one.
  if (!m_goto_guid.isEmpty()) {
    const QList<NavRoute> routes = m_nav_provider->userRoutes();
    for (int i = 0; i < routes.size(); ++i) {
      if (routes[i].guid == m_goto_guid) {
        m_nav_provider->deleteRoute(i);
        break;
      }
    }
    m_goto_guid.clear();
  }
  const int idx = m_nav_provider->createRoute(
      name, {QPointF(own.lon, own.lat), QPointF(lon, lat)});
  if (idx < 0) return;
  m_goto_guid = m_nav_provider->userRoutes().value(idx).guid;
  // Activate directly (no zoom-to-extent: the boat and the target are both
  // already in or near the view the user is working in).
  m_route_follower->activate(idx);
  update();
}

void ChartCanvas::navigateToHere() {
  // wx ID_DEF_MENU_GOTO_HERE: a temporary 2-point route from the fix to the
  // right-click point, activated immediately, deleted on arrival.
  startGotoRoute(m_ctx_lat, m_ctx_lon, tr("Go to here"));
}

void ChartCanvas::navigateToWaypoint(const QString& guid) {
  // wx ID_WP_MENU_GOTO: a temporary route from the fix to the mark.
  if (!m_nav_provider) return;
  for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
    if (wp.guid == guid) {
      startGotoRoute(wp.lat, wp.lon,
                     wp.name.isEmpty() ? tr("Go to mark")
                                       : tr("Go to %1").arg(wp.name));
      return;
    }
  }
}

void ChartCanvas::zeroXte() {
  if (m_route_follower) m_route_follower->zeroXte();
}

void ChartCanvas::insertRoutePointAtMenu() {
  if (!m_nav_provider || m_menu_route < 0 || m_menu_seg < 0) return;
  m_nav_provider->insertRoutePoint(m_menu_route, m_menu_seg, m_menu_ins_lat,
                                   m_menu_ins_lon);
  update();
}

QVariantMap ChartCanvas::importGpx(const QUrl& url) {
  if (!m_nav_provider) return {};
  const QVariantMap counts = m_nav_provider->importGpx(url.toLocalFile());
  update();
  return counts;
}

bool ChartCanvas::exportGpxAll(const QUrl& url) const {
  return m_nav_provider && m_nav_provider->exportGpxAll(url.toLocalFile());
}

bool ChartCanvas::exportGpxRoute(int index, const QUrl& url) const {
  return m_nav_provider &&
         m_nav_provider->exportGpxRoute(index, url.toLocalFile());
}

bool ChartCanvas::exportGpxTrack(const QString& guid, const QUrl& url) const {
  return m_nav_provider &&
         m_nav_provider->exportGpxTrack(guid, url.toLocalFile());
}

bool ChartCanvas::exportGpxWaypoint(const QString& guid,
                                    const QUrl& url) const {
  return m_nav_provider &&
         m_nav_provider->exportGpxWaypoint(guid, url.toLocalFile());
}

void ChartCanvas::appendToRoute(int index) {
  if (!m_nav_provider || m_route_build_mode) return;
  if (!m_nav_provider->beginAppendRoute(index)) return;
  // Reuse the route-build mouse flow: click adds a point (addRoutePoint
  // appends to the model route in append mode), right-click finishes.
  m_route_build_mode = true;
  emit routeBuildModeChanged();
  update();
}

void ChartCanvas::splitRouteAtMenu() {
  if (!m_nav_provider || m_menu_route < 0 || m_menu_seg < 0) return;
  // A followed route can't be split under the follower's feet.
  if (m_nav_provider->userRoutes().value(m_menu_route).active)
    deactivateRoute();
  clearRouteSelection();  // indices shift: original deleted, A + B appended
  m_nav_provider->splitRoute(m_menu_route, m_menu_seg);
  update();
}

void ChartCanvas::pushUndo(const UndoOp& op) {
  m_undo_stack.append(op);
  while (m_undo_stack.size() > kMaxUndo) m_undo_stack.removeFirst();
  m_redo_stack.clear();
  emit undoChanged();
}

QVariantMap ChartCanvas::snapshotMark(const QString& guid) const {
  if (!m_nav_provider) return {};
  for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
    if (wp.guid != guid) continue;
    QVariantMap snap;
    snap["name"] = wp.name;
    snap["comment"] = wp.comment;
    snap["icon"] = wp.iconName;
    snap["lat"] = wp.lat;
    snap["lon"] = wp.lon;
    return snap;
  }
  return {};
}

QString ChartCanvas::recreateMark(const QVariantMap& snap) {
  if (!m_nav_provider || snap.isEmpty()) return {};
  return m_nav_provider->dropMark(
      snap.value("lat").toDouble(), snap.value("lon").toDouble(),
      snap.value("name").toString(), snap.value("comment").toString(),
      snap.value("icon").toString());
}

void ChartCanvas::undo() {
  if (m_undo_stack.isEmpty() || !m_nav_provider) return;
  UndoOp op = m_undo_stack.takeLast();
  if (op.created) {
    // Undo a creation: delete the mark (still snap-ed for redo).
    m_nav_provider->deleteWaypoint(op.guid);
  } else {
    // Undo a deletion: recreate (fresh GUID -- record it for redo).
    op.guid = recreateMark(op.snap);
  }
  m_redo_stack.append(op);
  emit undoChanged();
  update();
}

void ChartCanvas::redo() {
  if (m_redo_stack.isEmpty() || !m_nav_provider) return;
  UndoOp op = m_redo_stack.takeLast();
  if (op.created) {
    // Redo a creation: recreate it (fresh GUID).
    op.guid = recreateMark(op.snap);
  } else {
    // Redo a deletion: delete again.
    m_nav_provider->deleteWaypoint(op.guid);
  }
  m_undo_stack.append(op);
  emit undoChanged();
  update();
}

void ChartCanvas::startMeasure() {
  if (m_measure_active) return;
  m_measure_active = true;
  m_measure_pts.clear();
  m_measure_text = tr("Click to start measuring");
  if (m_measure_layer) m_measure_layer->setState({}, QPointF(), false);
  emit measureChanged();
  update();
}

void ChartCanvas::stopMeasure() {
  if (!m_measure_active) return;
  m_measure_active = false;
  m_measure_pts.clear();
  m_measure_text.clear();
  if (m_measure_layer) m_measure_layer->setState({}, QPointF(), false);
  emit measureChanged();
  update();
}

void ChartCanvas::updateMeasure(double cur_lat, double cur_lon,
                                bool has_cursor) {
  if (!m_measure_active) return;
  // Total over the fixed legs, plus the live rubber-band leg to the cursor.
  double total = 0.0;
  for (int i = 1; i < m_measure_pts.size(); ++i) {
    double brg = 0.0, dist = 0.0;
    DistanceBearingMercator(m_measure_pts[i].y(), m_measure_pts[i].x(),
                            m_measure_pts[i - 1].y(), m_measure_pts[i - 1].x(),
                            &brg, &dist);
    total += dist;
  }
  DisplayConfig& dc = DisplayConfig::instance();
  if (has_cursor && !m_measure_pts.isEmpty()) {
    const QPointF& last = m_measure_pts.last();
    double brg = 0.0, dist = 0.0;
    DistanceBearingMercator(cur_lat, cur_lon, last.y(), last.x(), &brg, &dist);
    m_measure_text = tr("Leg %1: %2  %3   ·   Total %4")
                         .arg(m_measure_pts.size())
                         .arg(dc.formatBearing(brg), dc.formatDistance(dist),
                              dc.formatDistance(total + dist));
  } else if (m_measure_pts.size() >= 2) {
    m_measure_text = tr("Total %1").arg(dc.formatDistance(total));
  } else {
    m_measure_text = tr("Click the next point");
  }
  if (m_measure_layer)
    m_measure_layer->setState(m_measure_pts, QPointF(cur_lon, cur_lat),
                              has_cursor);
  emit measureChanged();
  update();
}

bool ChartCanvas::hitAisAt(const QPointF& sp, int* mmsi,
                           QString* name) const {
  if (!m_nav_provider || !m_viewport) return false;
  constexpr double kPickRadiusPx = 14.0;
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  double best = kPickRadiusPx * kPickRadiusPx;
  bool found = false;
  for (const AisTarget& t : m_nav_provider->aisTargets()) {
    const QPointF s = m.map(QPointF(t.lon, Viewport::latToWorldY(t.lat)));
    const double dx = s.x() - sp.x(), dy = s.y() - sp.y();
    const double d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      if (mmsi) *mmsi = t.mmsi;
      if (name) *name = t.name;
      found = true;
    }
  }
  return found;
}

void ChartCanvas::selectAisTarget(int mmsi) {
  if (!m_ais_selection || !m_nav_provider) return;
  for (const AisTarget& t : m_nav_provider->aisTargets()) {
    if (t.mmsi == mmsi) {
      m_ais_selection->select(t);
      return;
    }
  }
}

void ChartCanvas::centerOnAis(int mmsi) {
  if (!m_viewport || !m_nav_provider) return;
  for (const AisTarget& t : m_nav_provider->aisTargets()) {
    if (t.mmsi == mmsi) {
      m_viewport->setCenter(t.lat, t.lon);
      emit viewChanged();
      update();
      return;
    }
  }
}

void ChartCanvas::copyToClipboard(const QString& text) const {
  if (QClipboard* cb = QGuiApplication::clipboard()) cb->setText(text);
}

bool ChartCanvas::copyRouteAsKml(int index) const {
  if (!m_nav_provider) return false;
  const QString kml = m_nav_provider->routeAsKml(index);
  if (kml.isEmpty()) return false;
  copyToClipboard(kml);
  return true;
}

bool ChartCanvas::copyTrackAsKml(const QString& guid) const {
  if (!m_nav_provider) return false;
  const QString kml = m_nav_provider->trackAsKml(guid);
  if (kml.isEmpty()) return false;
  copyToClipboard(kml);
  return true;
}

QVariantMap ChartCanvas::pasteKmlFromClipboard() {
  QClipboard* cb = QGuiApplication::clipboard();
  if (!cb || !m_nav_provider) return {};
  const QVariantMap counts = m_nav_provider->pasteKml(cb->text());
  if (!counts.isEmpty()) update();
  return counts;
}

bool ChartCanvas::copyMarkAsKml(const QString& guid) const {
  if (!m_nav_provider) return false;
  const QString kml = m_nav_provider->waypointAsKml(guid);
  if (kml.isEmpty()) return false;
  copyToClipboard(kml);
  return true;
}

QVariantList ChartCanvas::aisTargetSnapshot() const {
  QVariantList out;
  if (!m_nav_provider) return out;
  QList<AisTarget> targets = m_nav_provider->aisTargets();
  // Nearest first; targets without a range solution sort to the end.
  std::sort(targets.begin(), targets.end(),
            [](const AisTarget& a, const AisTarget& b) {
              const double ra = a.rangeNm >= 0 ? a.rangeNm : 1e9;
              const double rb = b.rangeNm >= 0 ? b.rangeNm : 1e9;
              return ra < rb;
            });
  DisplayConfig& dc = DisplayConfig::instance();
  for (const AisTarget& t : targets) {
    QVariantMap row;
    row["mmsi"] = t.mmsi;
    row["name"] = t.name.isEmpty() ? QString::number(t.mmsi) : t.name;
    row["rangeText"] = t.rangeNm >= 0 ? dc.formatDistance(t.rangeNm)
                                      : QStringLiteral("--");
    row["bearingText"] = t.bearingDeg >= 0 ? dc.formatBearing(t.bearingDeg)
                                           : QStringLiteral("--");
    row["sogText"] = QString::number(t.sog, 'f', 1);
    row["cogText"] = QString::number(t.cog, 'f', 0) + QChar(0x00B0);
    row["cpaText"] =
        t.cpaValid ? dc.formatDistance(t.cpaNm) : QStringLiteral("--");
    row["tcpaText"] = t.cpaValid && t.tcpaMin >= 0
                          ? QString::number(t.tcpaMin, 'f', 0) + tr(" min")
                          : QStringLiteral("--");
    row["dangerous"] = t.dangerous;
    row["isSart"] = t.isSart;
    out.append(row);
  }
  return out;
}

void ChartCanvas::setMarkRangeRings(const QString& guid, bool show,
                                    int count, double step, int units) {
  if (!m_nav_provider) return;
  m_nav_provider->setWaypointRangeRings(guid, show, count, step, units);
  update();
}

void ChartCanvas::setMarkScamin(const QString& guid, int scamin) {
  if (!m_nav_provider) return;
  m_nav_provider->setWaypointScamin(guid, scamin);
  update();
}

void ChartCanvas::loadMmsiProperties() {
  const QString blob = ConfigStore::instance().getString("ais/mmsiProps");
  if (blob.isEmpty()) return;
  qDeleteAll(g_MMSI_Props_Array);
  g_MMSI_Props_Array.clear();
  for (const QString& spec : blob.split('|', Qt::SkipEmptyParts)) {
    wxString wspec = QString_to_wxString(spec);
    g_MMSI_Props_Array.append(new MmsiProperties(wspec));
  }
}

void ChartCanvas::persistMmsiProperties() const {
  QStringList specs;
  for (MmsiProperties* p : g_MMSI_Props_Array)
    if (p) specs.append(wxString_to_QString(p->Serialize()));
  ConfigStore::instance().setString("ais/mmsiProps", specs.join('|'));
}

QVariantList ChartCanvas::mmsiProperties() const {
  QVariantList out;
  for (MmsiProperties* p : g_MMSI_Props_Array) {
    if (!p) continue;
    QVariantMap row;
    row["mmsi"] = p->MMSI;
    row["trackType"] = p->TrackType;  // 0 default, 1 always, 2 never
    row["ignore"] = p->m_bignore;
    row["mob"] = p->m_bMOB;
    row["vdm"] = p->m_bVDM;
    row["follower"] = p->m_bFollower;
    row["persistTrack"] = p->m_bPersistentTrack;
    row["shipName"] = wxString_to_QString(p->m_ShipName);
    out.append(row);
  }
  return out;
}

void ChartCanvas::saveMmsiProperty(const QVariantMap& row) {
  const int mmsi = row.value("mmsi").toInt();
  if (mmsi <= 0) return;
  MmsiProperties* p = nullptr;
  for (MmsiProperties* q : g_MMSI_Props_Array)
    if (q && q->MMSI == mmsi) {
      p = q;
      break;
    }
  if (!p) {
    p = new MmsiProperties(mmsi);
    g_MMSI_Props_Array.append(p);
  }
  p->TrackType = row.value("trackType", TRACKTYPE_DEFAULT).toInt();
  p->m_bignore = row.value("ignore", false).toBool();
  p->m_bMOB = row.value("mob", false).toBool();
  p->m_bVDM = row.value("vdm", false).toBool();
  p->m_bFollower = row.value("follower", false).toBool();
  p->m_bPersistentTrack = row.value("persistTrack", false).toBool();
  p->m_ShipName = QString_to_wxString(row.value("shipName").toString());
  persistMmsiProperties();
}

void ChartCanvas::deleteMmsiProperty(int mmsi) {
  for (int i = 0; i < g_MMSI_Props_Array.size(); ++i) {
    if (g_MMSI_Props_Array[i] && g_MMSI_Props_Array[i]->MMSI == mmsi) {
      delete g_MMSI_Props_Array.takeAt(i);
      persistMmsiProperties();
      return;
    }
  }
}

bool ChartCanvas::hitTrackAt(const QPointF& sp, QString* guid,
                             QString* name) const {
  if (!m_nav_provider || !m_viewport) return false;
  constexpr double kR = 8.0;  // px to the nearest segment (route parity)
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  double best = kR * kR;
  bool found = false;
  for (const NavTrack& t : m_nav_provider->tracks()) {
    if (!t.visible || t.points.size() < 2) continue;
    QPointF prev;
    for (int i = 0; i < t.points.size(); ++i) {
      const QPointF w(t.points[i].x(), Viewport::latToWorldY(t.points[i].y()));
      const QPointF s2 = m.map(w);
      if (i > 0) {
        const QPointF d = s2 - prev;
        const double len2 = d.x() * d.x() + d.y() * d.y();
        double tparam = 0.0;
        if (len2 > 1e-9)
          tparam = std::clamp((QPointF::dotProduct(sp - prev, d)) / len2, 0.0,
                              1.0);
        const QPointF c = prev + tparam * d;
        const double dx = c.x() - sp.x(), dy = c.y() - sp.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < best) {
          best = d2;
          if (guid) *guid = t.guid;
          if (name) *name = t.name;
          found = true;
        }
      }
      prev = s2;
    }
  }
  return found;
}

bool ChartCanvas::hitWaypointAt(const QPointF& sp, QString* guid,
                                QString* name) const {
  if (!m_nav_provider || !m_viewport) return false;
  constexpr double kPickRadiusPx = 12.0;
  const QMatrix4x4 m = m_viewport->transformMatrix(
      static_cast<int>(width()), static_cast<int>(height()));
  double best = kPickRadiusPx * kPickRadiusPx;
  bool found = false;
  for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
    if (!wp.visible) continue;
    const QPointF s = m.map(QPointF(wp.lon, Viewport::latToWorldY(wp.lat)));
    const double dx = s.x() - sp.x(), dy = s.y() - sp.y();
    const double d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      if (guid) *guid = wp.guid;
      if (name) *name = wp.name;
      found = true;
    }
  }
  return found;
}

void ChartCanvas::placeSimShipHere() {
  if (!m_sim_ship) return;
  // The test ship IS the live position source, so leave demo mode and make
  // sure the model is polled (mirrors the globals the sim writes into the
  // overlays + HUD + the route follower).
  if (m_demo_mode) setDemoMode(false);
  if (m_model_provider) m_model_provider->setModelPolling(true);
  m_sim_ship->place(m_ctx_lat, m_ctx_lon);
  if (m_viewport) {
    m_viewport->setCenter(m_ctx_lat, m_ctx_lon);
    emit viewChanged();
  }
  m_live_centered = true;  // we explicitly centred on the test ship
  update();
}

bool ChartCanvas::routeVisible(int index) const {
  if (!m_nav_provider) return false;
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size()) return false;
  return m_visible_routes.contains(rs[index].guid);
}

void ChartCanvas::setRouteVisible(int index, bool on) {
  if (!m_nav_provider) return;
  const QList<NavRoute> rs = m_nav_provider->userRoutes();
  if (index < 0 || index >= rs.size()) return;
  const QString guid = rs[index].guid;
  if (guid.isEmpty()) return;
  const bool had = m_visible_routes.contains(guid);
  if (had == on) return;
  if (on)
    m_visible_routes.insert(guid);
  else
    m_visible_routes.remove(guid);
  if (m_route_layer) m_route_layer->setVisibleRouteGuids(m_visible_routes);
  ++m_route_vis_rev;
  emit routeVisibilityChanged();
  update();
}

// --- Marks (free waypoints) -------------------------------------------------

QVariantMap ChartCanvas::viewBounds() const {
  QVariantMap m;
  if (!m_viewport) return m;
  double n = -90, s = 90, e = -180, w = 180;
  const int cw = qMax(1, static_cast<int>(width()));
  const int ch = qMax(1, static_cast<int>(height()));
  // Sample all four corners (rotation-safe).
  const double xs[2] = {0.0, static_cast<double>(cw)};
  const double ys[2] = {0.0, static_cast<double>(ch)};
  for (double sx : xs) {
    for (double sy : ys) {
      double lat = 0, lon = 0;
      m_viewport->screenToLatLon(sx, sy, cw, ch, lat, lon);
      n = qMax(n, lat);
      s = qMin(s, lat);
      e = qMax(e, lon);
      w = qMin(w, lon);
    }
  }
  m["north"] = n;
  m["south"] = s;
  m["east"] = e;
  m["west"] = w;
  return m;
}

void ChartCanvas::dropAnchorMark(double lat, double lon) {
  if (!m_nav_provider) return;
  const QString name =
      QStringLiteral("Anchor ") +
      QDateTime::currentDateTime().toString(QStringLiteral("dd MMM hh:mm"));
  const QString guid = m_nav_provider->dropMark(
      lat, lon, name, tr("Set by anchor watch"), QStringLiteral("anchor"));
  if (!guid.isEmpty()) {
    UndoOp op;
    op.created = true;
    op.guid = guid;
    op.snap = snapshotMark(guid);
    pushUndo(op);
  }
  update();
}

void ChartCanvas::dropMarkHere(const QString& name, const QString& comment,
                               const QString& icon) {
  if (!m_nav_provider) return;
  const QString guid =
      m_nav_provider->dropMark(m_ctx_lat, m_ctx_lon, name, comment, icon);
  if (!guid.isEmpty()) {
    UndoOp op;
    op.created = true;
    op.guid = guid;
    op.snap = snapshotMark(guid);
    pushUndo(op);
  }
  update();
}

// Instant menu-driven drops (wx parity): default icon, dated name; the
// mark dialog renames/re-icons afterwards if wanted.
static QString datedMarkName() {
  return QDateTime::currentDateTime().toString(
      QStringLiteral("MMddhhmmss"));
}

void ChartCanvas::dropMarkAtCursor() {
  if (!m_nav_provider) return;
  const QString guid =
      m_nav_provider->dropMark(m_cursor_pos_lat, m_cursor_pos_lon,
                               datedMarkName(), QString(),
                               QStringLiteral("circle"));
  if (!guid.isEmpty()) {
    UndoOp op;
    op.created = true;
    op.guid = guid;
    op.snap = snapshotMark(guid);
    pushUndo(op);
  }
  update();
}

void ChartCanvas::dropMarkAtBoat() {
  if (!m_nav_provider) return;
  const OwnShipState s = m_nav_provider->ownShip();
  if (!s.valid) return;
  const QString guid = m_nav_provider->dropMark(
      s.lat, s.lon, datedMarkName(), QString(), QStringLiteral("circle"));
  if (!guid.isEmpty()) {
    UndoOp op;
    op.created = true;
    op.guid = guid;
    op.snap = snapshotMark(guid);
    pushUndo(op);
  }
  update();
}

void ChartCanvas::dropMob() {
  if (!m_nav_provider) return;
  // Drop the MOB mark at the live own-ship fix; fall back to the view centre
  // when there's no fix (wx ActivateMOB drops at gLat/gLon).
  const OwnShipState s = m_nav_provider->ownShip();
  const double lat = s.valid ? s.lat : (m_viewport ? m_viewport->centerLat() : 0);
  const double lon = s.valid ? s.lon : (m_viewport ? m_viewport->centerLon() : 0);
  m_nav_provider->dropMark(lat, lon, tr("MOB"), QString(),
                           QStringLiteral("mob"));
  update();
}

void ChartCanvas::showMark(const QString& guid) {
  if (!m_nav_provider || !m_viewport) return;
  for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
    if (wp.guid != guid) continue;
    m_selected_waypoint_guid = guid;
    if (m_waypoint_layer) m_waypoint_layer->setSelectedWaypointGuid(guid);
    m_viewport->setCenter(wp.lat, wp.lon);  // centre (a point has no extent)
    emit viewChanged();
    update();
    return;
  }
}

void ChartCanvas::setMarkVisible(const QString& guid, bool on) {
  if (m_nav_provider) m_nav_provider->setWaypointVisible(guid, on);
  update();
}

void ChartCanvas::renameMark(const QString& guid, const QString& name) {
  if (m_nav_provider && !name.isEmpty())
    m_nav_provider->renameWaypoint(guid, name);
}

void ChartCanvas::setMarkComment(const QString& guid, const QString& comment) {
  if (m_nav_provider) m_nav_provider->setWaypointComment(guid, comment);
}

void ChartCanvas::setMarkIcon(const QString& guid, const QString& icon) {
  if (m_nav_provider) m_nav_provider->setWaypointIcon(guid, icon);
  update();
}

void ChartCanvas::deleteMark(const QString& guid) {
  if (!m_nav_provider) return;
  UndoOp op;
  op.created = false;
  op.guid = guid;
  op.snap = snapshotMark(guid);
  if (!op.snap.isEmpty()) pushUndo(op);
  m_nav_provider->deleteWaypoint(guid);
  if (guid == m_selected_waypoint_guid) {
    m_selected_waypoint_guid.clear();
    if (m_waypoint_layer) m_waypoint_layer->setSelectedWaypointGuid(QString());
  }
  update();
}

QStringList ChartCanvas::markIconNames() const {
  // The picker list is deduplicated: alias keys sharing one image (the wx
  // vocabulary, kept so persisted marks resolve) show a single tile.
  return pickerIconKeys();
}

// --- Tracks (own-vessel) ----------------------------------------------------

void ChartCanvas::resetTrack() {
  if (m_nav_provider) m_nav_provider->resetTrack();
  update();
}

void ChartCanvas::showTrack(const QString& guid) {
  if (!m_nav_provider) return;
  for (const NavTrack& t : m_nav_provider->tracks()) {
    if (t.guid != guid || t.points.isEmpty()) continue;
    m_selected_track_guid = guid;
    if (m_track_layer) m_track_layer->setSelectedTrackGuid(guid);
    double n = -90, s = 90, e = -180, w = 180;
    for (const QPointF& p : t.points) {  // (lon, lat)
      n = std::max(n, p.y());
      s = std::min(s, p.y());
      e = std::max(e, p.x());
      w = std::min(w, p.x());
    }
    fitBounds(n, s, e, w);
    return;
  }
}

void ChartCanvas::renameTrack(const QString& guid, const QString& name) {
  if (m_nav_provider && !name.isEmpty())
    m_nav_provider->renameTrack(guid, name);
}

void ChartCanvas::deleteTrack(const QString& guid) {
  if (m_nav_provider) m_nav_provider->deleteTrack(guid);
  if (guid == m_selected_track_guid) {
    m_selected_track_guid.clear();
    if (m_track_layer) m_track_layer->setSelectedTrackGuid(QString());
  }
  update();
}

void ChartCanvas::setTrackVisible(const QString& guid, bool on) {
  if (m_nav_provider) m_nav_provider->setTrackVisible(guid, on);
  update();
}

void ChartCanvas::renameRoute(int index, const QString& name) {
  if (m_nav_provider && !name.isEmpty()) m_nav_provider->renameRoute(index, name);
}

void ChartCanvas::deleteRoute(int index) {
  if (m_nav_provider) m_nav_provider->deleteRoute(index);
  clearRouteSelection();
}

void ChartCanvas::zoomIn() {
  const int w = static_cast<int>(width());
  const int h = static_cast<int>(height());
  m_viewport->zoomAt(w / 2.0, h / 2.0, 1.4, w, h);
}

void ChartCanvas::zoomOut() {
  const int w = static_cast<int>(width());
  const int h = static_cast<int>(height());
  m_viewport->zoomAt(w / 2.0, h / 2.0, 1.0 / 1.4, w, h);
}

void ChartCanvas::fitWorld() {
  if (!m_boundary_provider) return;
  const double n = m_boundary_provider->northLat();
  const double s = m_boundary_provider->southLat();
  const double e = m_boundary_provider->eastLon();
  const double w = m_boundary_provider->westLon();
  if (e <= w || n <= s) return;
  m_viewport->setCenter((n + s) / 2.0, (e + w) / 2.0);
  const double cw = width() > 0 ? width() : 1024.0;
  const double ch = height() > 0 ? height() : 720.0;
  m_viewport->setScale(std::min(cw / (e - w), ch / (n - s)) * 0.9);
}

void ChartCanvas::setScaleDenominator(double n) {
  if (!m_viewport) return;
  // Free-form like wx (mui_bar.cpp OnScaleSelected): clamp to a sane range.
  n = std::clamp(n, 1000.0, 3.0e6);
  // displayScaleN(scale, lat) = K / scale, so invert: scale = K / N, using the
  // same cos(centre-lat) factor so the entered 1:N matches the readout.
  const double clat =
      std::max(0.05, std::cos(m_viewport->centerLat() * M_PI / 180.0));
  const double scale = 111320.0 * clat * 3.78 * 1000.0 / n;
  m_viewport->setScale(scale);  // Viewport::changed -> update + viewChanged
}

void ChartCanvas::applyDisplaySettings(
    S52VectorChartProvider* provider) const {
  if (!provider) return;
  provider->setDisplayCategory(m_display_category);
  provider->setHiddenClasses(
      QSet<QString>(m_hidden_classes.cbegin(), m_hidden_classes.cend()));
  provider->setShowSoundings(m_show_soundings);
  provider->setShowText(m_show_text);
  provider->setShowLights(m_show_lights);
  provider->setShowBuoys(m_show_buoys);
  provider->setDetailScale(m_detail_scale);
  provider->setDeclutter(ChartConfig::instance().declutterText());  // P2.23a
  // Sounding display. The depth unit applies here so freshly-decoded SOUNDG
  // figures format correctly (a depth-unit *change* re-decodes via the
  // DisplayConfig handler, since obstruction soundings are baked by s52plib).
  // Safety-depth emphasis and the ENC sounding-size slider are live render-time
  // re-rasters; the slider's -5..+5 maps to 0.5x..1.5x (1 + 0.1*f), mirroring
  // wx m_SoundingsScaleFactor.
  provider->setDepthUnit(DisplayConfig::instance().depthUnit());
  provider->setSafetyDepth(OwnShipConfig::instance().safetyDepth());
  provider->setSoundingScale(
      1.0 + 0.1 * UIConfig::instance().encSoundingScaleFactor());
  // Over-scale hatch threshold: above the quilt's normal overzoom band
  // (m_overzoom_k), floored at wx's ~4x, so the hatch only marks genuine
  // overscale -- not the routine overzoom the quilt does after autoscaling.
  provider->setOverscaleThreshold(std::max(4.0, m_overzoom_k));
}

void ChartCanvas::setDetailScale(double n) {
  if (n <= 0.0 || n == m_detail_scale) return;
  m_detail_scale = n;
  ConfigStore::instance().setInt("display/detailScale", static_cast<int>(n));
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setDetailScale(n);
  emit detailScaleChanged();
  update();
}

void ChartCanvas::setOverzoomFactor(double k) {
  k = std::clamp(k, 1.0, 5.0);
  if (k == m_overzoom_k) return;
  m_overzoom_k = k;
  ConfigStore::instance().setDouble("display/overzoomFactor", k);
  // The over-zoom factor changes which charts the quilt selects, so re-run the
  // per-view selection (loads/evicts as needed). Cheap; only on a settings edit.
  // It also sets the over-scale hatch threshold (max(4,k)), so push that to the
  // resident providers too.
  const double thr = std::max(4.0, m_overzoom_k);
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setOverscaleThreshold(thr);
  if (!m_catalog.isEmpty()) updateVisibleCells();
  emit overzoomFactorChanged();
  update();
}

void ChartCanvas::setDisplayCategory(int cat) {
  if (cat == m_display_category) return;
  m_display_category = cat;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setDisplayCategory(cat);
  if (m_layer_config) m_layer_config->setValue("display/category", cat);
  emit displayCategoryChanged();
  update();
}

// The wx SetAnchorOn category set (s52plib.cpp:11270).
static const char* kAnchorClasses[] = {"ACHBRT", "ACHARE", "CBLSUB",
                                       "PIPARE", "PIPSOL", "TUNNEL",
                                       "SBDARE"};

void ChartCanvas::setShowEncAnchoring(bool on) {
  if (on == m_show_enc_anchoring) return;
  m_show_enc_anchoring = on;
  QStringList classes = m_hidden_classes;
  for (const char* c : kAnchorClasses) {
    if (on)
      classes.removeAll(QLatin1String(c));
    else if (!classes.contains(QLatin1String(c)))
      classes.append(QLatin1String(c));
  }
  if (m_layer_config)
    m_layer_config->setValue("display/showEncAnchoring", on);
  setHiddenObjectClasses(classes);  // re-renders + notifies
}

void ChartCanvas::scaleChartStep(int dir) {
  if (!m_viewport || dir == 0) return;
  const double clat = m_viewport->centerLat();
  const double clon = m_viewport->centerLon();
  const double curN = displayScaleN(m_viewport->scale(), clat);
  // Distinct native scales of catalogued cells charting the view centre.
  QList<int> scales;
  for (auto it = m_catalog.cbegin(); it != m_catalog.cend(); ++it) {
    const CellExtent& c = it.value();
    if (c.nativeScale > 0 && c.navFeatures > 0 && c.covers(clat, clon) &&
        !scales.contains(c.nativeScale))
      scales.append(c.nativeScale);
  }
  if (scales.isEmpty()) return;
  std::sort(scales.begin(), scales.end());
  int target = 0;
  if (dir > 0) {  // larger scale = finer = smaller 1:N
    for (int i = scales.size() - 1; i >= 0; --i)
      if (scales[i] < curN * 0.9) { target = scales[i]; break; }
  } else {  // smaller scale = coarser = bigger 1:N
    for (int i = 0; i < scales.size(); ++i)
      if (scales[i] > curN * 1.1) { target = scales[i]; break; }
  }
  if (target <= 0) return;
  // Same autoscale math as selectChart (scale = K*cos(lat)/N).
  constexpr double kK = 111320.0 * 3.78 * 1000.0;
  const double cl = std::max(0.05, std::cos(clat * M_PI / 180.0));
  m_viewport->setScale(kK * cl / target);
  emit viewChanged();
  update();
}

void ChartCanvas::setHiddenObjectClasses(const QStringList& classes) {
  QStringList norm = classes;
  norm.removeDuplicates();
  norm.sort();
  if (norm == m_hidden_classes) return;
  m_hidden_classes = norm;
  const QSet<QString> hidden(norm.cbegin(), norm.cend());
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setHiddenClasses(hidden);
  if (m_layer_config)
    m_layer_config->setValue("display/hiddenClasses", norm.join(','));
  emit hiddenObjectClassesChanged();
  update();
}

QVariantList ChartCanvas::s57ClassCatalog() const {
  QVariantList out;
  const S57Dictionary& dict = S57Dictionary::instance();
  for (const QString& acr : dict.classAcronyms()) {
    QVariantMap row;
    row["acronym"] = acr;
    const QString desc = dict.className(acr);
    row["description"] = desc.isEmpty() ? acr : desc;
    out.append(row);
  }
  return out;
}

void ChartCanvas::setShowSoundings(bool on) {
  if (on == m_show_soundings) return;
  m_show_soundings = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowSoundings(on);
  if (m_layer_config) m_layer_config->setValue("display/soundings", on);
  emit showSoundingsChanged();
  update();
}

void ChartCanvas::setShowText(bool on) {
  if (on == m_show_text) return;
  m_show_text = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowText(on);
  if (m_layer_config) m_layer_config->setValue("display/text", on);
  emit showTextChanged();
  update();
}

void ChartCanvas::setShowLights(bool on) {
  if (on == m_show_lights) return;
  m_show_lights = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowLights(on);
  if (m_layer_config) m_layer_config->setValue("display/lights", on);
  emit showLightsChanged();
  update();
}

void ChartCanvas::setShowBuoys(bool on) {
  if (on == m_show_buoys) return;
  m_show_buoys = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowBuoys(on);
  if (m_layer_config) m_layer_config->setValue("display/buoys", on);
  emit showBuoysChanged();
  update();
}

void ChartCanvas::setDemoMode(bool on) {
  if (on == m_demo_mode) return;
  m_demo_mode = on;
  ConfigStore::instance().setBool("display/demoMode", on);  // opt-in, persisted
  // Demo = the Hakefjord NMEA-log replay (a self-contained sample feed);
  // live = the real model fed by user connections. The two are mutually
  // exclusive: in demo the live model poll is stopped (real feeds are not
  // shown); in live the replay is stopped (no Hakefjord).
  if (m_nav_provider) m_nav_provider->setLive(!on);
  if (m_demo_provider) m_demo_provider->setRunning(on);
  if (m_model_provider) m_model_provider->setModelPolling(!on);
  m_live_centered = false;  // recentre on the new source's first fix
  // Track recording is left to the user (toolbar toggle, #29); it records
  // off whichever own-ship fix is active.
  emit demoModeChanged();
  update();
}

QSGNode* ChartCanvas::updatePaintNode(QSGNode* old_node,
                                      UpdatePaintNodeData* /*update_data*/) {
  QSGNode* root = old_node;
  if (!root) {
    root = new QSGNode();
    m_world_anchored_root = new QSGTransformNode();
    root->appendChildNode(m_world_anchored_root);
    m_display_anchored_root = new QSGTransformNode();
    root->appendChildNode(m_display_anchored_root);
  }

  // World-anchored root: the Viewport transform. Pan/zoom mutates this
  // one matrix and all world-anchored Layer subtrees follow.
  if (m_world_anchored_root) {
    m_world_anchored_root->setMatrix(
        m_viewport->transformMatrix(static_cast<int>(width()),
                                    static_cast<int>(height())));
  }
  // Display-anchored root stays identity.

  m_compositor->syncToScene(m_world_anchored_root, m_display_anchored_root,
                            window());

  // Debug perf readout: time between paints -> smoothed fps + last frame ms.
  // Measured here on the render thread; published to the GUI thread (throttled)
  // so the QML HUD can bind to perfText. Only meaningful while frames are
  // actually being produced (idle = render-on-demand, so it freezes).
  if (m_frame_clock.isValid()) {
    const double dt_ms = m_frame_clock.nsecsElapsed() / 1.0e6;
    m_frame_clock.restart();
    if (dt_ms > 0.0) {
      const double inst = 1000.0 / dt_ms;
      m_fps_smooth =
          m_fps_smooth > 0.0 ? (0.85 * m_fps_smooth + 0.15 * inst) : inst;
      if (++m_frame_count % 10 == 0) {  // publish ~once per 10 frames
        const QString txt = QString::asprintf("%.0f fps  %.1f ms",
                                              m_fps_smooth, dt_ms);
        QMetaObject::invokeMethod(
            this,
            [this, txt]() {
              if (m_perf_text == txt) return;
              m_perf_text = txt;
              emit perfTextChanged();
            },
            Qt::QueuedConnection);
      }
    }
  } else {
    m_frame_clock.start();
  }
  return root;
}

void ChartCanvas::mousePressEvent(QMouseEvent* event) {
  // Clicking the chart claims keyboard focus for the canvas key layer
  // (P3.20) -- e.g. back from a settings text field.
  forceActiveFocus();
  // Route-building mode (Create Route). Left-press begins a gesture that is a
  // PAN if the cursor moves, or places a vertex if it stays put -- decided on
  // release (reusing the click/drag threshold) so the chart stays fully
  // pannable (and wheel-zoomable) while drawing. Right finishes the route.
  if (m_route_build_mode && m_nav_provider) {
    if (event->button() == Qt::LeftButton) {
      m_dragging = true;
      m_drag_last_pos = event->position();
      m_press_pos = event->position();
    } else if (event->button() == Qt::RightButton) {
      m_nav_provider->finishRoute();
      m_route_build_mode = false;
      emit routeBuildModeChanged();
      update();
    }
    event->accept();
    return;
  }

  if (event->button() == Qt::LeftButton) {
    // Route editing: grabbing a node selects its route and starts a drag.
    int rt = -1, nd = -1;
    if (m_route_edit_mode && hitRouteNode(event->position(), rt, nd)) {
      selectRoute(rt);
      m_dragging_node = true;
      m_drag_node = nd;
      event->accept();
      return;
    }
    m_dragging = true;
    m_drag_last_pos = event->position();
    m_press_pos = event->position();
    event->accept();
  } else if (event->button() == Qt::RightButton) {
    // Record the world point under the cursor first -- every menu's actions
    // (drop mark / navigate-to / insert point / center here) consume it.
    m_ctx_pos = event->position();
    if (m_viewport)
      m_viewport->screenToLatLon(m_ctx_pos.x(), m_ctx_pos.y(),
                                 static_cast<int>(width()),
                                 static_cast<int>(height()), m_ctx_lat,
                                 m_ctx_lon);

    // Right-click on a route node in edit mode -> the node menu (delete
    // point/route) takes precedence over the object menus.
    int rt = -1, nd = -1;
    if (m_route_edit_mode && hitRouteNode(event->position(), rt, nd)) {
      selectRoute(rt);
      m_menu_route = rt;
      m_menu_node = nd;
      emit routeNodeMenuRequested(event->position().x(),
                                    event->position().y());
      event->accept();
      return;
    }

    // Object-focused menus (P3.18, wx CanvasMenuHandler): an AIS target,
    // then a mark, then a route node / segment, else the general menu.
    int ais_mmsi = 0;
    QString ais_name;
    if (hitAisAt(event->position(), &ais_mmsi, &ais_name)) {
      emit aisMenuRequested(event->position().x(), event->position().y(),
                              ais_mmsi, ais_name);
      event->accept();
      return;
    }
    QString wp_guid, wp_name;
    if (hitWaypointAt(event->position(), &wp_guid, &wp_name)) {
      emit markMenuRequested(event->position().x(), event->position().y(),
                               wp_guid, wp_name);
      event->accept();
      return;
    }
    int seg = -1;
    double ilat = 0, ilon = 0;
    if (hitRouteNode(event->position(), rt, nd)) {
      selectRoute(rt);
      m_menu_route = rt;
      m_menu_seg = -1;
      const bool act = m_nav_provider &&
                       m_nav_provider->userRoutes().value(rt).active;
      emit routeMenuRequested(event->position().x(), event->position().y(),
                                rt, act, false);
      event->accept();
      return;
    }
    if (hitRouteSegment(event->position(), rt, seg, ilat, ilon)) {
      selectRoute(rt);
      m_menu_route = rt;
      m_menu_seg = seg;
      m_menu_ins_lat = ilat;
      m_menu_ins_lon = ilon;
      const bool act = m_nav_provider &&
                       m_nav_provider->userRoutes().value(rt).active;
      emit routeMenuRequested(event->position().x(), event->position().y(),
                                rt, act, true);
      event->accept();
      return;
    }
    QString trk_guid, trk_name;
    if (hitTrackAt(event->position(), &trk_guid, &trk_name)) {
      emit trackMenuRequested(event->position().x(), event->position().y(),
                                trk_guid, trk_name);
      event->accept();
      return;
    }

    emit contextMenuRequested(m_ctx_pos.x(), m_ctx_pos.y());
    event->accept();
  } else {
    QQuickItem::mousePressEvent(event);
  }
}

void ChartCanvas::centerViewHere() {
  if (!m_viewport) return;
  m_viewport->setCenter(m_ctx_lat, m_ctx_lon);
  emit viewChanged();
  update();
}

void ChartCanvas::setAisTrail(int mmsi, bool on) {
  if (m_ais_layer) m_ais_layer->setTrailEnabled(mmsi, on);
  update();
}

bool ChartCanvas::aisTrailEnabled(int mmsi) const {
  return m_ais_layer && m_ais_layer->trailEnabled(mmsi);
}

void ChartCanvas::queryObjectsHere() {
  // Populate the object-query view-model from the chart objects under the
  // right-click point; the QML object-query window binds to it.
  pickObjectsAt(m_ctx_pos);
}

void ChartCanvas::mouseMoveEvent(QMouseEvent* event) {
  if (m_dragging_node && m_nav_provider) {
    double lat = 0, lon = 0;
    const QPointF p = event->position();
    m_viewport->screenToLatLon(p.x(), p.y(), static_cast<int>(width()),
                               static_cast<int>(height()), lat, lon);
    m_nav_provider->moveRoutePoint(m_selected_route, m_drag_node, lat, lon);
    checkEdgePan(p);  // dragging a node toward an edge scrolls the view
    event->accept();
    return;
  }
  if (m_dragging) {
    const QPointF pos = event->position();
    const QPointF delta = pos - m_drag_last_pos;
    m_drag_last_pos = pos;
    m_viewport->panBy(delta.x(), delta.y());
    event->accept();
    return;
  }
  QQuickItem::mouseMoveEvent(event);
}

void ChartCanvas::mouseReleaseEvent(QMouseEvent* event) {
  // Route-building: a left-release that barely moved places a vertex; one that
  // moved was a pan (already applied live in mouseMoveEvent) -- no vertex.
  if (m_route_build_mode && event->button() == Qt::LeftButton) {
    const bool was_press = m_dragging;
    m_dragging = false;
    const QPointF d = event->position() - m_press_pos;
    if (was_press && d.manhattanLength() <= 6 && m_nav_provider && m_viewport) {
      double lat = 0, lon = 0;
      m_viewport->screenToLatLon(event->position().x(), event->position().y(),
                                 static_cast<int>(width()),
                                 static_cast<int>(height()), lat, lon);
      // Nearby-waypoint snap (P3.13, wx GetNearbyWaypoint): clicking within
      // pick range of an existing mark places the vertex exactly on it.
      QString snap_guid;
      if (hitWaypointAt(event->position(), &snap_guid, nullptr)) {
        for (const NavWaypoint& wp : m_nav_provider->waypoints()) {
          if (wp.guid == snap_guid) {
            lat = wp.lat;
            lon = wp.lon;
            break;
          }
        }
      }
      m_nav_provider->addRoutePoint(lat, lon);
    }
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && m_dragging_node) {
    m_dragging_node = false;
    m_drag_node = -1;
    if (m_nav_provider) m_nav_provider->commitRouteEdit();  // manager refresh
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && m_dragging) {
    m_dragging = false;
    // A press+release that barely moved is a click. Route interactions take
    // precedence: click a segment of the selected route to insert a point,
    // click another route's line to select it, else AIS pick / deselect.
    const QPointF d = event->position() - m_press_pos;
    if (d.manhattanLength() <= 6) {
      // Measure mode (P3.18): a click drops a measure point; everything else
      // (selection, AIS pick) is suspended while measuring.
      if (m_measure_active && m_viewport) {
        double mlat = 0, mlon = 0;
        m_viewport->screenToLatLon(event->position().x(),
                                   event->position().y(),
                                   static_cast<int>(width()),
                                   static_cast<int>(height()), mlat, mlon);
        m_measure_pts.append(QPointF(mlon, mlat));
        updateMeasure(mlat, mlon, false);
        event->accept();
        return;
      }
      int rt = -1, seg = -1;
      double ilat = 0, ilon = 0;
      if (hitRouteSegment(event->position(), rt, seg, ilat, ilon)) {
        if (m_route_edit_mode && rt == m_selected_route && m_nav_provider)
          m_nav_provider->insertRoutePoint(rt, seg, ilat, ilon);
        else
          selectRoute(rt);
      } else if (!pickAisAt(event->position())) {
        // Tides on? try a tide/current station before deselecting.
        if (!(DisplayConfig::instance().showTides() &&
              pickTideStationAt(event->position())))
          clearRouteSelection();  // clicked empty water -> leave edit
      }
    }
    event->accept();
  } else {
    QQuickItem::mouseReleaseEvent(event);
  }
}

void ChartCanvas::checkEdgePan(const QPointF& pos) {
  // 5%-margin band, 2%-of-dimension step per tick (wx CheckEdgePan(…,5,2)).
  const double w = width(), h = height();
  if (w <= 0 || h <= 0) return;
  const double mx = w * 0.05, my = h * 0.05;
  QPointF step(0, 0);
  // panBy takes a mouse-drag delta: cursor at the LEFT edge should reveal
  // more west = drag right = +x.
  if (pos.x() < mx)
    step.setX(w * 0.02);
  else if (pos.x() > w - mx)
    step.setX(-w * 0.02);
  if (pos.y() < my)
    step.setY(h * 0.02);
  else if (pos.y() > h - my)
    step.setY(-h * 0.02);
  if (step.isNull()) {
    m_edge_pan_timer->stop();
    return;
  }
  m_edge_pan_step = step;
  if (!m_edge_pan_timer->isActive()) m_edge_pan_timer->start();
}

void ChartCanvas::keyPressEvent(QKeyEvent* event) {
  // Arrow-pan step (logical px), matching a comfortable wx scroll notch.
  constexpr double kPanStep = 80.0;
  if (!m_viewport) {
    QQuickItem::keyPressEvent(event);
    return;
  }
  switch (event->key()) {
    // panBy takes a mouse-drag delta: dragging right reveals west, dragging
    // down reveals north -- so an arrow pans the VIEW toward its direction.
    case Qt::Key_Left:
      m_viewport->panBy(kPanStep, 0);
      break;
    case Qt::Key_Right:
      m_viewport->panBy(-kPanStep, 0);
      break;
    case Qt::Key_Up:
      m_viewport->panBy(0, kPanStep);
      break;
    case Qt::Key_Down:
      m_viewport->panBy(0, -kPanStep);
      break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      zoomIn();
      break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
      zoomOut();
      break;
    case Qt::Key_Z:
      // Cmd/Ctrl-Z undo, Shift-Cmd/Ctrl-Z redo (P3.18 tier 4).
      if (event->modifiers() & Qt::ControlModifier) {
        (event->modifiers() & Qt::ShiftModifier) ? redo() : undo();
        break;
      }
      QQuickItem::keyPressEvent(event);
      return;
    case Qt::Key_M:  // wx: M / F4 toggles the measure tool
    case Qt::Key_F4:
      m_measure_active ? stopMeasure() : startMeasure();
      break;
    case Qt::Key_Escape:
      // Cancel the transient mode, most specific first (wx parity).
      if (m_measure_active) {
        stopMeasure();
      } else if (m_route_build_mode) {
        setRouteBuildMode(false);  // discards the draft
      } else {
        clearRouteSelection();
      }
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      // Finish the route being built (same as the right-click finish).
      if (m_route_build_mode && m_nav_provider) {
        m_nav_provider->finishRoute();
        m_route_build_mode = false;
        emit routeBuildModeChanged();
        update();
      }
      break;
    default:
      QQuickItem::keyPressEvent(event);
      return;
  }
  event->accept();
}

void ChartCanvas::hoverMoveEvent(QHoverEvent* event) {
  if (m_viewport) {
    double lat = 0, lon = 0;
    const QPointF p = event->position();
    m_viewport->screenToLatLon(p.x(), p.y(), static_cast<int>(width()),
                               static_cast<int>(height()), lat, lon);
    m_cursor_text = DisplayConfig::instance().formatLatLon(lat, lon);
    // Bearing + range from own ship to the cursor (wx STAT_FIELD_CURSOR_BRGRNG).
    m_cursor_brgrng_text.clear();
    if (m_nav_provider) {
      const OwnShipState s = m_nav_provider->ownShip();
      if (s.valid) {
        double brg = 0.0, rng = 0.0;
        DistanceBearingMercator(lat, lon, s.lat, s.lon, &brg, &rng);
        DisplayConfig& dc = DisplayConfig::instance();
        m_cursor_brgrng_text =
            dc.formatBearing(brg) + QStringLiteral("  ") + dc.formatDistance(rng);
      }
    }
    m_cursor_pos_lat = lat;
    m_cursor_pos_lon = lon;
    emit cursorMoved();
    // Live rubber-band segment to the cursor while drawing a route.
    if (m_route_build_mode && m_nav_provider)
      m_nav_provider->setRouteRubberband(lat, lon);
    // Measure tool (P3.18): trail the dashed rubber-band + live readout.
    if (m_measure_active && !m_measure_pts.isEmpty())
      updateMeasure(lat, lon, true);
    // Edge auto-pan while building / measuring (P3.13).
    if (m_route_build_mode || m_measure_active)
      checkEdgePan(p);
    else if (m_edge_pan_timer->isActive())
      m_edge_pan_timer->stop();
  }
  QQuickItem::hoverMoveEvent(event);
}

void ChartCanvas::setRouteBuildMode(bool on) {
  if (on == m_route_build_mode) return;
  m_route_build_mode = on;
  if (m_nav_provider) {
    if (on)
      m_nav_provider->beginRoute();  // start a fresh draft
    else
      m_nav_provider->cancelRoute();  // toggled off -> discard draft
  }
  emit routeBuildModeChanged();
  update();
}

void ChartCanvas::setColorScheme(int scheme) {
  if (scheme < 0 || scheme > 2 || scheme == m_color_scheme) return;
  m_color_scheme = scheme;
  ConfigStore::instance().setInt("display/colorScheme", scheme);

  // Re-tint the world basemap + route lines immediately (cheap rebuilds).
  if (m_basemap) m_basemap->setColorScheme(scheme);
  if (m_route_layer) m_route_layer->setColorScheme(scheme);

  // S-52 cells bake their colours in at decode time, so switch the palette on
  // the decode thread (queued before the loadCell re-requests so the worker
  // applies it first), then re-decode the resident cells.
  if (m_worker) {
    QMetaObject::invokeMethod(m_worker, "setColorScheme", Qt::QueuedConnection,
                              Q_ARG(int, scheme));
    reloadResidentCells();
  }

  emit colorSchemeChanged();
  update();
}

void ChartCanvas::applyChartConfig() {
  if (!m_worker) return;
  const ChartConfig& c = ChartConfig::instance();
  ChartDisplaySettings s;
  s.importantTextOnly = c.importantTextOnly();
  s.useScamin = c.reducedDetailSmallScale();
  s.symbolStyle = c.graphicsStyle();
  s.boundaryStyle = c.boundaryStyle();
  s.twoShades = c.colourCount();
  s.safetyContour = c.safetyContour();
  s.shallowContour = c.shallowContour();
  s.deepContour = c.deepContour();
  m_height_unit = DisplayConfig::instance().heightUnit();
  s.heightUnit = m_height_unit;
  m_depth_unit = DisplayConfig::instance().depthUnit();
  s.depthUnit = m_depth_unit;
  s.chartInfoObjects = c.chartInfoObjects();          // P2.16
  s.dataQuality = c.dataQuality();                    // CATZOC overlay
  s.buoyLightLabels = c.buoyLightLabels();            // P2.16
  s.lightDescriptions = c.lightDescriptions();        // P2.16
  s.extendedLightSectors = c.extendedLightSectors();  // P2.16
  s.nationalText = c.nationalText();                  // P2.16
  s.declutterText = c.declutterText();                // P2.16
  s.superScamin = c.superScamin();                    // P2.16
  // Apply on the decode thread (queued before the re-requests), then re-decode
  // resident cells so the new symbology/shading bakes in.
  QMetaObject::invokeMethod(m_worker, "applyDisplaySettings",
                            Qt::QueuedConnection,
                            Q_ARG(ocpn::qtui::ChartDisplaySettings, s));
  reloadResidentCells();
  update();
}

// Evict every resident S-52 cell layer and re-run the quilt so they re-decode
// with the current global s52plib settings (palette / display options). Shared
// by setColorScheme and applyChartConfig.
void ChartCanvas::reloadResidentCells() {
  QList<QString> resident;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().extent.valid()) resident.append(it.key());  // skip demo
  for (const QString& name : resident) {
    m_compositor->removeLayer(m_loaded.value(name).layerId);
    m_loaded.remove(name);
    m_requested.remove(name);
  }
  m_needed.clear();
  // New settings/palette: a cell that was content-less before may now render
  // (e.g. SCAMIN off), so forget the known-empty set and let it re-decode. The
  // worker flushes its decoded-buffer cache on the same setting change.
  m_known_empty.clear();
  if (!m_catalog.isEmpty()) m_load_debounce->start();
}

void ChartCanvas::setTrackRecording(bool on) {
  if (on == m_track_recording) return;
  m_track_recording = on;
  if (m_nav_provider) m_nav_provider->setRecordingTrack(on);
  emit trackRecordingChanged();
  update();
}

bool ChartCanvas::pickAisAt(const QPointF& screen_pos) {
  if (!m_ais_selection || !m_nav_provider || !m_viewport) return false;
  // World->screen transform the canvas applies to the world-anchored root,
  // so target screen px = transform.map(world(lon, -lat)).
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  constexpr double kPickRadiusPx = 14.0;
  double best = kPickRadiusPx * kPickRadiusPx;
  const AisTarget* hit = nullptr;
  const QList<AisTarget> targets = m_nav_provider->aisTargets();
  for (const AisTarget& t : targets) {
    const QPointF sp = m.map(QPointF(t.lon, Viewport::latToWorldY(t.lat)));
    const double dx = sp.x() - screen_pos.x();
    const double dy = sp.y() - screen_pos.y();
    const double d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      hit = &t;
    }
  }
  if (hit) {
    m_ais_selection->select(*hit);
    return true;
  }
  m_ais_selection->clear();
  return false;
}

bool ChartCanvas::pickTideStationAt(const QPointF& screen_pos) {
  if (!m_tide_graph || !m_viewport || !ptcmgr || !ptcmgr->IsReady())
    return false;
  const QMatrix4x4 m = m_viewport->transformMatrix(static_cast<int>(width()),
                                                   static_cast<int>(height()));
  constexpr double kPickRadiusPx = 13.0;
  double best = kPickRadiusPx * kPickRadiusPx;
  int hit = -1;
  for (int i = 0; i <= ptcmgr->Get_max_IDX(); ++i) {
    const IDX_entry* e = ptcmgr->GetIDX_entry(i);
    if (!e || !e->IDX_Useable) continue;
    const char ty = e->IDX_type;
    if (ty != 't' && ty != 'T' && ty != 'c' && ty != 'C') continue;
    // Skip the deeper records of multi-depth current stations so the pick (and
    // hence the graph) lands on the shallowest, most-usable record -- matching
    // what TideLayer draws and the legacy wx RebuildCurrentSelectList.
    if ((ty == 'c' || ty == 'C') && e->b_skipTooDeep) continue;
    const QPointF sp =
        m.map(QPointF(e->IDX_lon, Viewport::latToWorldY(e->IDX_lat)));
    const double dx = sp.x() - screen_pos.x();
    const double dy = sp.y() - screen_pos.y();
    const double d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      hit = i;
    }
  }
  if (hit >= 0) {
    m_tide_graph->select(hit);
    return true;
  }
  return false;
}

void ChartCanvas::pickObjectsAt(const QPointF& screen_pos) {
  if (!m_object_query || !m_viewport) return;
  double lat = 0, lon = 0;
  m_viewport->screenToLatLon(screen_pos.x(), screen_pos.y(),
                             static_cast<int>(width()),
                             static_cast<int>(height()), lat, lon);
  // ~10px pick radius in degrees at the current zoom (px/degree).
  const double margin =
      m_viewport->scale() > 0.0 ? 10.0 / m_viewport->scale() : 0.0;
  // The quilt loads several overlapping cells (usage bands), each holding the
  // same features, so de-duplicate by LNAM (the S-57 unique feature id) --
  // the same real-world object across cells collapses to one result.
  QList<s52sg::QueryObject> found;
  QSet<QString> seen;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it) {
    if (!it.value().provider) continue;
    for (const s52sg::QueryObject& qo :
         it.value().provider->objectsAt(lat, lon, margin)) {
      QString lnam;
      for (const s52sg::QueryAttr& a : qo.attrs)
        if (a.name == QLatin1String("LNAM")) {
          lnam = a.value;
          break;
        }
      if (!lnam.isEmpty()) {
        if (seen.contains(lnam)) continue;  // same feature, another cell
        seen.insert(lnam);
      }
      found.append(qo);
    }
  }
  m_object_query->setObjects(found);
}

// --- Overlay layer visibility (P3.7) ----------------------------------------
namespace {
bool layerVisible(LayerCompositor* c, const char* id) {
  Layer* l = c ? c->layer(QString::fromLatin1(id)) : nullptr;
  return l ? l->visible() : true;
}
void setLayerVisible(LayerCompositor* c, const char* id, bool on) {
  if (Layer* l = c ? c->layer(QString::fromLatin1(id)) : nullptr)
    l->setVisible(on);
}
}  // namespace

bool ChartCanvas::showRoutes() const {
  return layerVisible(m_compositor.get(), "core.routes");
}
void ChartCanvas::setShowRoutes(bool on) {
  setLayerVisible(m_compositor.get(), "core.routes", on);
  emit overlayVisibilityChanged();
  update();
}
bool ChartCanvas::showTracks() const {
  return layerVisible(m_compositor.get(), "core.tracks");
}
void ChartCanvas::setShowTracks(bool on) {
  setLayerVisible(m_compositor.get(), "core.tracks", on);
  emit overlayVisibilityChanged();
  update();
}
bool ChartCanvas::showWaypoints() const {
  return layerVisible(m_compositor.get(), "core.waypoints");
}
void ChartCanvas::setShowWaypoints(bool on) {
  setLayerVisible(m_compositor.get(), "core.waypoints", on);
  emit overlayVisibilityChanged();
  update();
}

QString ChartCanvas::scaleText() const {
  if (!m_viewport || m_viewport->scale() <= 0.0) return QString();
  const double n = displayScaleN(m_viewport->scale(), m_viewport->centerLat());
  return QStringLiteral("1:%1").arg(static_cast<qlonglong>(n));
}

void ChartCanvas::setFollowOwnShip(bool on) {
  if (on == m_follow_own_ship) return;
  m_follow_own_ship = on;
  if (on && m_nav_provider) {  // jump to the ship immediately
    const OwnShipState s = m_nav_provider->ownShip();
    if (s.valid) m_viewport->setCenter(s.lat, s.lon);
  }
  emit followOwnShipChanged();
  update();
}

double ChartCanvas::chartRotationDeg() const {
  return m_viewport ? m_viewport->rotation() * 180.0 / M_PI : 0.0;
}

void ChartCanvas::updateChartRotation() {
  if (!m_viewport) return;
  const int mode = DisplayConfig::instance().navMode();  // 0 N, 1 Course, 2 Head
  if (mode == 0) {  // North-Up
    m_cog_avg_valid = false;
    m_viewport->setRotation(0.0);
    emit viewChanged();
    return;
  }
  const OwnShipState s =
      m_nav_provider ? m_nav_provider->ownShip() : OwnShipState{};
  if (!s.valid) return;  // keep the last rotation until a fix arrives

  double heading_deg = -1.0;
  if (mode == 2) {  // Head-Up: live heading (fall back to COG if no HDT)
    heading_deg = (s.hdg < 360.0) ? s.hdg : s.cog;
    m_cog_avg_valid = false;
  } else {  // Course-Up: circular-smoothed COG over the averaging window
    const double tau = DisplayConfig::instance().chartRotationAveraging();
    if (tau <= 0.0 || !m_cog_avg_valid) {
      m_cog_avg = s.cog;
      m_cog_avg_valid = true;
    } else {
      // Assume ~1 Hz fixes: alpha ~ 1/tau, clamped. Shortest-arc blend.
      const double alpha = std::clamp(1.0 / tau, 0.02, 1.0);
      double diff = s.cog - m_cog_avg;
      while (diff > 180.0) diff -= 360.0;
      while (diff < -180.0) diff += 360.0;
      m_cog_avg += alpha * diff;
      while (m_cog_avg >= 360.0) m_cog_avg -= 360.0;
      while (m_cog_avg < 0.0) m_cog_avg += 360.0;
    }
    heading_deg = m_cog_avg;
  }
  if (heading_deg < 0.0) return;
  // Rotate the chart so the heading/course points up (mirrors wx: rotation =
  // -heading). The viewport stores radians.
  m_viewport->setRotation(-heading_deg * M_PI / 180.0);
  emit viewChanged();
}

void ChartCanvas::fitBounds(double north, double south, double east,
                            double west) {
  if (!m_viewport) return;
  // Pad a near-zero span (single waypoint) so a "zoom to" lands at a sensible
  // harbour scale rather than infinite zoom.
  constexpr double kMinSpan = 0.05;  // degrees
  if (north - south < kMinSpan) {
    const double c = (north + south) / 2.0;
    north = c + kMinSpan / 2.0;
    south = c - kMinSpan / 2.0;
  }
  if (east - west < kMinSpan) {
    const double c = (east + west) / 2.0;
    east = c + kMinSpan / 2.0;
    west = c - kMinSpan / 2.0;
  }
  m_viewport->setCenter((north + south) / 2.0, (east + west) / 2.0);
  const double w = width() > 0 ? width() : 1000.0;
  const double h = height() > 0 ? height() : 700.0;
  const double sLon = w / ((east - west) * 1.25);
  const double sLat = h / ((north - south) * 1.25);
  m_viewport->setScale(std::min(sLon, sLat));
  update();
}

void ChartCanvas::wheelEvent(QWheelEvent* event) {
  // Vertical wheel: zoom. 120 units = one notch on a normal mouse wheel.
  // Trackpads use smaller pixelDelta values; both feed into angleDelta.
  const int deg = event->angleDelta().y() / 8;  // degrees (eighths)
  if (deg == 0) {
    QQuickItem::wheelEvent(event);
    return;
  }
  // Each notch (15°) zooms by the user's wheel-zoom factor (Display options;
  // 1.1 gentle .. 2.0 brisk). The exponent normalises trackpad/fine scrolls.
  const double per_notch =
      std::max(1.01, DisplayConfig::instance().wheelZoomFactor());
  const double factor = std::pow(per_notch, deg / 15.0);
  const QPointF p = event->position();
  m_viewport->zoomAt(p.x(), p.y(), factor,
                     static_cast<int>(width()),
                     static_cast<int>(height()));
  event->accept();
}

}  // namespace ocpn::qtui
