# Recompile main.dol into C (build-time) and build it as a static library.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(PIKMIN_DOL ${PROJECT_SOURCE_DIR}/orig/GPIP01/sys/main.dol)
set(PIKMIN_MAP ${PROJECT_SOURCE_DIR}/orig/GPIP01/files/dataDir/build.map)
set(PIKMIN_OVERRIDES ${PROJECT_SOURCE_DIR}/recomp/overrides.toml)
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
set(GEN_SHARDS 64)

file(GLOB_RECURSE RECOMP_PY CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/recomp/*.py)

math(EXPR _last "${GEN_SHARDS} - 1")
string(LENGTH "${_last}" _width)
set(GEN_SRCS)
foreach(i RANGE 0 ${_last})
    string(LENGTH "${i}" _len)
    math(EXPR _pad "${_width} - ${_len}")
    string(REPEAT "0" ${_pad} _zeros)
    list(APPEND GEN_SRCS ${GEN_DIR}/gen_${_zeros}${i}.c)
endforeach()
set(GEN_OUTPUTS ${GEN_SRCS} ${GEN_DIR}/functable.c ${GEN_DIR}/hle_stubs.c ${GEN_DIR}/gen.h)

add_custom_command(
    OUTPUT ${GEN_DIR}/stamp ${GEN_OUTPUTS}
    COMMAND ${Python3_EXECUTABLE} -m recomp --dol ${PIKMIN_DOL} --map ${PIKMIN_MAP}
            --overrides ${PIKMIN_OVERRIDES} --out ${GEN_DIR} --shards ${GEN_SHARDS}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    DEPENDS ${PIKMIN_DOL} ${PIKMIN_MAP} ${PIKMIN_OVERRIDES} ${RECOMP_PY}
    COMMENT "Recompiling main.dol to C"
    VERBATIM)

add_library(pikmin_gen STATIC ${GEN_SRCS} ${GEN_DIR}/functable.c)
target_include_directories(pikmin_gen PUBLIC ${GEN_DIR} ${PROJECT_SOURCE_DIR}/runtime/cpu)
# -ffp-contract=off: never fuse separate guest fmul/fadd into an FMA.
target_compile_options(pikmin_gen PRIVATE -O2 -fno-strict-aliasing -fwrapv -ffp-contract=off -w $<$<CONFIG:Release>:-g1>)
target_link_libraries(pikmin_gen PUBLIC m)

add_library(pikmin_stubs STATIC ${GEN_DIR}/hle_stubs.c ${PROJECT_SOURCE_DIR}/runtime/cpu/stubs.c)
target_link_libraries(pikmin_stubs PUBLIC pikmin_gen)

add_executable(pikmin_link_check ${PROJECT_SOURCE_DIR}/runtime/link_check.c)
target_link_libraries(pikmin_link_check PRIVATE pikmin_gen pikmin_stubs)

# The game: generated code + runtime. pikmin_rt is linked whole so its strong hle_* definitions
# always replace the weak generated stubs (a static archive would otherwise satisfy them with the
# first weak definition it finds).
if (WIN32)
    # COFF: no ELF-style weak override. Keep only the stubs pikmin_rt doesn't define, made strong.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hle_stubs_strong.c
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/strong_hle_stubs.py ${CMAKE_NM}
                $<TARGET_FILE:pikmin_rt> ${GEN_DIR}/hle_stubs.c ${CMAKE_CURRENT_BINARY_DIR}/hle_stubs_strong.c
        DEPENDS pikmin_rt ${GEN_DIR}/hle_stubs.c ${PROJECT_SOURCE_DIR}/tools/strong_hle_stubs.py
        COMMENT "Selecting HLE stubs not implemented by the runtime"
        VERBATIM)
    add_library(pikmin_hle_stubs STATIC ${CMAKE_CURRENT_BINARY_DIR}/hle_stubs_strong.c)
else ()
    add_library(pikmin_hle_stubs STATIC ${GEN_DIR}/hle_stubs.c)
endif ()
target_link_libraries(pikmin_hle_stubs PUBLIC pikmin_gen)
# main.dol is baked into the binary (the runtime copies its sections into guest RAM at boot),
# so no disc image is needed for the executable itself.
if (WIN32)  # COFF: read-only data is .rdata, and there is no GNU-stack note
    set(_dol_section ".section .rdata,\"dr\"")
    set(_dol_note "")
else ()
    set(_dol_section ".section .rodata")
    set(_dol_note ".section .note.GNU-stack,\"\",@progbits\n")
endif ()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dol_blob.S.in
     "${_dol_section}\n.balign 32\n.global pikmin_dol_start\n.global pikmin_dol_end\n"
     "pikmin_dol_start:\n.incbin \"@PIKMIN_DOL@\"\npikmin_dol_end:\n${_dol_note}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/dol_blob.S.in ${CMAKE_CURRENT_BINARY_DIR}/dol_blob.S @ONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/dol_blob.S PROPERTIES OBJECT_DEPENDS ${PIKMIN_DOL})
enable_language(ASM)
include(GNUInstallDirs)
add_executable(pikmin ${PROJECT_SOURCE_DIR}/runtime/main.cpp ${CMAKE_CURRENT_BINARY_DIR}/dol_blob.S)
# Game data fallbacks when neither --disc nor config names one: the installed data directory
# (relative to the binary), then the extracted tree in the source checkout.
file(RELATIVE_PATH PIKMIN_DATA_REL ${CMAKE_INSTALL_FULL_BINDIR} ${CMAKE_INSTALL_FULL_DATADIR}/pikminrecomp/data)
target_compile_definitions(pikmin PRIVATE PIKMIN_DATA_REL="${PIKMIN_DATA_REL}"
                           PIKMIN_SOURCE_DATA="${PROJECT_SOURCE_DIR}/orig/GPIP01")
target_compile_options(pikmin PRIVATE -O2 -Wall -Wextra $<$<CONFIG:Release>:-g1>)
if (WIN32)
    # One self-contained .exe: no libstdc++/libgcc/winpthread DLLs to ship next to it.
    target_link_options(pikmin PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
else ()
    target_link_options(pikmin PRIVATE -rdynamic)  # readable f_<addr> frames in fatal backtraces
endif ()
target_link_libraries(pikmin PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,pikmin_rt>" pikmin_gen pikmin_hle_stubs)

if (WIN32)
    # Windows: one folder (cmake --install build/win --prefix DIR): pikmin.exe, the MinGW DLLs it
    # needs (tools/mingw_dlls.py) and data/ (found next to the .exe). Nintendo data: never distribute.
    # The MinGW DLLs come from the sysroot (cross builds) or the compiler's folder (MSYS2).
    get_filename_component(_cxx_bin ${CMAKE_CXX_COMPILER} DIRECTORY)
    set(_dll_dirs ${_cxx_bin})
    foreach (_root ${CMAKE_FIND_ROOT_PATH})
        list(APPEND _dll_dirs ${_root}/bin)
    endforeach ()
    add_custom_command(TARGET pikmin POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/mingw_dlls.py ${CMAKE_OBJDUMP} $<TARGET_FILE:pikmin>
                $<TARGET_FILE_DIR:pikmin> ${_dll_dirs}
        COMMENT "Copying the DLLs pikmin.exe needs" VERBATIM)
    install(TARGETS pikmin RUNTIME DESTINATION .)
    install(CODE "file(GLOB _dlls \"${CMAKE_CURRENT_BINARY_DIR}/*.dll\")
                  file(INSTALL \${_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")")
    set(PIKMIN_ORIG ${PROJECT_SOURCE_DIR}/orig/GPIP01)
    install(FILES ${PIKMIN_ORIG}/sys/boot.bin ${PIKMIN_ORIG}/sys/fst.bin ${PIKMIN_ORIG}/sys/paths.txt DESTINATION data/sys)
    install(DIRECTORY ${PIKMIN_ORIG}/files DESTINATION data)
    return()
endif ()

# Installation: binary, desktop entry and icon (e.g. cmake --install build/cmake with
# -DCMAKE_INSTALL_PREFIX=$HOME/.local). The desktop entry launches `pikmin` without arguments,
# and finds the game data installed below.
install(TARGETS pikmin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file(${PROJECT_SOURCE_DIR}/packaging/pikminrecomp.desktop.in ${CMAKE_BINARY_DIR}/pikminrecomp.desktop @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/pikminrecomp.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${PROJECT_SOURCE_DIR}/packaging/pikminrecomp.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${PROJECT_SOURCE_DIR}/packaging/pikminrecomp-256.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME pikminrecomp.png)

# Game data as an extracted tree (no ISO needed at runtime). Nintendo data: never distribute.
set(PIKMIN_ORIG ${PROJECT_SOURCE_DIR}/orig/GPIP01)
install(FILES ${PIKMIN_ORIG}/sys/boot.bin ${PIKMIN_ORIG}/sys/fst.bin ${PIKMIN_ORIG}/sys/paths.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pikminrecomp/data/sys)
install(DIRECTORY ${PIKMIN_ORIG}/files DESTINATION ${CMAKE_INSTALL_DATADIR}/pikminrecomp/data)
