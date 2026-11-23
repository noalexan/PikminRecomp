# Host runtime library: ppc_* hooks and strong hle_* replacements for the SDK.
file(GLOB PIKMIN_RT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rt/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/hle/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/hle/dsp/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/enhance/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp)
if (PIKMIN_GRAPHICS)
    file(GLOB PIKMIN_RT_GX_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/hle/gx/*.cpp)
    list(APPEND PIKMIN_RT_SOURCES ${PIKMIN_RT_GX_SOURCES})
endif ()
add_library(pikmin_rt STATIC ${PIKMIN_RT_SOURCES})
target_include_directories(pikmin_rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
# -g1 in Release: line tables, so installed-binary crash reports resolve (see tools/resolve_bt.py)
target_compile_options(pikmin_rt PRIVATE -O2 -Wall -Wextra $<$<CONFIG:Release>:-g1>)
target_link_libraries(pikmin_rt PUBLIC m)

if (PIKMIN_GRAPHICS)
    target_link_libraries(pikmin_rt PUBLIC aurora::core aurora::gx aurora::pad)
    target_compile_definitions(pikmin_rt PUBLIC PIKMIN_GRAPHICS=1)
    # mmio.cpp feeds aurora's FIFO directly (internal header; aurora is pinned).
    target_include_directories(pikmin_rt PRIVATE ${aurora_SOURCE_DIR}/lib ${aurora_SOURCE_DIR}/include)
endif ()

# Build identification for logs and crash reports: the git hash is re-read on every build.
set(PIKMIN_BUILD_INFO ${CMAKE_CURRENT_BINARY_DIR}/build_info.cpp)
add_custom_target(pikmin_build_info
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/rt/build_info.cpp.in
            -DOUT=${PIKMIN_BUILD_INFO} -P ${CMAKE_CURRENT_SOURCE_DIR}/write_build_info.cmake
    BYPRODUCTS ${PIKMIN_BUILD_INFO}
    VERBATIM)
target_sources(pikmin_rt PRIVATE ${PIKMIN_BUILD_INFO})
add_dependencies(pikmin_rt pikmin_build_info)
if (CMAKE_BUILD_TYPE)
    set(PIKMIN_BUILD_TYPE ${CMAKE_BUILD_TYPE})
else ()
    set(PIKMIN_BUILD_TYPE Debug)
endif ()
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/rt/diag.cpp PROPERTIES
    COMPILE_DEFINITIONS PIKMIN_BUILD_TYPE="${PIKMIN_BUILD_TYPE}")
