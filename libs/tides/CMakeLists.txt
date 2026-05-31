# ~~~
# Summary:      libs/tides -- the tide/current harmonic prediction engine
# License:      GPLv3+
# ~~~
#
# The OpenCPN tide/current engine (TCMgr + IDX_entry + harmonic readers +
# vendored libtcd), lifted out of the wx GUI tree so the Qt build (opencpn-qt)
# can compute tides without the wx GUI. P3.14 phase A.
#
# NOTE: the engine still uses wxString / wxObjArray internally; it compiles
# against the wxWidgets headers/libs the app already links. De-wxing it to pure
# Qt/std types is the follow-up (P3.14 phase A.2).

add_library(TIDES STATIC
  src/tcmgr.cpp
  src/idx_entry.cpp
  src/station_data.cpp
  src/tc_data_source.cpp
  src/tc_data_factory.cpp
  src/tcds_ascii_harmonic.cpp
  src/tcds_binary_harmonic.cpp
)
add_library(ocpn::tides ALIAS TIDES)

target_include_directories(TIDES PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Qt 6.11 headers require a macOS 10.15+ deployment target; the project default
# is 10.13. Append the higher min (last -mmacosx-version-min wins), matching
# what the Qt targets already do for opencpn-qt.
if (APPLE)
  target_compile_options(TIDES PRIVATE -mmacosx-version-min=10.15)
endif ()

# wxWidgets compile flags + include dirs (engine still wx-internal for now).
if (wxWidgets_FOUND)
  include(${wxWidgets_USE_FILE})
endif ()

# ocpn::model brings georef / logger / wx_qt_string headers + Qt6 + the model
# include paths; Qt6::Core covers QString / QDateTime / QFileInfo / QDir.
target_link_libraries(TIDES PUBLIC ocpn::model Qt6::Core)
