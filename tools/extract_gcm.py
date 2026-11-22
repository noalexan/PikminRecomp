#!/usr/bin/env python3
"""Extract a GameCube disc image (GCM/ISO) into sys/ and files/.

Layout matches Dolphin's "Extract Entire Disc":
  sys/boot.bin, sys/bi2.bin, sys/apploader.img, sys/main.dol, sys/fst.bin
  files/<filesystem tree>
"""
import argparse
import struct
import sys
from pathlib import Path

GC_MAGIC = 0xC2339F3D


def u32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def dol_size(header: bytes) -> int:
    # 7 text + 11 data sections: offsets at 0x00, sizes at 0x90.
    end = 0x100
    for i in range(18):
        off = u32(header, 0x00 + i * 4)
        size = u32(header, 0x90 + i * 4)
        if off and size:
            end = max(end, off + size)
    return end


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()

    with args.iso.open("rb") as f:
        def read(off, n):
            f.seek(off)
            data = f.read(n)
            if len(data) != n:
                sys.exit(f"short read at {off:#x}")
            return data

        boot = read(0, 0x440)
        if u32(boot, 0x1C) != GC_MAGIC:
            sys.exit("not a GameCube disc image")
        game_id = boot[:6].decode("ascii")
        dol_off, fst_off, fst_len = u32(boot, 0x420), u32(boot, 0x424), u32(boot, 0x428)

        sysdir = args.out / "sys"
        filesdir = args.out / "files"
        sysdir.mkdir(parents=True, exist_ok=True)
        filesdir.mkdir(parents=True, exist_ok=True)

        (sysdir / "boot.bin").write_bytes(boot)
        (sysdir / "bi2.bin").write_bytes(read(0x440, 0x2000))

        ldr_hdr = read(0x2440, 0x20)
        ldr_len = 0x20 + u32(ldr_hdr, 0x14) + u32(ldr_hdr, 0x18)
        (sysdir / "apploader.img").write_bytes(read(0x2440, ldr_len))

        dol_hdr = read(dol_off, 0x100)
        (sysdir / "main.dol").write_bytes(read(dol_off, dol_size(dol_hdr)))

        fst = read(fst_off, fst_len)
        (sysdir / "fst.bin").write_bytes(fst)

        count = u32(fst, 8)
        names_base = count * 12

        def name_at(off):
            end = fst.index(b"\0", names_base + off)
            return fst[names_base + off:end].decode("shift_jis")

        # Walk the flat FST, tracking directory end indices.
        stack = [(count, filesdir)]
        nfiles = 0
        for i in range(1, count):
            while stack and i >= stack[-1][0]:
                stack.pop()
            parent = stack[-1][1]
            w0, w1, w2 = struct.unpack_from(">III", fst, i * 12)
            is_dir = w0 >> 24
            path = parent / name_at(w0 & 0xFFFFFF)
            if is_dir:
                path.mkdir(exist_ok=True)
                stack.append((w2, path))
            else:
                path.write_bytes(read(w1, w2))
                nfiles += 1

    print(f"{game_id}: main.dol {dol_size(dol_hdr):#x} bytes, {nfiles} files -> {args.out}")


if __name__ == "__main__":
    main()
