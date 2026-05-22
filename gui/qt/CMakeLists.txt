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
)

# Inject the source-tree path to data/s57data/ so s52plib can find its
# presentation library (S52RAZDS.RLE / chartsymbols.xml / raster
# symbol sheets) without a separate install step during prototyping.
# Production installs will discover via QStandardPaths::AppDataLocation.
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_QT_S57DATA_DIR="${CMAKE_SOURCE_DIR}/data/s57data"
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
