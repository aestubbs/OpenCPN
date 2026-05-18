cmake_minimum_required(VERSION 3.16.0)

if (TARGET observable::observable)
  return ()
endif ()

# Qt discovery is centralised in the top-level CMakeLists (task P0.3). This
# call is idempotent and keeps the library usable as a standalone subproject.
find_package(Qt6 REQUIRED COMPONENTS Core)

set(SRC
  include/observable_confvar.h
  include/observable_data.h
  include/observable_evt.h
  include/observable_evtvar.h
  include/observable_globvar.h
  include/observable_qt.h
  include/observable.h
  src/observable_confvar.cpp
  src/observable.cpp
  src/observable_qt.cpp
  src/observable_qt_p.h
)
add_library(_OBSERVABLE STATIC ${SRC})
add_library(observable::observable ALIAS _OBSERVABLE)

# observable_qt.h declares QObject-derived types; let CMake run moc.
set_target_properties(_OBSERVABLE PROPERTIES AUTOMOC ON)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang|GNU")  # Apple is AppleClang
  target_compile_options(_OBSERVABLE PRIVATE -fvisibility=default -fPIC)
endif ()

target_include_directories(
  _OBSERVABLE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(_OBSERVABLE PUBLIC Qt6::Core)
