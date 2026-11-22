# aurora (encounter/aurora, MIT): GameCube GX/PAD on WebGPU (Dawn). Pinned, patched, built from source.
include(FetchContent)

set(PIKMIN_AURORA_COMMIT f77492f74b0a81895511220bafa6bdc7e8bb9734)
file(GLOB PIKMIN_AURORA_PATCHES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/aurora-patches/*.patch)
list(SORT PIKMIN_AURORA_PATCHES)

set(AURORA_ENABLE_GX ON CACHE BOOL "" FORCE)
set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_RMLUI OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(AURORA_SDL3_PROVIDER system CACHE STRING "" FORCE)
set(AURORA_DAWN_PROVIDER vendor CACHE STRING "" FORCE)
set(AURORA_DAWN_LINKAGE static CACHE STRING "" FORCE)

if (PIKMIN_AURORA_PATCHES)
    set(_aurora_patch_cmd git apply --whitespace=nowarn ${PIKMIN_AURORA_PATCHES})
else ()
    set(_aurora_patch_cmd ${CMAKE_COMMAND} -E true)
endif ()

FetchContent_Declare(aurora
    GIT_REPOSITORY https://github.com/encounter/aurora.git
    GIT_TAG ${PIKMIN_AURORA_COMMIT}
    GIT_SHALLOW FALSE
    PATCH_COMMAND git reset --hard ${PIKMIN_AURORA_COMMIT} COMMAND ${_aurora_patch_cmd}
    UPDATE_DISCONNECTED TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(aurora)
