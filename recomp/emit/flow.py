"""Branches, calls and CR logical instructions."""
from __future__ import annotations

from recomp.decode import Insn
from recomp.emit import FnState, emits


def branch_target(i: Insn) -> int | None:
    """Static target of b/bc, else None."""
    if i.op == "b":
        return (i.a["li"] if i.a["aa"] else i.addr + i.a["li"]) & 0xFFFFFFFF
    if i.op == "bc":
        return (i.a["bd"] if i.a["aa"] else i.addr + i.a["bd"]) & 0xFFFFFFFF
    return None


def is_terminator(i: Insn) -> bool:
    """Unconditional transfer with no fall-through."""
    if i.op == "b":
        return not i.a["lk"]
    if i.op in ("bclr", "bcctr"):
        return (i.a["bo"] & 0x14) == 0x14 and not i.a["lk"]
    return False


@emits("bclr")
def _bclr(i: Insn, st: FnState) -> str:
    return "return;"
