# Sound library
#
# Parameters:
#     SOUND_WX_LIBS - wxWidgets libraries
#     SOUND_WX_INCLUDE_DIR - wxWidgets headers location
#     Numerous configuration variables, see cmake/SoundConfig.cmake
#
# Exports
#     ocpn::sound transitive link target
#
cmake_minimum_required(VERSION 3.10.0)

if (TARGET ocpn::sound)
  return ()
endif ()

option(OCPN_ENABLE_SYSTEM_CMD_SOUND
       "Use aplay(1), afplay(1) etc. to play sounds if available"
       ${cmd_snd_default}
)
set(OCPN_SOUND_CMD "" CACHE STRING
    "Hardcoded value for CLI command used in SystemCmdSound backend"
)

if (WIN32 OR APPLE OR QT_ANDROID)
  option(OCPN_ENABLE_PORTAUDIO ${PORTAUDIO_MSG} OFF)
else ()
  option(OCPN_ENABLE_PORTAUDIO ${PORTAUDIO_MSG} ON)
endif ()
cmake_dependent_option(
  OCPN_ENABLE_SNDFILE "Use libsndfile for portaudio if available." ON
  "OCPN_ENABLE_PORTAUDIO" ON
)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)


set(SOUND_WX_INCLUDE_DIR ""
  CACHE STRING "wxWidgets header location (mandatory)"
)
set(SOUND_WX_LIBS "" CACHE STRING "wxWidgets libraries (mandatory)")

if ("${SOUND_WX_LIBS}" STREQUAL "")
  message(FATAL_ERROR "Sound parameter SOUND_WX_LIBS is undefined")
endif ()
if ("${SOUND_WX_INCLUDE_DIR}" STREQUAL "")
  message(FATAL_ERROR "Sound parameter SOUND_WX_LINCLUDE_DIR is undefined")
endif ()

set(PORTAUDIO_MSG "Use portaudio(3) to play sounds if available")

set(SRC
  src/sound.cpp
  src/ocpn_wx_sound.cpp
  src/factory.cpp
  src/sound_file_loader.cpp
  src/sound_loader_factory.cpp
  src/system_cmd_sound.cpp
  include/o_sound/sound.h
  include/o_sound/ocpn_wx_sound.h
  include/o_sound/factory.h
  include/o_sound/sound_file_loader.h
  include/o_sound/sound_loader_factory.h
  include/o_sound/system_cmd_sound.h
)
find_package(Portaudio)
find_package(LibSndfile)

if (WIN32)
  list(APPEND SRC src/msw_sound.cpp include/o_sound/msw_sound.h)
endif ()

if (QT_ANDROID)
  list(APPEND SRC src/android_sound.cpp include/o_sound/android_sound.h)
endif ()

if (PORTAUDIO_FOUND)
  list(APPEND SRC include/o_sound/port_audio_sound.h src/port_audio_sound.cpp)
endif ()

if (LIBSNDFILE_FOUND)
  list(APPEND SRC include/o_sound/sndfile_sound_loader.h src/sndfile_sound_loader.cpp)
endif ()

add_library(_SOUND STATIC ${SRC})
add_library(ocpn::sound ALIAS _SOUND)

include(SoundConfig)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang|GNU")  # Apple is AppleClang
  target_compile_options(_SOUND PRIVATE -fvisibility=default -fPIC)
endif ()

target_include_directories(_SOUND
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/o_sound
    ${CMAKE_BINARY_DIR}/include/o_sound
    ${SOUND_WX_INCLUDE_DIRS}
)
target_link_libraries(_SOUND PRIVATE ${SOUND_WX_LIBS})

# When libsndfile is found we add src/sndfile_sound_loader.cpp (above); it needs
# the libsndfile headers + library on _SOUND's own paths. Without this the build
# fails the moment libsndfile is installed (sndfile.h not found).
#
# The header lives in /opt/homebrew/include, which CMake lists in
# CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES (Homebrew/arm64 + AppleClang) and so
# *drops* from a normal target_include_directories() -I -- yet the compiler
# doesn't actually search it. Pass it as a raw -I compile option, which is not
# deduplicated against the implicit set.
if (LIBSNDFILE_FOUND)
  foreach (_sf_inc ${LIBSNDFILE_INCLUDE_DIRS})
    target_compile_options(_SOUND PRIVATE "-I${_sf_inc}")
  endforeach ()
  target_link_libraries(_SOUND PRIVATE ${LIBSNDFILE_LIBRARIES})
endif ()

if (QT_ANDROID)
  target_link_libraries(_SOUND PRIVATE ocpn::java-jvm)
endif ()
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/snd_config.h.in
  ${CMAKE_BINARY_DIR}/include/snd_config.h
)
