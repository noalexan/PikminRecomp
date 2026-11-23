#!/usr/bin/env python3
"""Windows (COFF) builds: weak definitions don't reliably lose to strong ones across archives, so
keep only the generated hle_* stubs the runtime doesn't define, as ordinary definitions.

usage: strong_hle_stubs.py NM RUNTIME_LIB IN_STUBS OUT_STUBS
"""
import re
import subprocess
import sys

nm, lib, src, dst = sys.argv[1:5]
out = subprocess.run([nm, "-g", "--defined-only", lib], capture_output=True, text=True, check=True).stdout
defined = {line.split()[-1] for line in out.splitlines() if len(line.split()) >= 3 and line.split()[1] in "TtDdBbRr"}
lines = []
for line in open(src, encoding="utf-8"):
    m = re.match(r"__attribute__\(\(weak\)\) void (hle_\w+)\(", line)
    if m:
        if m.group(1) in defined:
            continue
        line = line.replace("__attribute__((weak)) ", "", 1)
    lines.append(line)
open(dst, "w", encoding="utf-8").write("".join(lines))
