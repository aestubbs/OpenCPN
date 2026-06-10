# The Chart Downloader Qt plugin (P4.4 v1): catalog XML -> download ->
# libarchive extract.
qt_add_plugin(opencpn-qt-chartdldr-plugin
  CLASS_NAME ChartDldrPlugin
  chartdldr_plugin.h
  chartdldr_plugin.cpp
)
qt_add_resources(opencpn-qt-chartdldr-plugin "chartdldr_plugin_qml"
  PREFIX "/chartdldr_plugin"
  FILES Settings.qml
)
find_library(OCPN_QT_LIBARCHIVE archive
  HINTS /opt/homebrew/opt/libarchive/lib /usr/local/opt/libarchive/lib)
find_path(OCPN_QT_LIBARCHIVE_INC archive.h
  HINTS /opt/homebrew/opt/libarchive/include /usr/local/opt/libarchive/include)
target_include_directories(opencpn-qt-chartdldr-plugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${OCPN_QT_LIBARCHIVE_INC})
target_link_libraries(opencpn-qt-chartdldr-plugin PRIVATE
  Qt6::Core Qt6::Gui Qt6::Network ${OCPN_QT_LIBARCHIVE})

# Qt 6 file-I/O headers need the 10.15 libc++ attributes (see P1.10).
if (APPLE)
  target_compile_options(opencpn-qt-chartdldr-plugin PRIVATE -mmacosx-version-min=10.15)
endif ()
