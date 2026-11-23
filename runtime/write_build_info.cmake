# cmake -DSRC=<repo> -DIN=<build_info.cpp.in> -DOUT=<build_info.cpp> -P write_build_info.cmake
# Writes OUT with the current git hash (+ "-dirty"), touching it only when the content changes.
execute_process(COMMAND git -C "${SRC}" rev-parse --short HEAD OUTPUT_VARIABLE PIKMIN_GIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE rc)
if (NOT rc EQUAL 0 OR PIKMIN_GIT_HASH STREQUAL "")
    set(PIKMIN_GIT_HASH "unknown")
else ()
    execute_process(COMMAND git -C "${SRC}" diff --quiet HEAD RESULT_VARIABLE dirty ERROR_QUIET)
    if (NOT dirty EQUAL 0)
        string(APPEND PIKMIN_GIT_HASH "-dirty")
    endif ()
endif ()
configure_file("${IN}" "${OUT}.tmp" @ONLY)
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
