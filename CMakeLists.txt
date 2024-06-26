cmake_minimum_required(VERSION 3.23)
project(beebem VERSION 0.0.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BEEBEM_WITH_DEBUGGER "compile with additional logging" OFF)
option(BEEBEM_ENABLE_ECONET "compile with econet" ON)
option(BEEBEM_DISABLE_REALTIME_SLIDER "compile without realtime slider on slide bar" ON)
option(BEEBEM_DISABLE_WELCOME_MESSAGE "compile without welcome message" OFF)
option(BEEBEM_ENABLE_FULLSCREEN_FPS "enable full screen FPS indicator" OFF)
option(BEEBEM_ENABLE_SYSTEM_CP "use system cp instead of C file copy" OFF)
option(BEEBEM_ENABLE_FORCE_COLORMAP "force colormap for 8-bit displays" OFF)

if (BEEBEM_WITH_DEBUGGER)
  message(STATUS "enabling debugger")
  add_definitions(-DWITH_DEBUG_OUTPUT=1 -DEG_DEBUG=1)
else()
  message(STATUS "disabling debugger")
  add_definitions(-DWITHOUT_LEVEL_INDICATOR=1, -DWITHOUT_SHENANIGANS=1)
endif()

add_definitions(-DWITH_UNIX_EXTRAS=1 -DREG_FILENAME="beebem.registry")

include(create_config_file.cmake)

find_package(ZLIB)
set(HAVE_LIBZ ZLIB_FOUND)
if (ZLIB_FOUND)
  set(HAVE_ZLIB_H 1)
endif()

add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(doc)
