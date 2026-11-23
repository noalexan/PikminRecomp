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
if (MINGW)
    # Cross-compiling with MinGW-w64 on Linux: <Windows.h> spelled as MSVC code does, and abseil's
    # open(..., O_NONBLOCK) (MinGW has no O_NONBLOCK; the flag is meaningless for files on Windows).
    # Only needed on a case-sensitive host (Linux cross builds); a Windows host finds windows.h as is.
    if (NOT CMAKE_HOST_WIN32)
        include_directories(BEFORE SYSTEM ${CMAKE_CURRENT_LIST_DIR}/mingw-shim)
    endif ()
    add_compile_definitions(O_NONBLOCK=0)
    # Dawn's D3D11/D3D12 backends need Windows SDK headers MinGW lacks (DXProgrammableCapture.h,
    # dxcapi...): Vulkan only.
    set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
    set(DAWN_USE_WINDOWS_UI OFF CACHE BOOL "" FORCE)  # CoreWindow/SwapChainPanel surfaces: D3D headers
endif ()
if (WIN32)
    # Cross builds (MinGW): protobuf (Tint IR binary / fuzzers only) would need a host protoc.
    set(DAWN_BUILD_PROTOBUF OFF CACHE BOOL "" FORCE)
    set(TINT_BUILD_IR_BINARY OFF CACHE BOOL "" FORCE)
endif ()

# Hash of every patch's contents: part of the declaration, so editing a patch re-populates aurora
# (FetchContent otherwise only notices changes to the declaration itself).
set(_aurora_patch_hash_input "")
foreach (_patch IN LISTS PIKMIN_AURORA_PATCHES)
    file(SHA256 ${_patch} _h)
    string(APPEND _aurora_patch_hash_input "${_h}")
endforeach ()
string(SHA256 PIKMIN_AURORA_PATCH_HASH "${_aurora_patch_hash_input}")

if (PIKMIN_AURORA_PATCHES)
    set(_aurora_patch_cmd git apply --whitespace=nowarn ${PIKMIN_AURORA_PATCHES})
else ()
    set(_aurora_patch_cmd ${CMAKE_COMMAND} -E true)
endif ()

FetchContent_Declare(aurora
    GIT_REPOSITORY https://github.com/encounter/aurora.git
    GIT_TAG ${PIKMIN_AURORA_COMMIT}
    GIT_SHALLOW FALSE
    PATCH_COMMAND ${CMAKE_COMMAND} -E echo "aurora patches ${PIKMIN_AURORA_PATCH_HASH}"
          COMMAND git reset --hard ${PIKMIN_AURORA_COMMIT} COMMAND ${_aurora_patch_cmd}
    UPDATE_DISCONNECTED TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(aurora)

if (MINGW)
    # aurora names MSVC libraries (comsuppw.lib, DXGI.lib, ...): point them at MinGW's. comsuppw
    # isn't needed (MinGW's comutil.h defines its helpers inline).
    foreach (_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_libs aurora_core ${_prop})
        if (_libs)
            list(REMOVE_ITEM _libs comsuppw.lib "$<LINK_ONLY:comsuppw.lib>")
            list(TRANSFORM _libs REPLACE "DXGI\\.lib" "dxgi")
            list(TRANSFORM _libs REPLACE "wbemuuid\\.lib" "wbemuuid")
            list(TRANSFORM _libs REPLACE "ntdll\\.lib" "ntdll")
            set_target_properties(aurora_core PROPERTIES ${_prop} "${_libs}")
        endif ()
    endforeach ()
endif ()

