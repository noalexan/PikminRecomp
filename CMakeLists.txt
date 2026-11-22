cmake_minimum_required(VERSION 3.25)
project(PikminRecomp C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

option(PIKMIN_GRAPHICS "Build aurora graphics" ON)
if (PIKMIN_GRAPHICS)
    include(extern/aurora.cmake)
endif ()
add_subdirectory(tests/cpu)
add_subdirectory(runtime)
add_subdirectory(tests/runtime)

if(EXISTS ${PROJECT_SOURCE_DIR}/orig/GPIP01/sys/main.dol)
    include(runtime/gen.cmake)
    add_subdirectory(tests/game)
else()
    message(STATUS "orig/GPIP01 not extracted: skipping recompiled game targets")
endif()

if (PIKMIN_GRAPHICS)
    add_subdirectory(tests/gfx)
endif ()
