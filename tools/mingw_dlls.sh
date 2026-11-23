#!/usr/bin/env bash
# Copy the MinGW DLLs an .exe needs (recursively) next to it.
# usage: [OUT_DIR=dir] tools/mingw_dlls.sh EXE_OR_DLL [SYSROOT_BIN]   (default: next to EXE)
set -eu
exe="$1"; bin="${2:-/usr/x86_64-w64-mingw32/sys-root/mingw/bin}"; dir="${OUT_DIR:-$(dirname "$exe")}"
todo=("$exe"); declare -A seen
while ((${#todo[@]})); do
    f="${todo[0]}"; todo=("${todo[@]:1}")
    for dll in $(x86_64-w64-mingw32-objdump -p "$f" | awk '/DLL Name:/ {print $3}'); do
        key="${dll,,}"; [[ -n "${seen[$key]:-}" ]] && continue; seen[$key]=1
        src=$(find "$bin" -maxdepth 1 -iname "$dll" | head -1)
        [[ -z "$src" ]] && continue   # a Windows system DLL
        cp -u "$src" "$dir/"; todo+=("$src"); echo "$dll"
    done
done
