# The GRIB Qt plugin (P4.5 v1): the vendored zyGrib decode core compiled
# as-is (wx-string surface; the plugin links wx base like the app) + a
# wind-arrow Layer through opencpn_qt_toolkit -- the first
# Layer-contributing plugin.
set(GRIB_SRC ${CMAKE_SOURCE_DIR}/plugins/grib_pi/src)
file(GLOB GRIB_BZIP2_C ${GRIB_SRC}/bzip2/*.c)
qt_add_plugin(opencpn-qt-grib-plugin
  CLASS_NAME GribPlugin
  grib_plugin.h
  grib_plugin.cpp
  grib_wind_layer.h
  grib_wind_layer.cpp
  ${GRIB_SRC}/GribRecord.cpp
  ${GRIB_SRC}/GribV1Record.cpp
  ${GRIB_SRC}/GribV2Record.cpp
  ${GRIB_SRC}/GribReader.cpp
  ${GRIB_SRC}/zuFile.cpp
  ${GRIB_BZIP2_C}
)
qt_add_resources(opencpn-qt-grib-plugin "grib_plugin_qml"
  PREFIX "/grib_plugin"
  FILES Settings.qml CursorReadout.qml Flyout.qml CursorDataHud.qml
)
target_include_directories(opencpn-qt-grib-plugin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${GRIB_SRC}
  ${GRIB_SRC}/bzip2)
target_link_libraries(opencpn-qt-grib-plugin PRIVATE
  Qt6::Core Qt6::Gui Qt6::Quick opencpn_qt_toolkit
  ${wxWidgets_LIBRARIES} ZLIB::ZLIB)
# Qt 6 file-I/O headers need the 10.15 libc++ attributes (see P1.10).
if (APPLE)
  target_compile_options(opencpn-qt-grib-plugin PRIVATE
    -mmacosx-version-min=10.15)
endif ()
