"""Semantics harness: assemble PPC → translate → compile C → run → parse state.

Assembles with `powerpc64-linux-gnu-as -a32 -mbig -mgekko` at guest address 0x80003000.
A trailing `blr` is appended so snippets return.
"""
from __future__ import annotations

import json
import struct
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from recomp.translate import translate_words

ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = Path(__file__).with_name("harness_main.c.in")
BASE = 0x80003000


@dataclass
class State:
    r: list[int]
    f: list[tuple[float, float]]
    gqr: list[int]
    cr: int
    xer: int
    lr: int
    ctr: int
    fpscr: int
    mmio: list[tuple[int, int, int]]
    c_source: str
    _ram: bytes

    def mem(self, addr: int, n: int) -> bytes:
        phys = addr & 0x1FFFFFFF
        return self._ram[phys:phys + n]

    def cr_field(self, n: int) -> int:
        return (self.cr >> (28 - 4 * n)) & 0xF


def assemble(asm: str) -> list[int]:
    with tempfile.TemporaryDirectory() as d:
        src, obj, raw = Path(d, "t.s"), Path(d, "t.o"), Path(d, "t.bin")
        src.write_text(".text\n" + asm + "\nblr\n")
        subprocess.run(["powerpc64-linux-gnu-as", "-a32", "-mbig", "-mgekko", "-o", obj, src],
                       check=True, capture_output=True, text=True)
        subprocess.run(["powerpc64-linux-gnu-objcopy", "-O", "binary", "-j", ".text", obj, raw], check=True)
        data = raw.read_bytes()
    return list(struct.unpack(f">{len(data) // 4}I", data))


def _dbits(x: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def _init_lines(regs: dict, mem: dict[int, bytes]) -> str:
    out = []
    for k, v in regs.items():
        if k[0] == "r" and k[1:].isdigit():
            out.append(f"c->r[{int(k[1:])}] = {v & 0xFFFFFFFF}u;")
        elif k[0] == "f" and k[1:].isdigit():
            p0, p1 = v if isinstance(v, tuple) else (v, v)
            out.append(f"c->f[{int(k[1:])}].ps0 = ppc_bitsd({_dbits(p0)}ull);")
            out.append(f"c->f[{int(k[1:])}].ps1 = ppc_bitsd({_dbits(p1)}ull);")
        elif k.startswith("gqr"):
            out.append(f"c->gqr[{int(k[3:])}] = {v}u;")
        elif k in ("cr", "xer", "lr", "ctr", "fpscr"):
            out.append(f"c->{k} = {v & 0xFFFFFFFF}u;")
        else:
            raise KeyError(k)
    for addr, data in mem.items():
        for i, b in enumerate(data):
            out.append(f"ram[{(addr & 0x1FFFFFFF) + i}] = {b};")
    return "\n".join("    " + line for line in out)


def run_case(asm: str, regs: dict | None = None, mem: dict[int, bytes] | None = None,
             extra_c: str = "", **translate_kwargs) -> State:
    regs, mem = regs or {}, mem or {}
    words = assemble(asm)
    fn = translate_words(words, BASE, "f_test", **translate_kwargs)
    src = (TEMPLATE.read_text().replace("@EXTRA@", extra_c).replace("@FUNCTION@", fn)
           .replace("@INIT@", _init_lines(regs, mem)))
    with tempfile.TemporaryDirectory() as d:
        c_file, exe, ram = Path(d, "h.c"), Path(d, "h"), Path(d, "ram.bin")
        c_file.write_text(src)
        cc = subprocess.run(["cc", "-std=c17", "-O1", f"-I{ROOT / 'runtime/cpu'}", "-o", exe, c_file, "-lm"],
                            capture_output=True, text=True)
        if cc.returncode:
            raise AssertionError(f"C compile failed:\n{cc.stderr}\n--- source ---\n{fn}")
        run = subprocess.run([exe, ram], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(f"harness exited {run.returncode}: {run.stderr}\n--- source ---\n{fn}")
        j = json.loads(run.stdout)
        ram_bytes = ram.read_bytes()
    to_d = lambda u: struct.unpack("<d", struct.pack("<Q", u))[0]
    return State(r=j["r"], f=[(to_d(a), to_d(b)) for a, b in j["f"]], gqr=j["gqr"], cr=j["cr"], xer=j["xer"],
                 lr=j["lr"], ctr=j["ctr"], fpscr=j["fpscr"], mmio=[tuple(m) for m in j["mmio"]],
                 c_source=fn, _ram=ram_bytes)
