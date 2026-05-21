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
)

# s52_engine.{h,cpp} exist in this directory but are *not* added to the
# build yet -- they require libs/s52plib to link standalone, which it
# can't (it has unresolved symbols satisfied only by gui/src/ wx chart
# engine files). See docs/QT_MIGRATION_TASKS.md P2.8 -- the standalone
# refactor lands first, then we wire S52Engine in.

target_link_libraries(opencpn-qt PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Qml
)

# macOS: bundle as a .app so the resource path resolves cleanly. Match
# the deployment-target bump applied to the model target (P1.10).
if (APPLE)
  set_target_properties(opencpn-qt PROPERTIES
    MACOSX_BUNDLE TRUE
  )
  target_compile_options(opencpn-qt PRIVATE -mmacosx-version-min=10.15)
endif ()
