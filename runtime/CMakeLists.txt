# Host runtime library: ppc_* hooks and strong hle_* replacements for the SDK.
file(GLOB PIKMIN_RT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rt/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/hle/*.cpp)
if (PIKMIN_GRAPHICS)
    file(GLOB PIKMIN_RT_GX_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/hle/gx/*.cpp)
    list(APPEND PIKMIN_RT_SOURCES ${PIKMIN_RT_GX_SOURCES})
endif ()
add_library(pikmin_rt STATIC ${PIKMIN_RT_SOURCES})
target_include_directories(pikmin_rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
target_compile_options(pikmin_rt PRIVATE -O2 -Wall -Wextra)
target_link_libraries(pikmin_rt PUBLIC m)

if (PIKMIN_GRAPHICS)
    target_link_libraries(pikmin_rt PUBLIC aurora::core aurora::gx aurora::pad)
    target_compile_definitions(pikmin_rt PUBLIC PIKMIN_GRAPHICS=1)
    # mmio.cpp feeds aurora's FIFO directly (internal header; aurora is pinned).
    target_include_directories(pikmin_rt PRIVATE ${aurora_SOURCE_DIR}/lib ${aurora_SOURCE_DIR}/include)
endif ()
