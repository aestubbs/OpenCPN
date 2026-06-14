# gui/qt -- new Qt-Quick chart-canvas prototype.
#
# Builds the `opencpn-qt` executable: a minimal QGuiApplication +
# QQmlApplicationEngine that hosts a ChartCanvas QQuickItem. Sibling
# to the legacy wx-based `OpenCPN` target; replaces it in Phase 3.
#
# See docs/QT_MIGRATION_TASKS.md Phase 2 for the three-tier scene-graph
# plan this implements.

# The plugin-facing layer toolkit (shared library; see toolkit/).
add_subdirectory(toolkit)

qt_add_executable(opencpn-qt
  main.cpp
)

# Custom AA-line shaders (P2.4 #5 / P2.13): baked to .qsb via qsb and exposed
# as Qt resources at :/shaders/aaline.{vert,frag}.qsb (see aa_line.cpp).
find_package(Qt6 REQUIRED COMPONENTS ShaderTools SerialPort Multimedia Svg
                                      LinguistTools)
# Shader FILES are absolute: qt_add_translations defers an IDE source-expose
# step to the END of the TOP-LEVEL directory scope, where relative paths
# would (wrongly) resolve against the top-level source dir.
qt_add_shaders(opencpn-qt "opencpn_qt_shaders"
  PREFIX "/"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/aaline.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/aaline.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/coastshade.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/coastshade.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/areapattern.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/areapattern.frag
)

qt_add_qml_module(opencpn-qt
  URI opencpn.qt
  VERSION 1.0
  QML_FILES
    qml/AppMenuBar.qml
    qml/AppMenuBarInWindow.qml
    qml/MenuModel.qml
    qml/Main.qml
    qml/FloatToolbar.qml
    qml/MuiBar.qml
    qml/ToolbarFlyout.qml
    qml/OptionsWindow.qml
    qml/VectorDetailList.qml
    qml/ScaleDialog.qml
    qml/MarkEditorDialog.qml
    qml/RouteDetailsWindow.qml
    qml/ObjectQueryWindow.qml
    qml/AboutWindow.qml
    qml/DataMonitorWindow.qml
    qml/CanvasOptionsDrawer.qml
    qml/RouteManagerDrawer.qml
    qml/TideGraphPanel.qml
    qml/TideTimeBar.qml
    qml/VesselHud.qml
    qml/ChartContextMenus.qml
    qml/AisTargetListWindow.qml
    qml/ConfirmDialog.qml
    qml/SendToPeerDialog.qml
    qml/SendToGpsDialog.qml
        qml/PrioritiesDialog.qml
  SOURCES
    app_controller.h
    chart_canvas.h
    chart_canvas.cpp
    layer_compositor.h
    layer_compositor.cpp
    coast_shade.h
    coast_shade.cpp
    area_pattern_material.h
    area_pattern_material.cpp
    chart_provider.h
    chart_layer.h
    chart_layer.cpp
    raster_chart_provider.h
    raster_chart_provider.cpp
    test_chart.h
    test_chart.cpp
    s52_engine.h
    s52_engine.cpp
    s52_vector_chart_provider.h
    s52_vector_chart_provider.cpp
    chart_extent.h
    chart_boundary_provider.h
    chart_boundary_provider.cpp
    pick_highlight_provider.h
    pick_highlight_provider.cpp
    chart_worker.h
    chart_worker.cpp
    shapefile_basemap_provider.h
    shapefile_basemap_provider.cpp
    demo_nav_data_provider.h
    demo_nav_data_provider.cpp
    ais_layer.h
    ais_layer.cpp
    own_ship_layer.h
    own_ship_layer.cpp
    alert_engine.h
    alert_engine.cpp
    grid_layer.h
    grid_layer.cpp
    anchor_watch_layer.h
    anchor_watch_layer.cpp
    waypoint_icon_provider.h
    waypoint_icon_provider.cpp
    waypoint_icons.h
    waypoint_icons.cpp
    route_overlay_layers.h
    route_overlay_layers.cpp
    ais_target_store.h
    in_memory_ais_store.h
    in_memory_ais_store.cpp
    sqlite_ais_store.h
    sqlite_ais_store.cpp
    nav_core.h
    nav_core.cpp
    own_ship_holder.h
    nav_feed_worker.h
    nav_feed_worker.cpp
    model_nav_data_provider.h
    model_nav_data_provider.cpp
    switchable_nav_provider.h
    switchable_nav_provider.cpp
    nav_state_view_model.h
    nav_state_view_model.cpp
    ais_selection_view_model.h
    ais_selection_view_model.cpp
    object_query_view_model.h
    object_query_view_model.cpp
    s57_dictionary.h
    s57_dictionary.cpp
    route_list_view_model.h
    route_list_view_model.cpp
    route_follower.h
    route_follower.cpp
    sim_ship_controller.h
    sim_ship_controller.cpp
    connections_view_model.h
    connections_view_model.cpp
    chart_source_model.h
    chart_source_model.cpp
    chart_catalog_db.h
    chart_catalog_db.cpp
    senc_cache.h
    senc_cache.cpp
    decoded_cell_cache.h
    decoded_cell_cache.cpp
    config_store.h
    config_store.cpp
    own_ship_config.h
    own_ship_config.cpp
    display_config.h
    display_config.cpp
    ais_config.h
    ais_config.cpp
    ais_cpa.h
    ais_cpa.cpp
    route_defaults_config.h
    route_defaults_config.cpp
    ui_config.h
    ui_config.cpp
    sound_player.h
    sound_player.cpp
    time_controller.h
    time_controller.cpp
    tide_model.h
    tide_model.cpp
    tide_layer.h
    tide_layer.cpp
    tide_graph_view_model.h
    tide_graph_view_model.cpp
    chart_config.h
    chart_config.cpp
    ocharts_service.h
    ocharts_service.cpp
    osenc_reader.h
    osenc_reader.cpp
    nmea_monitor_model.h
    nmea_monitor_model.cpp
    peer_send_controller.h
    peer_send_controller.cpp
    gps_upload_controller.h
    gps_upload_controller.cpp
    cm93_dictionary.h
    cm93_dictionary.cpp
    cm93_cell_reader.h
    cm93_cell_reader.cpp
    cm93_transcoder.h
    cm93_transcoder.cpp
    cm93_scanner.h
    cm93_scanner.cpp
    comm_priorities_model.h
    comm_priorities_model.cpp
    config_templates.h
    config_templates.cpp
    raster_chart.h
    raster_chart.cpp
    raster_chart_layer.h
    raster_chart_layer.cpp
    cm93_loader.h
    cm93_loader.cpp
    plugin/ocpn_qt_plugin.h
    plugin/plugin_registry.h
    plugin/plugin_registry.cpp
    wind_decoder.h
    wind_decoder.cpp
)

# Inject the source-tree path to data/s57data/ so s52plib can find its
# presentation library (S52RAZDS.RLE / chartsymbols.xml / raster
# symbol sheets) without a separate install step during prototyping.
# Production installs will discover via QStandardPaths::AppDataLocation.
# App version string for the About dialog (from VERSION.cmake, like the wx
# PACKAGE_VERSION). Re-included here so VERSION_* are in this directory's scope.
include(${CMAKE_SOURCE_DIR}/VERSION.cmake)
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_VERSION_STR="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
  OCPN_QT_S57DATA_DIR="${CMAKE_SOURCE_DIR}/data/s57data"
  # Bundled tide/current harmonic data set, seeded on first run (P3.14 B).
  OCPN_QT_TCDATA="${CMAKE_SOURCE_DIR}/data/tcdata/harmonics-dwf-20210110-free.tcd"
  # OpenCPN shapefile basemap -- the always-on world background chart, read
  # by ShapefileBasemapProvider. basemap_low is the better default map;
  # basemap_crude_10x10 is the fallback.
  OCPN_QT_BASEMAP_SHP="${CMAKE_SOURCE_DIR}/data/basemap_shp/basemap_low.shp"
  # Bundled alert sounds (ship's bells, P3.15). 1bells/2bells.wav for striking.
  OCPN_QT_SOUNDS_DIR="${CMAKE_SOURCE_DIR}/data/sounds"
  # Default mark-icon SVGs (rendered into the WayPointman catalogue at startup).
  OCPN_QT_MARKICONS_DIR="${CMAKE_SOURCE_DIR}/data/svg/markicons"
)

# i18n (P3.10): Qt Linguist translations. `cmake --build build --target
# update_translations` re-scans the target's C++/QML sources into the .ts
# files (translate with Qt Linguist); lrelease-compiled .qm files embed at
# :/i18n and main.cpp installs the QTranslator for the configured language.
qt_add_translations(opencpn-qt
  TS_FILES
    translations/opencpn-qt_de.ts
    translations/opencpn-qt_fr.ts
    translations/opencpn-qt_es.ts
    translations/opencpn-qt_nl.ts
    translations/opencpn-qt_it.ts
  RESOURCE_PREFIX "/i18n"
  LUPDATE_OPTIONS -no-obsolete
)

# The example Qt plugin (P4.2 validation; opt-in).
option(OCPN_QT_EXAMPLE_PLUGIN "Build the example Qt plugin" OFF)
if (OCPN_QT_EXAMPLE_PLUGIN)
  add_subdirectory(plugin/example)
endif ()

# The Dashboard plugin (P4.3) -- superseded by the built-in HUD stats
# panel (user decision 2026-06-10); kept as a nav-data API reference,
# off by default.
option(OCPN_QT_DASHBOARD_PLUGIN "Build the Dashboard Qt plugin" OFF)
if (OCPN_QT_DASHBOARD_PLUGIN)
  add_subdirectory(plugin/dashboard)
endif ()

# The Chart Downloader plugin (P4.4 v1).
option(OCPN_QT_CHARTDLDR_PLUGIN "Build the Chart Downloader Qt plugin" ON)
if (OCPN_QT_CHARTDLDR_PLUGIN)
  add_subdirectory(plugin/chartdldr)
endif ()

# The GRIB plugin (P4.5 v1).
option(OCPN_QT_GRIB_PLUGIN "Build the GRIB Qt plugin" ON)
if (OCPN_QT_GRIB_PLUGIN)
  add_subdirectory(plugin/grib)
endif ()

# The o-charts shop plugin (P4.7, day-1 per user decision).
option(OCPN_QT_OCHARTSHOP_PLUGIN "Build the o-charts shop Qt plugin" ON)
if (OCPN_QT_OCHARTSHOP_PLUGIN)
  add_subdirectory(plugin/ochartshop)
endif ()

# libtess2: the basemap provider tessellates the land polygons.
target_include_directories(opencpn-qt PRIVATE
  ${CMAKE_SOURCE_DIR}/libs/libtess2/Include
)
target_link_libraries(opencpn-qt PRIVATE opencpn_qt_toolkit)

# Optional: an absolute path to a real S-57 .000 ENC cell to load on
# startup (P2.8d). Set -DOCPN_QT_TEST_ENC=/path/to/CELL.000 at configure
# time; if unset/empty the canvas falls back to the synthetic demo chart.
if (NOT DEFINED OCPN_QT_TEST_ENC)
  set(OCPN_QT_TEST_ENC "")
endif ()
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_QT_TEST_ENC="${OCPN_QT_TEST_ENC}"
)

# A recorded NMEA-0183 log replayed into the model to give the live nav
# overlays (AIS / own-ship) data without a hardware connection (P2.11).
# Override with -DOCPN_QT_NMEA_LOG=/path/to/log at configure time.
if (NOT DEFINED OCPN_QT_NMEA_LOG)
  set(OCPN_QT_NMEA_LOG "${CMAKE_SOURCE_DIR}/test/testdata/Hakefjord.log")
endif ()
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_QT_NMEA_LOG="${OCPN_QT_NMEA_LOG}"
)

# o-charts decryption daemon (closed-source but redistributable; vendored
# under third_party/oexserverd). OChartsService runs it to generate the
# machine fingerprint and, later, to decrypt .oesenc/.oeu cells over a FIFO.
# Overridable; falls back at runtime to the installed o-charts_pi plugin copy.
if (NOT DEFINED OCPN_QT_OEXSERVERD)
  set(OCPN_QT_OEXSERVERD "${CMAKE_SOURCE_DIR}/third_party/oexserverd/oexserverd")
endif ()
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_QT_OEXSERVERD="${OCPN_QT_OEXSERVERD}"
)

# wx_qt_string.h lives in the model layer; we use it from s52_engine.cpp
# to bridge QString <-> wxString at the s52plib boundary.
target_include_directories(opencpn-qt PRIVATE
  ${CMAKE_SOURCE_DIR}/model/include
)

# Bring in wxWidgets compile flags for the .cpp that talks to s52plib
# (s52_engine.cpp). wx stays under the hood for now -- libs/s52plib's
# API is still wx-typed (wxString / wxColour / wxFont); de-wxing that
# is a separate later task.
include(${wxWidgets_USE_FILE})

target_link_libraries(opencpn-qt PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Qml
  # QQuickStyle (selects the native Quick Controls style in main.cpp).
  Qt6::QuickControls2
  # QSerialPortInfo for the Connections editor's serial-port enumeration.
  Qt6::SerialPort
  # QMediaPlayer/QAudioOutput for the alert-sound engine (SoundPlayer).
  Qt6::Multimedia
  # QSvgRenderer -- rasterise the default mark-icon SVGs (waypoint_icons.cpp).
  Qt6::Svg
  # libs/s52plib transitively brings ocpn::model, ocpn::geoprim,
  # ocpn::gl-headers, ocpn::pugixml, ocpn::gdal, ocpn::tess2 -- the
  # full set s52plib needs after the P2.8.0 standalone-link refactor.
  ocpn::s52plib
  # OGR S-57 driver -- reads .000 ENC cells into OGR features (P2.8d).
  ocpn::s57-charts
  # Shapefile reader for the world basemap (data/basemap_shp/*.shp).
  ocpn::shapefile_cpp
  # Tide/current harmonic prediction engine (P3.14 phase A).
  ocpn::tides
  ${wxWidgets_LIBRARIES}
)

# macOS: bundle as a .app so the resource path resolves cleanly. Match
# the deployment-target bump applied to the model target (P1.10).
if (APPLE)
  set_target_properties(opencpn-qt PROPERTIES
    MACOSX_BUNDLE TRUE
  )
  target_compile_options(opencpn-qt PRIVATE -mmacosx-version-min=10.15)
  # Native title-bar accessory toggle (NSTitlebarAccessoryViewController).
  target_sources(opencpn-qt PRIVATE macos_titlebar.h macos_titlebar.mm)
  target_link_libraries(opencpn-qt PRIVATE "-framework AppKit")
endif ()
