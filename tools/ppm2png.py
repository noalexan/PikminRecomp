#!/usr/bin/env python3
"""Convert a binary PPM (P6) to PNG, optionally downscaled: ppm2png.py IN.ppm OUT.png [SCALE]"""
import struct
import sys
import zlib
from pathlib import Path


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    data = src.read_bytes()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    ow, oh = w // scale, h // scale
    rows = []
    for y in range(oh):
        row = bytearray(b"\x00")
        base = (y * scale) * w * 3
        for x in range(ow):
            i = base + x * scale * 3
            row += px[i:i + 3]
        rows.append(bytes(row))
    raw = zlib.compress(b"".join(rows), 6)

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", ow, oh, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", raw) + chunk(b"IEND", b"")
    dst.write_bytes(png)


if __name__ == "__main__":
    main()
