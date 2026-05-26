# gui/qt -- new Qt-Quick chart-canvas prototype.
#
# Builds the `opencpn-qt` executable: a minimal QGuiApplication +
# QQmlApplicationEngine that hosts a ChartCanvas QQuickItem. Sibling
# to the legacy wx-based `OpenCPN` target; replaces it in Phase 3.
#
# See docs/QT_MIGRATION_TASKS.md Phase 2 for the three-tier scene-graph
# plan this implements.

qt_add_executable(opencpn-qt
  main.cpp
)

# Custom AA-line shaders (P2.4 #5 / P2.13): baked to .qsb via qsb and exposed
# as Qt resources at :/shaders/aaline.{vert,frag}.qsb (see aa_line.cpp).
find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
qt_add_shaders(opencpn-qt "opencpn_qt_shaders"
  PREFIX "/"
  FILES
    shaders/aaline.vert
    shaders/aaline.frag
    shaders/coastshade.vert
    shaders/coastshade.frag
)

qt_add_qml_module(opencpn-qt
  URI opencpn.qt
  VERSION 1.0
  QML_FILES
    qml/Main.qml
  SOURCES
    app_controller.h
    chart_canvas.h
    chart_canvas.cpp
    layer.h
    layer_compositor.h
    layer_compositor.cpp
    viewport.h
    sg_helpers.h
    sg_helpers.cpp
    sg_texture_cache.h
    sg_builder.h
    sg_builder.cpp
    aa_line.h
    aa_line.cpp
    coast_shade.h
    coast_shade.cpp
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
    chart_worker.h
    chart_worker.cpp
    shapefile_basemap_provider.h
    shapefile_basemap_provider.cpp
    nav_data.h
    nav_data_provider.h
    demo_nav_data_provider.h
    demo_nav_data_provider.cpp
    nav_layer.h
    ais_layer.h
    ais_layer.cpp
    own_ship_layer.h
    own_ship_layer.cpp
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
    connections_view_model.h
    connections_view_model.cpp
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
    chart_config.h
    chart_config.cpp
    nmea_monitor_model.h
    nmea_monitor_model.cpp
    wind_decoder.h
    wind_decoder.cpp
)

# Inject the source-tree path to data/s57data/ so s52plib can find its
# presentation library (S52RAZDS.RLE / chartsymbols.xml / raster
# symbol sheets) without a separate install step during prototyping.
# Production installs will discover via QStandardPaths::AppDataLocation.
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_QT_S57DATA_DIR="${CMAKE_SOURCE_DIR}/data/s57data"
  # OpenCPN shapefile basemap -- the always-on world background chart, read
  # by ShapefileBasemapProvider. basemap_low is the better default map;
  # basemap_crude_10x10 is the fallback.
  OCPN_QT_BASEMAP_SHP="${CMAKE_SOURCE_DIR}/data/basemap_shp/basemap_low.shp"
)

# libtess2: the basemap provider tessellates the land polygons.
target_include_directories(opencpn-qt PRIVATE
  ${CMAKE_SOURCE_DIR}/libs/libtess2/Include
)

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
  # libs/s52plib transitively brings ocpn::model, ocpn::geoprim,
  # ocpn::gl-headers, ocpn::pugixml, ocpn::gdal, ocpn::tess2 -- the
  # full set s52plib needs after the P2.8.0 standalone-link refactor.
  ocpn::s52plib
  # OGR S-57 driver -- reads .000 ENC cells into OGR features (P2.8d).
  ocpn::s57-charts
  # Shapefile reader for the world basemap (data/basemap_shp/*.shp).
  ocpn::shapefile_cpp
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
