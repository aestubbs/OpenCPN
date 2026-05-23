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

qt_add_qml_module(opencpn-qt
  URI opencpn.qt
  VERSION 1.0
  QML_FILES
    qml/Main.qml
  SOURCES
    chart_canvas.h
    chart_canvas.cpp
    layer.h
    layer_compositor.h
    layer_compositor.cpp
    viewport.h
    sg_helpers.h
    sg_helpers.cpp
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
    gshhs_world_provider.h
    gshhs_world_provider.cpp
)

# Inject the source-tree path to data/s57data/ so s52plib can find its
# presentation library (S52RAZDS.RLE / chartsymbols.xml / raster
# symbol sheets) without a separate install step during prototyping.
# Production installs will discover via QStandardPaths::AppDataLocation.
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_QT_S57DATA_DIR="${CMAKE_SOURCE_DIR}/data/s57data"
  # GSHHS crude world coastline (poly-c-1.dat) -- the always-on world
  # background chart, read directly by GshhsWorldProvider.
  OCPN_QT_GSHHS_DIR="${CMAKE_SOURCE_DIR}/data/gshhs"
)

# libtess2: GshhsWorldProvider tessellates the GSHHS land polygons.
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
  # libs/s52plib transitively brings ocpn::model, ocpn::geoprim,
  # ocpn::gl-headers, ocpn::pugixml, ocpn::gdal, ocpn::tess2 -- the
  # full set s52plib needs after the P2.8.0 standalone-link refactor.
  ocpn::s52plib
  # OGR S-57 driver -- reads .000 ENC cells into OGR features (P2.8d).
  ocpn::s57-charts
  ${wxWidgets_LIBRARIES}
)

# macOS: bundle as a .app so the resource path resolves cleanly. Match
# the deployment-target bump applied to the model target (P1.10).
if (APPLE)
  set_target_properties(opencpn-qt PROPERTIES
    MACOSX_BUNDLE TRUE
  )
  target_compile_options(opencpn-qt PRIVATE -mmacosx-version-min=10.15)
endif ()
