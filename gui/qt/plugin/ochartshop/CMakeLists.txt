# The o-charts shop plugin (P4.7, day-1): account login, system
# registration via the host's oexserverd fingerprint, chart download +
# install against the live o-charts.org API.
qt_add_plugin(opencpn-qt-ochartshop-plugin
  CLASS_NAME OchartShopPlugin
  ochartshop_plugin.h
  ochartshop_plugin.cpp
)
qt_add_resources(opencpn-qt-ochartshop-plugin "ochartshop_plugin_qml"
  PREFIX "/ochartshop_plugin"
  FILES Settings.qml
)
find_library(OCPN_QT_LIBARCHIVE archive
  HINTS /opt/homebrew/opt/libarchive/lib /usr/local/opt/libarchive/lib)
find_path(OCPN_QT_LIBARCHIVE_INC archive.h
  HINTS /opt/homebrew/opt/libarchive/include /usr/local/opt/libarchive/include)
target_include_directories(opencpn-qt-ochartshop-plugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${OCPN_QT_LIBARCHIVE_INC})
target_link_libraries(opencpn-qt-ochartshop-plugin PRIVATE
  Qt6::Core Qt6::Gui Qt6::Network ${OCPN_QT_LIBARCHIVE})
# Qt 6 file-I/O headers need the 10.15 libc++ attributes (see P1.10).
if (APPLE)
  target_compile_options(opencpn-qt-ochartshop-plugin PRIVATE
    -mmacosx-version-min=10.15)
endif ()
