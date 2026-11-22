"""Load main.dol plus its CodeWarrior link map into a queryable image."""
from __future__ import annotations

import bisect
import re
import struct
from dataclasses import dataclass
from pathlib import Path

CODE_SECTIONS = {".init", ".text"}
BSS_SECTIONS = {".bss", ".sbss", ".sbss2"}

# "  <secoff> <size> <vaddr> [<align>] <name> [(entry of <parent>)]" then "\t<lib> <obj>" / "\t<obj>"
_HEAD_RE = re.compile(
    r"^\s+([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8})\s+(?:\d+ )?(\S+)(?: \(entry of (\S+)\))?\s*$"
)
_MEMMAP_RE = re.compile(r"^\s+(\S+)\s+([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})$")


class ImageError(Exception):
    pass


@dataclass(frozen=True)
class Function:
    name: str
    addr: int
    size: int
    library: str | None
    obj: str
    parent: str | None


@dataclass(frozen=True)
class DataSymbol:
    name: str
    addr: int
    size: int
    section: str


@dataclass(frozen=True)
class Section:
    addr: int
    data: bytes
    is_text: bool


class Image:
    def __init__(self, sections: list[Section], functions: list[Function], data: list[DataSymbol]):
        self.sections = sorted(sections, key=lambda s: s.addr)
        self.functions = sorted(functions, key=lambda f: (f.addr, f.parent is not None))
        self.data = sorted(data, key=lambda d: d.addr)
        self._entries = {f.addr: f for f in reversed(self.functions)}
        self._parents = [f for f in self.functions if f.parent is None]
        self._parent_addrs = [f.addr for f in self._parents]
        self._data_by_addr = {}
        for d in self.data:
            self._data_by_addr.setdefault(d.addr, d)

    def read_u32(self, addr: int) -> int:
        for s in self.sections:
            if s.addr <= addr and addr + 4 <= s.addr + len(s.data):
                return struct.unpack_from(">I", s.data, addr - s.addr)[0]
        raise ImageError(f"address 0x{addr:08x} is outside the loaded DOL sections")

    def function_at(self, addr: int) -> Function | None:
        return self._entries.get(addr)

    def function_containing(self, addr: int) -> Function | None:
        i = bisect.bisect_right(self._parent_addrs, addr) - 1
        if i >= 0:
            f = self._parents[i]
            if addr < f.addr + f.size:
                return f
        return None

    def data_at(self, addr: int) -> DataSymbol | None:
        return self._data_by_addr.get(addr)


def _read_dol(path: Path) -> list[Section]:
    try:
        raw = path.read_bytes()
    except FileNotFoundError:
        raise ImageError(f"{path}: DOL file not found") from None
    if len(raw) < 0x100:
        raise ImageError(f"{path}: too small to be a DOL")
    sections = []
    for i in range(18):
        off, addr, size = (struct.unpack_from(">I", raw, base + i * 4)[0] for base in (0x00, 0x48, 0x90))
        if size:
            if off + size > len(raw):
                raise ImageError(f"{path}: section {i} extends past end of file")
            sections.append(Section(addr, raw[off:off + size], i < 7))
    return sections


def _split_owner(tail: str) -> tuple[str | None, str]:
    parts = tail.split()
    if len(parts) >= 2:
        return parts[0], parts[1]
    return None, parts[0] if parts else ""


def load_image(dol: Path, map_path: Path) -> Image:
    sections = _read_dol(dol)
    try:
        text = map_path.read_bytes().decode("latin-1").replace("\r", "")
    except FileNotFoundError:
        raise ImageError(f"{map_path}: link map not found") from None

    raw_funcs: list[tuple[str, int, int, str | None, str, str | None]] = []
    markers: list[tuple[int, int]] = []  # per-object code section extents
    data: list[DataSymbol] = []
    memmap: list[tuple[str, int, int]] = []
    section = None
    in_memmap = False
    for line in text.split("\n"):
        if line.endswith(" section layout"):
            section, in_memmap = line.split()[0], False
            continue
        if line.startswith("Memory map:"):
            in_memmap = True
            continue
        if in_memmap:
            m = _MEMMAP_RE.match(line)
            if m:
                memmap.append((m[1], int(m[2], 16), int(m[3], 16)))
            elif line.strip() and not line.startswith(" "):
                in_memmap = False
            continue
        if section is None:
            continue
        head, _, tail = line.partition("\t")
        m = _HEAD_RE.match(head)
        if not m:
            continue
        name = m[4]
        size, addr = int(m[2], 16), int(m[3], 16)
        if name == section:
            if section in CODE_SECTIONS:
                markers.append((addr, size))
            continue
        if name.startswith("..."):
            continue
        library, obj = _split_owner(tail)
        if section in CODE_SECTIONS:
            raw_funcs.append((name, addr, size, library, obj, m[5]))
        else:
            data.append(DataSymbol(name, addr, size, section))

    if not raw_funcs:
        raise ImageError(f"{map_path}: no code symbols found; is this a CodeWarrior link map?")

    dol_addrs = {s.addr for s in sections}
    for name, addr, size in memmap:
        if size and name not in BSS_SECTIONS and addr not in dol_addrs and name.startswith((".", "ext")):
            raise ImageError(f"{map_path}: section {name} at 0x{addr:08x} does not match {dol}")

    by_name = {}
    for name, addr, size, _, _, parent in raw_funcs:
        if parent is None:
            by_name.setdefault(name, (addr, size))
    functions = []
    for name, addr, size, library, obj, parent in raw_funcs:
        if parent is not None:
            if parent in by_name:
                paddr, psize = by_name[parent]
            elif parent in CODE_SECTIONS:
                paddr, psize = next(((a, sz) for a, sz in markers if a <= addr < a + sz), (addr, 0))
            else:
                raise ImageError(f"{map_path}: {name} is an entry of unknown function {parent}")
            size = paddr + psize - addr
        functions.append(Function(name, addr, size, library, obj, parent))
    return Image(sections, functions, data)
