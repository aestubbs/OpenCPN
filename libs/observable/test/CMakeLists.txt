# Standalone build for the Qt observable unit tests (task P1.1f).
# Configure directly, e.g.:
#   cmake -S libs/observable/test -B build/observable_test
#   cmake --build build/observable_test
#   ./build/observable_test/test_observable_qt

cmake_minimum_required(VERSION 3.16)
project(observable_qt_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if (APPLE AND EXISTS "/opt/homebrew/opt/qt")
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
endif ()
find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(test_observable_qt
  test_observable_qt.cpp
  ../src/observable_qt.cpp
  ../include/observable_qt.h
)
target_include_directories(test_observable_qt PRIVATE ../include)
target_link_libraries(test_observable_qt PRIVATE Qt6::Core)
