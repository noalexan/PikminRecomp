#!/usr/bin/env python3
"""Resolve a pikmin crash log to guest function names: resolve_bt.py LOG

Reads the last "=== pikmin crash:" report in LOG (a run log or captured stderr; older logs without
one fall back to the last "backtrace" marker). Prints each backtrace frame with the guest function
and its object from build.map, then the guest return addresses (lr, last HLE lr).
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from recomp.image import load_image  # noqa: E402

root = Path(__file__).resolve().parent.parent
img = load_image(root / "orig/GPIP01/sys/main.dol", root / "orig/GPIP01/files/dataDir/build.map")
text = Path(sys.argv[1]).read_text(errors="replace")

start = text.rfind("=== pikmin crash:")
if start < 0:
    start = text.rfind("backtrace")
report = text[start:] if start >= 0 else text
end = report.find("=== end ===")
if end >= 0:
    report = report[:end]


def describe(addr):
    f = img.function_containing(addr) if hasattr(img, "function_containing") else img.function_at(addr)
    return f"{f.name}  ({f.library} {f.obj})" if f else "?"


print(report.splitlines()[0] if report else "(no report)")
print("frames:")
for m in re.finditer(r"\((f_([0-9a-f]{8})\w*|hle_\w+|_ZN\w+|\w+)\+0x[0-9a-f]+\)", report):
    print(" ", m[1], describe(int(m[2], 16)) if m[2] else "")
for label, m in [("lr", re.search(r"^lr 0x([0-9a-f]{8})", report, re.M)),
                 ("last HLE lr", re.search(r"^last HLE .* lr 0x([0-9a-f]{8})", report, re.M))]:
    if m:
        print(f"{label} 0x{m[1]}: {describe(int(m[1], 16))}")
