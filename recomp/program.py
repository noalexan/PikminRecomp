"""Whole-program generation: translate every function, emit shards, table and HLE stubs."""
from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from recomp.emit import RecompError
from recomp.image import Function, Image
from recomp.translate import translate_function_state

_DATA_SECTIONS = {".data", ".rodata", ".sdata", ".sdata2"}


@dataclass
class Report:
    functions: int = 0
    overridden: int = 0
    unimpl: list[tuple[int, int, str]] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    unresolved_bcctr: list[tuple[int, str]] = field(default_factory=list)


def _sanitize(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def load_hooks(img: Image, path: Path) -> set[int]:
    """Functions with mode "hook": translated as f_<addr>_orig, every call goes to host hle_<name>."""
    per_fn = tomllib.loads(path.read_text()).get("functions", {})
    return {f.addr for f in img.functions if per_fn.get(f.name) == "hook"}


def load_overrides(img: Image, path: Path) -> set[int]:
    try:
        cfg = tomllib.loads(path.read_text())
    except tomllib.TOMLDecodeError as e:
        raise RecompError(f"{path}: invalid TOML: {e}") from None
    override = cfg.get("libraries", {}).get("override", [])
    if not isinstance(override, list) or not all(isinstance(x, str) for x in override):
        raise RecompError(f"{path}: [libraries] override must be a list of library names")
    present = {f.library for f in img.functions if f.library}
    unknown = sorted(set(override) - present)
    if unknown:
        raise RecompError(f"{path}: unknown libraries in [libraries] override: {', '.join(unknown)}")
    libs = set(override)
    per_fn = cfg.get("functions", {})
    known = {f.name for f in img.functions}
    for name, mode in per_fn.items():
        if name not in known:
            raise RecompError(f"{path}: unknown function {name!r} in [functions]")
        if mode not in ("override", "translate", "poll", "hook"):
            raise RecompError(f"{path}: {name!r} must be \"override\", \"translate\", \"poll\" or \"hook\", got {mode!r}")
    out = set()
    for f in img.functions:
        mode = per_fn.get(f.name)
        if mode == "override" or (mode is None and f.library in libs):
            out.add(f.addr)
    return out


def load_polls(img: Image, path: Path) -> set[int]:
    """Functions marked "poll": translated, but entry and loop heads are scheduling points (ppc_poll).

    For guest busy-wait loops whose exit depends on another thread or an interrupt: on the
    GameCube the interrupt would preempt the loop; here the loop must offer the chance.
    """
    per_fn = tomllib.loads(path.read_text()).get("functions", {})
    return {f.addr for f in img.functions if per_fn.get(f.name) == "poll"}


class ImageContext:
    """TranslateContext over a loaded Image."""

    def __init__(self, img: Image, overridden: set[int], hooked: set[int] = frozenset()):
        self.img = img
        self.overridden = overridden
        self.hooked = hooked
        self.hle_names = self._hle_names()
        self._tables = self._index_jump_tables()

    def _hle_names(self) -> dict[int, str]:
        names, used = {}, set()
        for f in self.img.functions:
            if (f.addr in self.overridden or f.addr in self.hooked) and f.addr not in names:
                n = "hle_" + _sanitize(f.name)
                if n in used:
                    n = f"{n}_{f.addr:08x}"
                used.add(n)
                names[f.addr] = n
        return names

    def _index_jump_tables(self) -> dict[int, list[list[int]]]:
        """Parent function addr -> tables (entry lists) whose entries all lie in that function."""
        out: dict[int, list[list[int]]] = {}
        for d in self.img.data:
            if d.section not in _DATA_SECTIONS or d.size < 8 or d.size % 4:
                continue
            try:
                entries = [self.img.read_u32(d.addr + 4 * k) for k in range(d.size // 4)]
            except Exception:
                continue
            f = self.img.function_containing(entries[0])
            if f is not None and all(f.addr <= e < f.addr + f.size for e in entries):
                out.setdefault(f.addr, []).append(entries)
        return out

    def symbol(self, f: Function) -> str:
        if f.addr in self.overridden or f.addr in self.hooked:
            return self.hle_names[f.addr]
        return f"f_{f.addr:08x}"

    def read_u32(self, addr: int) -> int:
        return self.img.read_u32(addr)

    def call_target(self, addr: int) -> str | None:
        f = self.img.function_at(addr)
        return None if f is None else self.symbol(f)

    def is_function_body(self, addr: int) -> str | None:
        f = self.img.function_containing(addr)
        return f.name if f is not None and f.addr != addr else None

    def jump_table(self, addr: int) -> tuple[int, int] | None:
        d = self.img.data_at(addr)
        if d is None or d.section not in _DATA_SECTIONS or d.size < 4:
            return None
        return addr, d.size // 4

    def jump_tables_into(self, start: int, end: int) -> list[int]:
        parent = self.img.function_containing(start)
        if parent is None:
            return []
        out = []
        for entries in self._tables.get(parent.addr, []):
            if all(start <= e < end for e in entries):
                out += entries
        return out


_LOOKUP_MUL = 0x9E3779B1  # Fibonacci hashing of the word address


def _lookup_hash(addr: int, size: int) -> int:
    return (((addr >> 2) * _LOOKUP_MUL) & 0xFFFFFFFF) >> (32 - size.bit_length() + 1)


def _write_if_changed(path: Path, text: str) -> None:
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def _unique_entries(img: Image) -> list[Function]:
    seen, out = set(), []
    for f in img.functions:
        if f.addr not in seen:
            seen.add(f.addr)
            out.append(f)
    return out


def generate(img: Image, overrides: Path, out: Path, shards: int = 64) -> Report:
    out.mkdir(parents=True, exist_ok=True)
    overridden = load_overrides(img, overrides)
    polls = load_polls(img, overrides)
    hooked = load_hooks(img, overrides)
    ctx = ImageContext(img, overridden, hooked)
    report = Report()
    functions = _unique_entries(img)

    # Translate, grouped by owning object so each object lands in one shard.
    by_obj: dict[tuple[str, str], list[str]] = {}
    for f in functions:
        if f.addr in overridden:
            report.overridden += 1
            continue
        if f.size <= 0:
            continue
        sym = f"f_{f.addr:08x}_orig" if f.addr in hooked else ctx.symbol(f)
        try:
            src, st = translate_function_state(ctx, sym, f.addr, f.addr + f.size, f.name, poll=f.addr in polls)
        except RecompError as e:
            report.errors.append(f"{f.name} @ 0x{f.addr:08x}: {e}")
            src = (f"void {sym}(ppc_ctx *c) {{ /* {f.name}: translation failed */ "
                   f"ppc_unimpl(c, 0x{f.addr:08x}u, 0u); }}\n")
            st = None
        if st is not None:
            report.unimpl += [(a, w, f.name) for a, w in st.unimpl]
            report.unresolved_bcctr += [(a, f.name) for a in st.unresolved_bcctr]
        report.functions += 1
        by_obj.setdefault((f.library or "", f.obj), []).append(src)

    # Balance whole objects across shards (largest first, into the lightest shard).
    bins: list[list[str]] = [[] for _ in range(shards)]
    sizes = [0] * shards
    for key in sorted(by_obj, key=lambda k: -sum(len(s) for s in by_obj[k])):
        k = sizes.index(min(sizes))
        bins[k].append(f"/* ---- {key[0]} {key[1]} ---- */\n" + "\n".join(by_obj[key]))
        sizes[k] += sum(len(s) for s in by_obj[key])
    width = len(str(shards - 1))
    for k, parts in enumerate(bins):
        _write_if_changed(out / f"gen_{k:0{width}d}.c", '#include "gen.h"\n\n' + "\n".join(parts))

    entries = sorted({f.addr: ctx.symbol(f) for f in functions if f.size > 0 or f.addr in overridden}.items())
    protos = "\n".join(f"void {sym}(ppc_ctx *c);" for _, sym in entries)
    protos += "".join(f"\nvoid f_{a:08x}_orig(ppc_ctx *c);" for a in sorted(hooked))
    _write_if_changed(out / "gen.h", (
        "/* Generated by recomp. Do not edit. */\n#ifndef PIKMIN_GEN_H\n#define PIKMIN_GEN_H\n"
        "#include <math.h>\n#include \"memory.h\"\n#include \"ops.h\"\n#include \"ppc.h\"\n#include \"ps.h\"\n\n"
        f"{protos}\n\n#endif\n"))

    # Indirect calls (virtual calls, function pointers) resolve through an open-addressing hash
    # table built here: one probe for almost every lookup, instead of a binary search.
    hsize = 1
    while hsize < 4 * len(entries):
        hsize *= 2
    slots: list = [None] * hsize
    for a, sym in entries:
        i = _lookup_hash(a, hsize)
        while slots[i] is not None:
            i = (i + 1) & (hsize - 1)
        slots[i] = (a, sym)
    rows = ",\n".join(f"    {{0x{e[0]:08x}u, {e[1]}}}" if e else "    {0, 0}" for e in slots)
    _write_if_changed(out / "functable.c", (
        "/* Generated by recomp. Do not edit. */\n#include <stdio.h>\n#include <stdlib.h>\n#include \"gen.h\"\n\n"
        "typedef struct { uint32_t addr; ppc_fn fn; } ppc_func_entry;\n"
        f"#define PPC_FUNC_SLOTS {hsize}u\n"
        f"static const ppc_func_entry ppc_funcs[PPC_FUNC_SLOTS] = {{\n{rows}\n}};\n\n"
        "ppc_fn ppc_lookup(uint32_t addr) {\n"
        f"    uint32_t i = ((addr >> 2) * {_LOOKUP_MUL:#x}u) >> {32 - hsize.bit_length() + 1};\n"
        "    for (;;) {\n"
        "        const ppc_func_entry *e = &ppc_funcs[i];\n"
        "        if (e->addr == addr && e->fn) return e->fn;\n"
        "        if (!e->fn) break;\n"
        "        i = (i + 1) & (PPC_FUNC_SLOTS - 1);\n"
        "    }\n"
        "    fprintf(stderr, \"ppc_lookup: no function at 0x%08x\\n\", addr);\n"
        "    abort();\n"
        "}\n"))

    stubs = "\n".join(
        f"__attribute__((weak)) void {ctx.hle_names[f.addr]}(ppc_ctx *c) {{ (void)c; ppc_hle_missing(\"{f.name}\", 0x{f.addr:08x}u); }}"
        for f in functions if f.addr in overridden)
    # Hooks without a host implementation fall through to the original translation.
    hook_stubs = "\n".join(
        f"__attribute__((weak)) void {ctx.hle_names[f.addr]}(ppc_ctx *c) {{ f_{f.addr:08x}_orig(c); }}"
        for f in functions if f.addr in hooked)
    _write_if_changed(out / "hle_stubs.c",
                      f"/* Generated by recomp. Placeholder HLE stubs. */\n#include \"gen.h\"\n\n{stubs}\n{hook_stubs}\n")

    lines = [f"functions translated: {report.functions}", f"functions overridden: {report.overridden}",
             f"unimplemented instructions: {len(report.unimpl)}", f"errors: {len(report.errors)}",
             f"unresolved bcctr (dynamic dispatch via ppc_lookup): {len(report.unresolved_bcctr)}"]
    lines += [f"  unimpl 0x{a:08x} {w:08x} {n}" for a, w, n in report.unimpl]
    lines += [f"  error {e}" for e in report.errors]
    lines += [f"  bcctr 0x{a:08x} {n}" for a, n in report.unresolved_bcctr]
    _write_if_changed(out / "report.txt", "\n".join(lines) + "\n")
    (out / "stamp").write_text("")
    return report
