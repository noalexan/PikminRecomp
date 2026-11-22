"""Gekko paired-single arithmetic and quantized loads/stores."""
from __future__ import annotations

from recomp.decode import Insn
from recomp.emit import FnState, emits

R = FnState.r


def _f(n: int, lane: int) -> str:
    return f"c->f[{n}].ps{lane}"


def _pair(i: Insn, e0: str, e1: str, rnd: bool = True) -> str:
    """Write both lanes of frD (computed before either write), rounding to single if rnd."""
    d = i.a["frd"]
    w = "ppc_round_single" if rnd else ""
    rc = " ppc_cr_set(c, 1, c->fpscr >> 28);" if i.a.get("rc") else ""
    return (f"{{ double r0 = {w}({e0}), r1 = {w}({e1}); "
            f"c->f[{d}].ps0 = r0; c->f[{d}].ps1 = r1;{rc} }}")


def _lanes(tmpl: str, i: Insn, csel: int | None = None) -> tuple[str, str]:
    a, b, cc = i.a.get("fra", 0), i.a["frb"], i.a.get("frc", 0)
    return tuple(
        tmpl.format(a=_f(a, k), b=_f(b, k), c=_f(cc, k if csel is None else csel)) for k in (0, 1)
    )


_ARITH = {
    "ps_add": "{a} + {b}", "ps_sub": "{a} - {b}", "ps_div": "{a} / {b}", "ps_mul": "{a} * {c}",
    "ps_madd": "fma({a}, {c}, {b})", "ps_msub": "fma({a}, {c}, -{b})",
    "ps_nmadd": "-fma({a}, {c}, {b})", "ps_nmsub": "-fma({a}, {c}, -{b})",
    "ps_res": "1.0 / {b}", "ps_rsqrte": "1.0 / sqrt({b})",
}


@emits(*_ARITH)
def _arith(i, st):
    return _pair(i, *_lanes(_ARITH[i.op], i))


@emits("ps_muls0", "ps_muls1")
def _muls(i, st):
    return _pair(i, *_lanes("{a} * {c}", i, csel=int(i.op[-1])))


@emits("ps_madds0", "ps_madds1")
def _madds(i, st):
    return _pair(i, *_lanes("fma({a}, {c}, {b})", i, csel=int(i.op[-1])))


@emits("ps_sum0")
def _sum0(i, st):
    a, b, cc = i.a["fra"], i.a["frb"], i.a["frc"]
    return _pair(i, f"{_f(a, 0)} + {_f(b, 1)}", _f(cc, 1))


@emits("ps_sum1")
def _sum1(i, st):
    a, b, cc = i.a["fra"], i.a["frb"], i.a["frc"]
    return _pair(i, _f(cc, 0), f"{_f(a, 0)} + {_f(b, 1)}")


@emits("ps_sel")
def _sel(i, st):
    return _pair(i, *_lanes("({a} >= 0.0) ? {c} : {b}", i), rnd=False)


_MOVES = {"ps_neg": "-{b}", "ps_abs": "fabs({b})", "ps_nabs": "-fabs({b})", "ps_mr": "{b}"}


@emits(*_MOVES)
def _moves(i, st):
    return _pair(i, *_lanes(_MOVES[i.op], i), rnd=False)


@emits("ps_merge00", "ps_merge01", "ps_merge10", "ps_merge11")
def _merge(i, st):
    la, lb = int(i.op[-2]), int(i.op[-1])
    return _pair(i, _f(i.a["fra"], la), _f(i.a["frb"], lb), rnd=False)


@emits("ps_cmpu0", "ps_cmpo0", "ps_cmpu1", "ps_cmpo1")
def _cmp(i, st):
    lane = int(i.op[-1])
    return f"ppc_cmp_f(c, {i.a['crfd']}, {_f(i.a['fra'], lane)}, {_f(i.a['frb'], lane)});"


# ---- quantized load/store -----------------------------------------------------------

def _psq_ea(i: Insn, st: FnState, update: bool, indexed: bool) -> str:
    ra = R(i.a["ra"]) if update else st.ra0(i.a["ra"])
    off = R(i.a["rb"]) if indexed else f"{i.a['ps_d'] & 0xFFFFFFFF}u"
    return f"uint32_t ea = {ra} + {off};"


def _psq_parse(op: str) -> tuple[bool, bool, bool]:
    """(is_load, update, indexed) for psq_l/lu/lx/lux/st/stu/stx/stux."""
    suffix = op[len("psq_l"):] if op.startswith("psq_l") else op[len("psq_st"):]
    return op.startswith("psq_l"), "u" in suffix, "x" in suffix


@emits("psq_l", "psq_lu", "psq_lx", "psq_lux")
def _psq_l(i, st):
    _, update, indexed = _psq_parse(i.op)
    d = i.a["frd"]
    upd = f" {R(i.a['ra'])} = ea;" if update else ""
    return (f"{{ {_psq_ea(i, st, update, indexed)} double p0, p1; "
            f"ppc_psq_load(c, ea, {i.a['w']}, {i.a['i']}, &p0, &p1);{upd} "
            f"c->f[{d}].ps0 = p0; c->f[{d}].ps1 = p1; }}")


@emits("psq_st", "psq_stu", "psq_stx", "psq_stux")
def _psq_st(i, st):
    _, update, indexed = _psq_parse(i.op)
    s = i.a["frs"]
    upd = f" {R(i.a['ra'])} = ea;" if update else ""
    return (f"{{ {_psq_ea(i, st, update, indexed)} "
            f"ppc_psq_store(c, ea, c->f[{s}].ps0, c->f[{s}].ps1, {i.a['w']}, {i.a['i']});{upd} }}")
