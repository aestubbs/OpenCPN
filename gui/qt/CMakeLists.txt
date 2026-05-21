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
    demo_layers.h
    demo_layers.cpp
)

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
