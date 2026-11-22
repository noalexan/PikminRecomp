"""Emitter registry and per-function translation state."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Protocol

from recomp.decode import Insn


class RecompError(Exception):
    pass


class TranslateContext(Protocol):
    def read_u32(self, addr: int) -> int: ...

    def call_target(self, addr: int) -> str | None:
        """C symbol for a function entry (translated or override) at addr."""

    def is_function_body(self, addr: int) -> str | None:
        """Name of a function whose body (not entry) contains addr."""

    def jump_table(self, addr: int) -> tuple[int, int] | None:
        """(table address, entry count) if a jump-table data symbol starts at addr."""


@dataclass
class FnState:
    ctx: TranslateContext
    name: str
    start: int
    end: int
    insns: list[Insn]
    labels: set[int] = field(default_factory=set)
    unimpl: list[tuple[int, int]] = field(default_factory=list)

    def contains(self, addr: int) -> bool:
        return self.start <= addr < self.end

    @staticmethod
    def r(n: int) -> str:
        return f"c->r[{n}]"

    @staticmethod
    def ra0(n: int) -> str:
        """rA operand where rA=0 means the literal 0."""
        return "0u" if n == 0 else f"c->r[{n}]"


Emitter = Callable[[Insn, FnState], str]
EMITTERS: dict[str, Emitter] = {}


def emits(*ops: str):
    def deco(fn: Emitter) -> Emitter:
        for op in ops:
            if op in EMITTERS:
                raise RuntimeError(f"duplicate emitter for {op}")
            EMITTERS[op] = fn
        return fn
    return deco


def load_all() -> None:
    """Import every emitter module so it registers its ops."""
    from recomp.emit import flow, integer  # noqa: F401
    for mod in ("loadstore", "fpu", "paired", "system"):
        try:
            __import__(f"recomp.emit.{mod}")
        except ModuleNotFoundError as e:
            if e.name != f"recomp.emit.{mod}":
                raise
