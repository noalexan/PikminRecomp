"""Integer and floating-point loads/stores."""
from __future__ import annotations

from recomp.decode import Insn
from recomp.emit import FnState, emits

R = FnState.r

# op -> (C load expression template over `ea`, result conversion)
_LOADS = {
    "lwz": "mem_r32(c, ea)",
    "lhz": "(uint32_t)mem_r16(c, ea)",
    "lha": "(uint32_t)(int32_t)(int16_t)mem_r16(c, ea)",
    "lbz": "(uint32_t)mem_r8(c, ea)",
    "lwbrx": "__builtin_bswap32(mem_r32(c, ea))",
    "lhbrx": "(uint32_t)__builtin_bswap16(mem_r16(c, ea))",
}
_STORES = {
    "stw": "mem_w32(c, ea, {v});",
    "sth": "mem_w16(c, ea, (uint16_t){v});",
    "stb": "mem_w8(c, ea, (uint8_t){v});",
    "stwbrx": "mem_w32(c, ea, __builtin_bswap32({v}));",
    "sthbrx": "mem_w16(c, ea, __builtin_bswap16((uint16_t){v}));",
}


def _base(op: str) -> tuple[str, bool, bool]:
    """Split e.g. 'lwzux' -> ('lwz', update=True, indexed=True)."""
    if op in _LOADS or op in _STORES:
        return op, False, op.endswith("brx")
    for suffix, upd, idx in (("ux", True, True), ("x", False, True), ("u", True, False)):
        if op.endswith(suffix) and (op[: -len(suffix)] in _LOADS or op[: -len(suffix)] in _STORES
                                    or op[: -len(suffix)] in _FLOADS or op[: -len(suffix)] in _FSTORES):
            return op[: -len(suffix)], upd, idx
    return op, False, op.endswith("x")


def _ea(i: Insn, st: FnState, update: bool, indexed: bool) -> str:
    ra = R(i.a["ra"]) if update else st.ra0(i.a["ra"])
    off = R(i.a["rb"]) if indexed else f"{i.a['d'] & 0xFFFFFFFF}u"
    return f"uint32_t ea = {ra} + {off};"


def _upd(i: Insn, update: bool) -> str:
    return f" {R(i.a['ra'])} = ea;" if update else ""


def _int_load(i, st):
    base, update, indexed = _base(i.op)
    return f"{{ {_ea(i, st, update, indexed)} uint32_t v = {_LOADS[base]};{_upd(i, update)} {R(i.a['rd'])} = v; }}"


def _int_store(i, st):
    base, update, indexed = _base(i.op)
    store = _STORES[base].format(v=R(i.a["rs"]))
    return f"{{ {_ea(i, st, update, indexed)} {store}{_upd(i, update)} }}"


_FLOADS = {"lfs", "lfd"}
_FSTORES = {"stfs", "stfd"}


def _f_load(i, st):
    base, update, indexed = _base(i.op)
    d = i.a["frd"]
    if base == "lfs":
        val = f"double v = (double)mem_rf32(c, ea);{_upd(i, update)} c->f[{d}].ps0 = v; c->f[{d}].ps1 = v;"
    else:
        val = f"double v = mem_rf64(c, ea);{_upd(i, update)} c->f[{d}].ps0 = v;"
    return f"{{ {_ea(i, st, update, indexed)} {val} }}"


def _f_store(i, st):
    base, update, indexed = _base(i.op)
    s = i.a["frs"]
    store = f"mem_wf32(c, ea, (float)c->f[{s}].ps0);" if base == "stfs" else f"mem_wf64(c, ea, c->f[{s}].ps0);"
    return f"{{ {_ea(i, st, update, indexed)} {store}{_upd(i, update)} }}"


def _variants(bases, forms):
    return [b + f for b in bases for f in forms]


emits(*_variants(["lwz", "lhz", "lha", "lbz"], ["", "u", "x", "ux"]), "lwbrx", "lhbrx")(_int_load)
emits(*_variants(["stw", "sth", "stb"], ["", "u", "x", "ux"]), "stwbrx", "sthbrx")(_int_store)
emits(*_variants(["lfs", "lfd"], ["", "u", "x", "ux"]))(_f_load)
emits(*_variants(["stfs", "stfd"], ["", "u", "x", "ux"]))(_f_store)


@emits("stfiwx")
def _stfiwx(i, st):
    return (f"{{ {_ea(i, st, False, True)} "
            f"mem_w32(c, ea, (uint32_t)ppc_dbits(c->f[{i.a['frs']}].ps0)); }}")


@emits("lmw")
def _lmw(i, st):
    d = i.a["rd"]
    return (f"{{ {_ea(i, st, False, False)} "
            f"for (int n = {d}; n < 32; n++, ea += 4) c->r[n] = mem_r32(c, ea); }}")


@emits("stmw")
def _stmw(i, st):
    s = i.a["rs"]
    return (f"{{ {_ea(i, st, False, False)} "
            f"for (int n = {s}; n < 32; n++, ea += 4) mem_w32(c, ea, c->r[n]); }}")


@emits("lswi")
def _lswi(i, st):
    nb = i.a["nb"] or 32
    return (f"{{ uint32_t ea = {st.ra0(i.a['ra'])}; int reg = {i.a['rd']} - 1; "
            f"for (int k = 0; k < {nb}; k++) {{ if ((k & 3) == 0) {{ reg = (reg + 1) & 31; c->r[reg] = 0; }} "
            f"c->r[reg] |= (uint32_t)mem_r8(c, ea + k) << (24 - 8 * (k & 3)); }} }}")


@emits("stswi")
def _stswi(i, st):
    nb = i.a["nb"] or 32
    return (f"{{ uint32_t ea = {st.ra0(i.a['ra'])}; int reg = {i.a['rs']} - 1; "
            f"for (int k = 0; k < {nb}; k++) {{ if ((k & 3) == 0) reg = (reg + 1) & 31; "
            f"mem_w8(c, ea + k, (uint8_t)(c->r[reg] >> (24 - 8 * (k & 3)))); }} }}")
