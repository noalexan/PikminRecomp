#!/usr/bin/env python3
"""Convert a GameCube DOL plus its CodeWarrior link map into a symbolized ELF.

Section names, BSS extents and symbols come from the map; bytes come from the DOL.
Usage: dol2elf.py main.dol build.map out.elf
"""
import re
import struct
import sys
from pathlib import Path

LINKER_RE = re.compile(r"^\s+(\S+) ([0-9a-f]{8})$")
LAYOUT_RE = re.compile(r"^\s+([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8})\s+\d+ (\S+)\s+(.*)$")
MEMMAP_RE = re.compile(r"^\s+(\S+)\s+([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})$")
CODE_SECTIONS = {".init", ".text"}
BSS_SECTIONS = {".bss", ".sbss", ".sbss2"}


def parse_dol(data):
    segs = []
    for i in range(18):
        off, addr, size = (struct.unpack_from(">I", data, base + i * 4)[0] for base in (0x00, 0x48, 0x90))
        if size:
            segs.append((addr, data[off:off + size], i < 7))
    return segs, struct.unpack_from(">I", data, 0xE0)[0]


def parse_map(text):
    sections, symbols, current = [], [], None
    in_memmap = in_linker = False
    linker = []
    for line in text.splitlines():
        if line.startswith("Linker generated symbols:"):
            in_linker, in_memmap = True, False
            continue
        if in_linker:
            m = LINKER_RE.match(line)
            if m:
                linker.append((m[1], int(m[2], 16)))
            continue
        if line.endswith(" section layout"):
            current = line.split()[0]
            in_memmap = False
            continue
        if line.startswith("Memory map:"):
            in_memmap = True
            continue
        if in_memmap:
            m = MEMMAP_RE.match(line)
            if m:
                sections.append((m[1], int(m[2], 16), int(m[3], 16)))
            continue
        m = LAYOUT_RE.match(line)
        if m and current:
            name = m[4]
            if name == current or name.startswith("..."):
                continue  # per-object section markers / aliases
            symbols.append((name, int(m[3], 16), int(m[2], 16), current, m[5].strip()))
    return sections, symbols, linker


class Strtab:
    def __init__(self):
        self.data = bytearray(b"\0")
        self.index = {"": 0}

    def add(self, s):
        if s not in self.index:
            self.index[s] = len(self.data)
            self.data += s.encode("utf-8", "replace") + b"\0"
        return self.index[s]


def main():
    dol_path, map_path, out_path = map(Path, sys.argv[1:4])
    segs, entry = parse_dol(dol_path.read_bytes())
    map_sections, symbols, linker = parse_map(map_path.read_text("latin-1"))

    # Build section list: loaded sections from DOL (named via map), BSS from map.
    by_addr = {addr: name for name, addr, _ in map_sections}
    secs = []  # (name, addr, bytes|None, size, is_code)
    for addr, data, is_text in sorted(segs):
        name = by_addr.get(addr, f".seg_{addr:08x}")
        secs.append((name, addr, data, len(data), is_text))
    for name, addr, size in map_sections:
        if name in BSS_SECTIONS and size:
            secs.append((name, addr, None, size, False))
    secs.sort(key=lambda s: s[1])

    def section_index(addr):
        for i, (_, a, _, size, _) in enumerate(secs, start=1):
            if a <= addr < a + max(size, 1):
                return i
        return 0

    shstr, strtab = Strtab(), Strtab()
    locals_, globals_ = [], []
    for name, addr, size, sec, obj in symbols:
        shndx = section_index(addr)
        if not shndx:
            continue
        stype = 2 if sec in CODE_SECTIONS else 1  # FUNC / OBJECT
        local = name.startswith("@")
        (locals_ if local else globals_).append((strtab.add(name), addr, size, (0 if local else 1) << 4 | stype, shndx))

    for name, addr in linker:  # _SDA_BASE_ etc.; Ghidra derives r13/r2 from these
        globals_.append((strtab.add(name), addr, 0, 1 << 4, section_index(addr) or 0xFFF1))

    symtab = bytearray(16)  # null symbol
    for st_name, value, size, info, shndx in locals_ + globals_:
        symtab += struct.pack(">IIIBBH", st_name, value, size, info, 0, shndx)
    first_global = 1 + len(locals_)

    # Layout: ehdr, phdrs, section data, symtab, strtab, shstrtab, shdrs.
    loaded = [s for s in secs if s[2] is not None]
    phnum = len(loaded)
    off = 52 + 32 * phnum
    body = bytearray()
    shdrs, phdrs = [bytes(40)], []
    for name, addr, data, size, is_code in secs:
        flags = 0x2 | (0x4 if is_code else 0x1)  # ALLOC | EXECINSTR or WRITE
        if data is None:
            shdrs.append(struct.pack(">10I", shstr.add(name), 8, flags, addr, off + len(body), size, 0, 0, 32, 0))
            continue
        while (off + len(body)) % 32:
            body += b"\0"
        foff = off + len(body)
        body += data
        shdrs.append(struct.pack(">10I", shstr.add(name), 1, flags, addr, foff, size, 0, 0, 32, 0))
        phdrs.append(struct.pack(">8I", 1, foff, addr, addr, size, size, 5 if is_code else 6, 32))

    nsecs = len(secs)
    for name, data, sh_type, link, info, entsize, align in (
        (".symtab", symtab, 2, nsecs + 2, first_global, 16, 4),
        (".strtab", strtab.data, 3, 0, 0, 0, 1),
    ):
        while (off + len(body)) % 4:
            body += b"\0"
        shdrs.append(struct.pack(">10I", shstr.add(name), sh_type, 0, 0, off + len(body), len(data), link, info, align, entsize))
        body += data
    shstrndx = len(shdrs)
    shdrs.append(struct.pack(">10I", shstr.add(".shstrtab"), 3, 0, 0, off + len(body), 0, 0, 0, 1, 0))
    shstr_off = off + len(body)
    body += shstr.data
    shdrs[-1] = struct.pack(">10I", shstr.index[".shstrtab"], 3, 0, 0, shstr_off, len(shstr.data), 0, 0, 1, 0)
    while (off + len(body)) % 4:
        body += b"\0"
    shoff = off + len(body)

    ehdr = b"\x7fELF\x01\x02\x01" + bytes(9) + struct.pack(
        ">HHIIIIIHHHHHH", 2, 20, 1, entry, 52, shoff, 0, 52, 32, phnum, 40, len(shdrs), shstrndx)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(ehdr + b"".join(phdrs) + body + b"".join(shdrs))
    print(f"{out_path}: {len(secs)} sections, {len(locals_) + len(globals_)} symbols, entry {entry:#x}")


if __name__ == "__main__":
    main()
