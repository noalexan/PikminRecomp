#!/usr/bin/env python3
"""Resolve f_<addr> frames in a pikmin stderr log to link-map names: resolve_bt.py LOG [MARKER]"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from recomp.image import load_image  # noqa: E402

root = Path(__file__).resolve().parent.parent
img = load_image(root / "orig/GPIP01/sys/main.dol", root / "orig/GPIP01/files/dataDir/build.map")
text = Path(sys.argv[1]).read_text()
marker = sys.argv[2] if len(sys.argv) > 2 else "backtrace"
text = text[text.find(marker):]
for m in re.finditer(r"\((f_([0-9a-f]{8})|hle_\w+|_ZN\w+)", text):
    f = img.function_at(int(m[2], 16)) if m[2] else None
    print(" ", m[1], f.name if f else "")
