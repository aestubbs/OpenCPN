# ~~~
# Summary:      libs/tides -- the tide/current harmonic prediction engine
# License:      GPLv3+
# ~~~
#
# The OpenCPN tide/current engine (TCMgr + IDX_entry + harmonic readers +
# vendored libtcd), lifted out of the wx GUI tree so the Qt build (opencpn-qt)
# can compute tides without the wx GUI. P3.14 phase A.
#
# The engine is pure Qt/std (de-wx'd, P3.14 phase A.2): QString / QHash /
# QDateTime / QFileInfo / a small ObjArray<T> shim -- no wxWidgets.

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

# ocpn::model brings georef / logger headers + Qt6 + the model include paths;
# Qt6::Core covers QString / QHash / QDateTime / QFileInfo / QDir.
target_link_libraries(TIDES PUBLIC ocpn::model Qt6::Core)
