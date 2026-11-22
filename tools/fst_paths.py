#!/usr/bin/env python3
"""Write the path index for a directory-backed disc: line i = files/-relative path of FST entry i.

Directories and the root get an empty line. Names are decoded from Shift-JIS exactly as
extract_gcm.py does, so the paths match the extracted tree byte for byte.
Usage: fst_paths.py sys/fst.bin OUT
"""
import struct
import sys
from pathlib import Path


def fst_paths(fst: bytes) -> list[str]:
    count = struct.unpack_from(">I", fst, 8)[0]
    names = count * 12
    out = [""] * count
    stack = [(count, "")]
    for i in range(1, count):
        while i >= stack[-1][0]:
            stack.pop()
        w0, _, w2 = struct.unpack_from(">III", fst, i * 12)
        end = fst.index(b"\0", names + (w0 & 0xFFFFFF))
        name = fst[names + (w0 & 0xFFFFFF):end].decode("shift_jis")
        path = f"{stack[-1][1]}/{name}" if stack[-1][1] else name
        if w0 >> 24:
            stack.append((w2, path))
        else:
            out[i] = path
    return out


if __name__ == "__main__":
    lines = fst_paths(Path(sys.argv[1]).read_bytes())
    assert not any("\n" in p for p in lines)
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n", encoding="utf-8")
