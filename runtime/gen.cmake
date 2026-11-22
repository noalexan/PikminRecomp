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
target_compile_options(pikmin_gen PRIVATE -O2 -fno-strict-aliasing -fwrapv -ffp-contract=off -w)
target_link_libraries(pikmin_gen PUBLIC m)

add_library(pikmin_stubs STATIC ${GEN_DIR}/hle_stubs.c ${PROJECT_SOURCE_DIR}/runtime/cpu/stubs.c)
target_link_libraries(pikmin_stubs PUBLIC pikmin_gen)

add_executable(pikmin_link_check ${PROJECT_SOURCE_DIR}/runtime/link_check.c)
target_link_libraries(pikmin_link_check PRIVATE pikmin_gen pikmin_stubs)

# The game: generated code + runtime. pikmin_rt is linked whole so its strong hle_* definitions
# always replace the weak generated stubs (a static archive would otherwise satisfy them with the
# first weak definition it finds).
add_library(pikmin_hle_stubs STATIC ${GEN_DIR}/hle_stubs.c)
target_link_libraries(pikmin_hle_stubs PUBLIC pikmin_gen)
add_executable(pikmin ${PROJECT_SOURCE_DIR}/runtime/main.cpp)
target_compile_options(pikmin PRIVATE -O2 -Wall -Wextra)
target_link_libraries(pikmin PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,pikmin_rt>" pikmin_gen pikmin_hle_stubs)
