#!/usr/bin/env python3
"""Copy the MinGW DLLs a Windows .exe/.dll needs (recursively) into a folder. Works on Linux (cross
builds) and natively under MSYS2. Windows' own DLLs aren't in the search dirs and are skipped.

usage: mingw_dlls.py OBJDUMP FILE OUT_DIR SEARCH_DIR...
"""
import os
import shutil
import subprocess
import sys

objdump, first, out, *search = sys.argv[1:]
index = {}
for d in search:
    if os.path.isdir(d):
        for name in os.listdir(d):
            if name.lower().endswith(".dll"):
                index.setdefault(name.lower(), os.path.join(d, name))
todo, seen = [first], set()
while todo:
    f = todo.pop()
    dump = subprocess.run([objdump, "-p", f], capture_output=True, text=True, check=True).stdout
    for line in dump.splitlines():
        if "DLL Name:" not in line:
            continue
        dll = line.split("DLL Name:")[1].strip()
        key = dll.lower()
        if key in seen or key not in index:
            continue
        seen.add(key)
        dst = os.path.join(out, os.path.basename(index[key]))
        if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(index[key]):
            shutil.copy2(index[key], dst)
        todo.append(index[key])
        print(dll)
