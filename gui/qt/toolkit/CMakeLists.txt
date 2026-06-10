# opencpn_qt_toolkit -- the plugin-facing layer toolkit (P4): the Layer
# contract + compositor-side bases (Layer, NavLayer), the nav-data seam
# (NavDataProvider + value types), the Viewport and the scene-graph
# builders (SgBuilder, AA lines, texture helpers). A SHARED library so
# OcpnQtPlugin modules can contribute Layers / consume nav data against
# real symbols instead of the executable's (P4.2 recorded constraint).
# Symbols are default-visible on macOS/Linux; Windows export macros land
# with the P0.5 CI gate.
qt_add_library(opencpn_qt_toolkit SHARED
  layer.h
  nav_data.h
  nav_data_provider.h
  nav_layer.h
  viewport.h
  sg_builder.h
  sg_builder.cpp
  sg_helpers.h
  sg_helpers.cpp
  sg_texture_cache.h
  aa_line.h
  aa_line.cpp
)
set_target_properties(opencpn_qt_toolkit PROPERTIES AUTOMOC ON)
target_include_directories(opencpn_qt_toolkit PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(opencpn_qt_toolkit PRIVATE
  ${CMAKE_SOURCE_DIR}/libs/libtess2/Include
)
target_link_libraries(opencpn_qt_toolkit PUBLIC
  Qt6::Core Qt6::Gui Qt6::Quick
)
target_link_libraries(opencpn_qt_toolkit PRIVATE TESS2)

# Qt 6 file-I/O headers need the 10.15 libc++ attributes (see P1.10).
if (APPLE)
  target_compile_options(opencpn_qt_toolkit PRIVATE -mmacosx-version-min=10.15)
endif ()
