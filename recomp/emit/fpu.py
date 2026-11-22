"""Scalar floating-point instructions (FPRs hold doubles in ps0)."""
from __future__ import annotations

from recomp.decode import Insn
from recomp.emit import FnState, emits


def F(n: int) -> str:
    return f"c->f[{n}].ps0"


def _rc(i: Insn) -> str:
    return " ppc_cr_set(c, 1, c->fpscr >> 28);" if i.a.get("rc") else ""


def _write(i: Insn, expr: str, single: bool) -> str:
    d = i.a["frd"]
    if single:
        return f"{{ double r = ppc_round_single({expr}); c->f[{d}].ps0 = r; c->f[{d}].ps1 = r;{_rc(i)} }}"
    return f"{{ double r = {expr}; c->f[{d}].ps0 = r;{_rc(i)} }}"


_BINARY = {"fadd": "+", "fsub": "-", "fdiv": "/"}


@emits("fadd", "fsub", "fdiv", "fadds", "fsubs", "fdivs")
def _binary(i, st):
    single = i.op.endswith("s")
    op = _BINARY[i.op.rstrip("s") if single else i.op]
    return _write(i, f"{F(i.a['fra'])} {op} {F(i.a['frb'])}", single)


@emits("fmul", "fmuls")
def _fmul(i, st):
    return _write(i, f"{F(i.a['fra'])} * {F(i.a['frc'])}", i.op == "fmuls")


_FUSED = {
    "fmadd": "fma({a}, {c}, {b})",
    "fmsub": "fma({a}, {c}, -{b})",
    "fnmadd": "-fma({a}, {c}, {b})",
    "fnmsub": "-fma({a}, {c}, -{b})",
}


@emits("fmadd", "fmsub", "fnmadd", "fnmsub", "fmadds", "fmsubs", "fnmadds", "fnmsubs")
def _fused(i, st):
    single = i.op.endswith("s")
    tmpl = _FUSED[i.op[:-1] if single else i.op]
    return _write(i, tmpl.format(a=F(i.a["fra"]), b=F(i.a["frb"]), c=F(i.a["frc"])), single)


@emits("frsp")
def _frsp(i, st):
    return _write(i, F(i.a["frb"]), True)


_UNARY = {"fneg": "-{b}", "fabs": "fabs({b})", "fnabs": "-fabs({b})", "fmr": "{b}"}


@emits(*_UNARY)
def _unary(i, st):
    return _write(i, _UNARY[i.op].format(b=F(i.a["frb"])), False)


@emits("fsel")
def _fsel(i, st):
    return _write(i, f"({F(i.a['fra'])} >= 0.0) ? {F(i.a['frc'])} : {F(i.a['frb'])}", False)


@emits("fres")
def _fres(i, st):
    return _write(i, f"1.0 / {F(i.a['frb'])}", True)


@emits("frsqrte")
def _frsqrte(i, st):
    return _write(i, f"1.0 / sqrt({F(i.a['frb'])})", False)


@emits("fctiw", "fctiwz")
def _fctiw(i, st):
    rtz = 1 if i.op == "fctiwz" else 0
    return (f"c->f[{i.a['frd']}].ps0 = ppc_bitsd(0xFFF8000000000000ull | "
            f"ppc_fctiw({F(i.a['frb'])}, {rtz}));{_rc(i)}")


@emits("fcmpu", "fcmpo")
def _fcmp(i, st):
    return f"ppc_cmp_f(c, {i.a['crfd']}, {F(i.a['fra'])}, {F(i.a['frb'])});"


# ---- FPSCR ----------------------------------------------------------------------

@emits("mffs")
def _mffs(i, st):
    return f"c->f[{i.a['frd']}].ps0 = ppc_bitsd(0xFFF8000000000000ull | c->fpscr);{_rc(i)}"


@emits("mtfsf")
def _mtfsf(i, st):
    mask = 0
    for field in range(8):
        if i.a["fm"] & (0x80 >> field):
            mask |= 0xF << (28 - 4 * field)
    return (f"c->fpscr = (c->fpscr & 0x{~mask & 0xFFFFFFFF:08X}u) | "
            f"((uint32_t)ppc_dbits({F(i.a['frb'])}) & 0x{mask:08X}u); ppc_fpscr_changed(c);{_rc(i)}")


@emits("mtfsfi")
def _mtfsfi(i, st):
    shift = 28 - 4 * i.a["crfd"]
    return (f"c->fpscr = (c->fpscr & ~(0xFu << {shift})) | ({i.a['imm']}u << {shift}); "
            f"ppc_fpscr_changed(c);{_rc(i)}")


@emits("mtfsb0", "mtfsb1")
def _mtfsb(i, st):
    bit = f"(1u << {31 - i.a['crbd']})"
    op = f"c->fpscr &= ~{bit};" if i.op == "mtfsb0" else f"c->fpscr |= {bit};"
    return f"{op} ppc_fpscr_changed(c);{_rc(i)}"


@emits("mcrfs")
def _mcrfs(i, st):
    return f"ppc_cr_set(c, {i.a['crfd']}, c->fpscr >> {28 - 4 * i.a['crfs']});"
