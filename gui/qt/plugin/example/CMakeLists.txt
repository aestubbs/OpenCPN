# The example Qt plugin (P4.2 validation). Not built by default -- enable
# with -DOCPN_QT_EXAMPLE_PLUGIN=ON and copy the module into
# <AppData>/opencpn-qt/plugins-qt/.
qt_add_plugin(opencpn-qt-example-plugin
  CLASS_NAME ExamplePlugin
  example_plugin.h
  example_plugin.cpp
)
qt_add_resources(opencpn-qt-example-plugin "example_plugin_qml"
  PREFIX "/example_plugin"
  FILES Hud.qml Settings.qml
)
target_include_directories(opencpn-qt-example-plugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(opencpn-qt-example-plugin PRIVATE Qt6::Core Qt6::Gui)

# Qt 6 file-I/O headers need the 10.15 libc++ attributes (see P1.10).
if (APPLE)
  target_compile_options(opencpn-qt-example-plugin PRIVATE -mmacosx-version-min=10.15)
endif ()
