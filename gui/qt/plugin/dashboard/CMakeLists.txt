# The Dashboard Qt plugin (P4.3): instrument strip fed from the host
# NavDataProvider via the opencpn_qt_toolkit shared library.
qt_add_plugin(opencpn-qt-dashboard-plugin
  CLASS_NAME DashboardPlugin
  dashboard_plugin.h
  dashboard_plugin.cpp
)
qt_add_resources(opencpn-qt-dashboard-plugin "dashboard_plugin_qml"
  PREFIX "/dashboard_plugin"
  FILES Strip.qml Settings.qml
)
target_include_directories(opencpn-qt-dashboard-plugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(opencpn-qt-dashboard-plugin PRIVATE
  Qt6::Core Qt6::Gui opencpn_qt_toolkit)
