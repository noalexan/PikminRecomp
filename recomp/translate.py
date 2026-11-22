"""Translate one guest function to a C function over ppc_ctx."""
from __future__ import annotations

from recomp.decode import decode
from recomp.emit import EMITTERS, FnState, RecompError, TranslateContext, load_all
from recomp.emit.flow import branch_target, is_terminator

__all__ = ["RecompError", "TranslateContext", "translate_function", "translate_words"]

load_all()


def translate_function(ctx: TranslateContext, name: str, start: int, end: int, comment: str = "") -> str:
    return translate_function_state(ctx, name, start, end, comment)[0]


def translate_function_state(ctx: TranslateContext, name: str, start: int, end: int,
                             comment: str = "") -> tuple[str, FnState]:
    insns = [decode(ctx.read_u32(a), a) for a in range(start, end, 4)]
    st = FnState(ctx, name, start, end, insns)
    for i in insns:
        t = branch_target(i)
        if t is not None and st.contains(t):
            st.labels.add(t)
    body = []
    for i in insns:
        if i.addr in st.labels:
            body.append(f"L_{i.addr:08x}:;")
        emitter = EMITTERS.get(i.op)
        if emitter is None:
            st.unimpl.append((i.addr, i.word))
            code = f"ppc_unimpl(c, 0x{i.addr:08x}u, 0x{i.word:08x}u);"
        else:
            code = emitter(i, st)
        body.append(f"    /* {i.addr:08x} {i.word:08x} {i.op} */ {code}")
    if not insns or not is_terminator(insns[-1]):
        target = ctx.call_target(end)
        if target is None:
            raise RecompError(f"{name}: falls through past 0x{end:08x} and no function starts there")
        body.append(f"    {target}(c); return;")
    header = f"void {name}(ppc_ctx *c) {{" + (f" /* {comment} */" if comment else "")
    return "\n".join([header, *body, "}"]) + "\n", st


class _WordsContext:
    def __init__(self, words, addr, data, tables, entries, bodies):
        self.words, self.addr = words, addr
        self.data, self.tables, self.entries, self.bodies = data, tables, entries, bodies

    def read_u32(self, addr: int) -> int:
        off = addr - self.addr
        if 0 <= off < 4 * len(self.words) and off % 4 == 0:
            return self.words[off // 4]
        for base, blob in self.data.items():
            if base <= addr and addr + 4 <= base + len(blob):
                return int.from_bytes(blob[addr - base:addr - base + 4], "big")
        raise RecompError(f"read outside test image at 0x{addr:08x}")

    def call_target(self, addr: int) -> str | None:
        return self.entries.get(addr)

    def is_function_body(self, addr: int) -> str | None:
        for name, (lo, hi) in self.bodies.items():
            if lo < addr < hi:
                return name
        return None

    def jump_table(self, addr: int) -> tuple[int, int] | None:
        return (addr, self.tables[addr]) if addr in self.tables else None


def translate_words(words: list[int], addr: int, name: str, *, data: dict[int, bytes] | None = None,
                    tables: dict[int, int] | None = None, entries: dict[int, str] | None = None,
                    bodies: dict[str, tuple[int, int]] | None = None) -> str:
    ctx = _WordsContext(words, addr, data or {}, tables or {}, entries or {}, bodies or {})
    return translate_function(ctx, name, addr, addr + 4 * len(words))
