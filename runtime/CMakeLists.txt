# Host runtime library: ppc_* hooks and strong hle_* replacements for the SDK.
file(GLOB PIKMIN_RT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rt/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/hle/*.cpp)
add_library(pikmin_rt STATIC ${PIKMIN_RT_SOURCES})
target_include_directories(pikmin_rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
target_compile_options(pikmin_rt PRIVATE -O2 -Wall -Wextra)
target_link_libraries(pikmin_rt PUBLIC m)
