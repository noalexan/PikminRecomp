"""Gekko (PowerPC 750CXe + paired singles) instruction decoder.

Ops are base mnemonics: no '.'/'o' suffixes, no simplified forms.
Signed fields are sign-extended; branch displacements are in bytes.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Insn:
    addr: int
    word: int
    op: str
    a: dict[str, int]


def _sext(v: int, bits: int) -> int:
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def _f(w: int) -> dict[str, int]:
    """All raw fields; forms below pick the relevant ones."""
    return {
        "d5": (w >> 21) & 31, "a5": (w >> 16) & 31, "b5": (w >> 11) & 31,
        "c5": (w >> 6) & 31, "rc": w & 1,
    }


# ---- form builders: (word) -> args ------------------------------------------

def _arith_imm(w):  # rd, ra, simm
    f = _f(w)
    return {"rd": f["d5"], "ra": f["a5"], "simm": _sext(w & 0xFFFF, 16)}


def _logic_imm(w):  # rs, ra, uimm
    f = _f(w)
    return {"rs": f["d5"], "ra": f["a5"], "uimm": w & 0xFFFF}


def _load(w):
    f = _f(w)
    return {"rd": f["d5"], "ra": f["a5"], "d": _sext(w & 0xFFFF, 16)}


def _store(w):
    f = _f(w)
    return {"rs": f["d5"], "ra": f["a5"], "d": _sext(w & 0xFFFF, 16)}


def _fload(w):
    f = _f(w)
    return {"frd": f["d5"], "ra": f["a5"], "d": _sext(w & 0xFFFF, 16)}


def _fstore(w):
    f = _f(w)
    return {"frs": f["d5"], "ra": f["a5"], "d": _sext(w & 0xFFFF, 16)}


def _cmp_imm(w, signed):
    v = w & 0xFFFF
    return {"crfd": (w >> 23) & 7, "l": (w >> 21) & 1, "ra": (w >> 16) & 31,
            "simm" if signed else "uimm": _sext(v, 16) if signed else v}


def _rot(w, reg_sh):
    f = _f(w)
    a = {"rs": f["d5"], "ra": f["a5"], "mb": f["c5"], "me": (w >> 1) & 31, "rc": f["rc"]}
    if reg_sh:
        a["rb"] = f["b5"]
    else:
        a["sh"] = f["b5"]
    return a


def _xo(w):  # rd, ra, rb, oe, rc
    f = _f(w)
    return {"rd": f["d5"], "ra": f["a5"], "rb": f["b5"], "oe": (w >> 10) & 1, "rc": f["rc"]}


def _xlogic(w):  # rs, ra, rb, rc
    f = _f(w)
    return {"rs": f["d5"], "ra": f["a5"], "rb": f["b5"], "rc": f["rc"]}


def _xunary(w):  # rs, ra, rc  (cntlzw, extsb, extsh)
    f = _f(w)
    return {"rs": f["d5"], "ra": f["a5"], "rc": f["rc"]}


def _xload(w):
    f = _f(w)
    return {"rd": f["d5"], "ra": f["a5"], "rb": f["b5"]}


def _xstore(w):
    f = _f(w)
    return {"rs": f["d5"], "ra": f["a5"], "rb": f["b5"]}


def _xfload(w):
    f = _f(w)
    return {"frd": f["d5"], "ra": f["a5"], "rb": f["b5"]}


def _xfstore(w):
    f = _f(w)
    return {"frs": f["d5"], "ra": f["a5"], "rb": f["b5"]}


def _xcache(w):
    f = _f(w)
    return {"ra": f["a5"], "rb": f["b5"]}


def _xcmp(w):
    return {"crfd": (w >> 23) & 7, "l": (w >> 21) & 1, "ra": (w >> 16) & 31, "rb": (w >> 11) & 31}


def _spr(w):
    return ((w >> 16) & 31) | (((w >> 11) & 31) << 5)


def _none(w):
    return {}


def _crlogic(w):
    f = _f(w)
    return {"crbd": f["d5"], "crba": f["a5"], "crbb": f["b5"]}


def _fa(w):  # A-form FP: frd, fra, frb, frc, rc
    f = _f(w)
    return {"frd": f["d5"], "fra": f["a5"], "frb": f["b5"], "frc": f["c5"], "rc": f["rc"]}


def _fx(w):  # X-form FP unary/binary: frd, fra, frb, rc
    f = _f(w)
    return {"frd": f["d5"], "fra": f["a5"], "frb": f["b5"], "rc": f["rc"]}


def _fcmp(w):
    return {"crfd": (w >> 23) & 7, "fra": (w >> 16) & 31, "frb": (w >> 11) & 31}


def _psq(w):  # psq_l/st D-form (opcodes 56,57,60,61)
    return {"frd": (w >> 21) & 31, "frs": (w >> 21) & 31, "ra": (w >> 16) & 31,
            "w": (w >> 15) & 1, "i": (w >> 12) & 7, "ps_d": _sext(w & 0xFFF, 12)}


def _psqx(w):  # psq_lx/stx/lux/stux
    return {"frd": (w >> 21) & 31, "frs": (w >> 21) & 31, "ra": (w >> 16) & 31,
            "rb": (w >> 11) & 31, "w": (w >> 10) & 1, "i": (w >> 7) & 7}


# ---- tables -----------------------------------------------------------------

_PRIMARY = {
    3: ("twi", lambda w: {"to": (w >> 21) & 31, "ra": (w >> 16) & 31, "simm": _sext(w & 0xFFFF, 16)}),
    7: ("mulli", _arith_imm), 8: ("subfic", _arith_imm),
    10: ("cmpli", lambda w: _cmp_imm(w, False)), 11: ("cmpi", lambda w: _cmp_imm(w, True)),
    12: ("addic", lambda w: {**_arith_imm(w), "rc": 0}),
    13: ("addic", lambda w: {**_arith_imm(w), "rc": 1}),
    14: ("addi", _arith_imm), 15: ("addis", _arith_imm),
    16: ("bc", lambda w: {"bo": (w >> 21) & 31, "bi": (w >> 16) & 31,
                         "bd": _sext(w & 0xFFFC, 16), "aa": (w >> 1) & 1, "lk": w & 1}),
    17: ("sc", _none),
    18: ("b", lambda w: {"li": _sext(w & 0x03FFFFFC, 26), "aa": (w >> 1) & 1, "lk": w & 1}),
    20: ("rlwimi", lambda w: _rot(w, False)), 21: ("rlwinm", lambda w: _rot(w, False)),
    23: ("rlwnm", lambda w: _rot(w, True)),
    24: ("ori", _logic_imm), 25: ("oris", _logic_imm), 26: ("xori", _logic_imm), 27: ("xoris", _logic_imm),
    28: ("andi", lambda w: {**_logic_imm(w), "rc": 1}),
    29: ("andis", lambda w: {**_logic_imm(w), "rc": 1}),
    32: ("lwz", _load), 33: ("lwzu", _load), 34: ("lbz", _load), 35: ("lbzu", _load),
    36: ("stw", _store), 37: ("stwu", _store), 38: ("stb", _store), 39: ("stbu", _store),
    40: ("lhz", _load), 41: ("lhzu", _load), 42: ("lha", _load), 43: ("lhau", _load),
    44: ("sth", _store), 45: ("sthu", _store), 46: ("lmw", _load), 47: ("stmw", _store),
    48: ("lfs", _fload), 49: ("lfsu", _fload), 50: ("lfd", _fload), 51: ("lfdu", _fload),
    52: ("stfs", _fstore), 53: ("stfsu", _fstore), 54: ("stfd", _fstore), 55: ("stfdu", _fstore),
    56: ("psq_l", _psq), 57: ("psq_lu", _psq), 60: ("psq_st", _psq), 61: ("psq_stu", _psq),
}

_OP19 = {
    0: ("mcrf", lambda w: {"crfd": (w >> 23) & 7, "crfs": (w >> 18) & 7}),
    16: ("bclr", lambda w: {"bo": (w >> 21) & 31, "bi": (w >> 16) & 31, "lk": w & 1}),
    33: ("crnor", _crlogic), 50: ("rfi", _none), 129: ("crandc", _crlogic), 150: ("isync", _none),
    193: ("crxor", _crlogic), 225: ("crnand", _crlogic), 257: ("crand", _crlogic),
    289: ("creqv", _crlogic), 417: ("crorc", _crlogic), 449: ("cror", _crlogic),
    528: ("bcctr", lambda w: {"bo": (w >> 21) & 31, "bi": (w >> 16) & 31, "lk": w & 1}),
}

# XO-form arithmetic, keyed by 9-bit xo (OE folded into args).
_OP31_XO = {
    8: "subfc", 10: "addc", 11: "mulhwu", 40: "subf", 75: "mulhw", 104: "neg", 136: "subfe",
    138: "adde", 200: "subfze", 202: "addze", 232: "subfme", 234: "addme", 235: "mullw",
    266: "add", 459: "divwu", 491: "divw",
}

_OP31 = {
    0: ("cmp", _xcmp), 32: ("cmpl", _xcmp),
    4: ("tw", lambda w: {"to": (w >> 21) & 31, "ra": (w >> 16) & 31, "rb": (w >> 11) & 31}),
    19: ("mfcr", lambda w: {"rd": (w >> 21) & 31}),
    20: ("lwarx", _xload), 150: ("stwcx", _xstore),
    23: ("lwzx", _xload), 55: ("lwzux", _xload), 87: ("lbzx", _xload), 119: ("lbzux", _xload),
    279: ("lhzx", _xload), 311: ("lhzux", _xload), 343: ("lhax", _xload), 375: ("lhaux", _xload),
    534: ("lwbrx", _xload), 790: ("lhbrx", _xload), 533: ("lswx", _xload),
    151: ("stwx", _xstore), 183: ("stwux", _xstore), 215: ("stbx", _xstore), 247: ("stbux", _xstore),
    407: ("sthx", _xstore), 439: ("sthux", _xstore), 662: ("stwbrx", _xstore),
    918: ("sthbrx", _xstore), 661: ("stswx", _xstore),
    597: ("lswi", lambda w: {"rd": (w >> 21) & 31, "ra": (w >> 16) & 31, "nb": (w >> 11) & 31}),
    725: ("stswi", lambda w: {"rs": (w >> 21) & 31, "ra": (w >> 16) & 31, "nb": (w >> 11) & 31}),
    535: ("lfsx", _xfload), 567: ("lfsux", _xfload), 599: ("lfdx", _xfload), 631: ("lfdux", _xfload),
    663: ("stfsx", _xfstore), 695: ("stfsux", _xfstore), 727: ("stfdx", _xfstore),
    759: ("stfdux", _xfstore), 983: ("stfiwx", _xfstore),
    24: ("slw", _xlogic), 28: ("and", _xlogic), 60: ("andc", _xlogic), 124: ("nor", _xlogic),
    284: ("eqv", _xlogic), 316: ("xor", _xlogic), 412: ("orc", _xlogic), 444: ("or", _xlogic),
    476: ("nand", _xlogic), 536: ("srw", _xlogic), 792: ("sraw", _xlogic),
    824: ("srawi", lambda w: {"rs": (w >> 21) & 31, "ra": (w >> 16) & 31, "sh": (w >> 11) & 31, "rc": w & 1}),
    26: ("cntlzw", _xunary), 922: ("extsh", _xunary), 954: ("extsb", _xunary),
    83: ("mfmsr", lambda w: {"rd": (w >> 21) & 31}),
    146: ("mtmsr", lambda w: {"rs": (w >> 21) & 31}),
    144: ("mtcrf", lambda w: {"rs": (w >> 21) & 31, "crm": (w >> 12) & 0xFF}),
    339: ("mfspr", lambda w: {"rd": (w >> 21) & 31, "spr": _spr(w)}),
    467: ("mtspr", lambda w: {"rs": (w >> 21) & 31, "spr": _spr(w)}),
    371: ("mftb", lambda w: {"rd": (w >> 21) & 31, "tbr": _spr(w)}),
    595: ("mfsr", lambda w: {"rd": (w >> 21) & 31, "sr": (w >> 16) & 15}),
    210: ("mtsr", lambda w: {"rs": (w >> 21) & 31, "sr": (w >> 16) & 15}),
    659: ("mfsrin", _xload), 242: ("mtsrin", _xstore),
    512: ("mcrxr", lambda w: {"crfd": (w >> 23) & 7}),
    598: ("sync", _none), 854: ("eieio", _none), 566: ("tlbsync", _none),
    306: ("tlbie", _xcache), 370: ("tlbia", _none),
    54: ("dcbst", _xcache), 86: ("dcbf", _xcache), 246: ("dcbtst", _xcache), 278: ("dcbt", _xcache),
    470: ("dcbi", _xcache), 982: ("icbi", _xcache), 1014: ("dcbz", _xcache),
    310: ("eciwx", _xload), 438: ("ecowx", _xstore),
}

_OP59 = {18: "fdivs", 20: "fsubs", 21: "fadds", 24: "fres", 25: "fmuls",
         28: "fmsubs", 29: "fmadds", 30: "fnmsubs", 31: "fnmadds"}

_OP63_X = {
    0: ("fcmpu", _fcmp), 32: ("fcmpo", _fcmp),
    12: ("frsp", _fx), 14: ("fctiw", _fx), 15: ("fctiwz", _fx), 40: ("fneg", _fx),
    72: ("fmr", _fx), 136: ("fnabs", _fx), 264: ("fabs", _fx),
    38: ("mtfsb1", lambda w: {"crbd": (w >> 21) & 31, "rc": w & 1}),
    70: ("mtfsb0", lambda w: {"crbd": (w >> 21) & 31, "rc": w & 1}),
    64: ("mcrfs", lambda w: {"crfd": (w >> 23) & 7, "crfs": (w >> 18) & 7}),
    134: ("mtfsfi", lambda w: {"crfd": (w >> 23) & 7, "imm": (w >> 12) & 15, "rc": w & 1}),
    583: ("mffs", lambda w: {"frd": (w >> 21) & 31, "rc": w & 1}),
    711: ("mtfsf", lambda w: {"fm": (w >> 17) & 0xFF, "frb": (w >> 11) & 31, "rc": w & 1}),
}
_OP63_A = {18: "fdiv", 20: "fsub", 21: "fadd", 22: "fsqrt", 23: "fsel", 25: "fmul",
           26: "frsqrte", 28: "fmsub", 29: "fmadd", 30: "fnmsub", 31: "fnmadd"}

_OP4_X = {
    0: ("ps_cmpu0", _fcmp), 32: ("ps_cmpo0", _fcmp), 64: ("ps_cmpu1", _fcmp), 96: ("ps_cmpo1", _fcmp),
    40: ("ps_neg", _fx), 72: ("ps_mr", _fx), 136: ("ps_nabs", _fx), 264: ("ps_abs", _fx),
    528: ("ps_merge00", _fx), 560: ("ps_merge01", _fx), 592: ("ps_merge10", _fx), 624: ("ps_merge11", _fx),
    1014: ("dcbz_l", _xcache),
}
_OP4_IDX = {6: "psq_lx", 7: "psq_stx", 38: "psq_lux", 39: "psq_stux"}
_OP4_A = {10: "ps_sum0", 11: "ps_sum1", 12: "ps_muls0", 13: "ps_muls1", 14: "ps_madds0",
          15: "ps_madds1", 18: "ps_div", 20: "ps_sub", 21: "ps_add", 23: "ps_sel", 24: "ps_res",
          25: "ps_mul", 26: "ps_rsqrte", 28: "ps_msub", 29: "ps_madd", 30: "ps_nmsub", 31: "ps_nmadd"}


def _lookup(w: int) -> tuple[str, dict[str, int]] | None:
    op = w >> 26
    if op in _PRIMARY:
        name, form = _PRIMARY[op]
        return name, form(w)
    xo10 = (w >> 1) & 0x3FF
    xo5 = (w >> 1) & 31
    if op == 19 and xo10 in _OP19:
        name, form = _OP19[xo10]
        return name, form(w)
    if op == 31:
        if (xo10 & 0x1FF) in _OP31_XO:
            return _OP31_XO[xo10 & 0x1FF], _xo(w)
        if xo10 in _OP31:
            name, form = _OP31[xo10]
            return name, form(w)
    if op == 59 and xo5 in _OP59:
        return _OP59[xo5], _fa(w)
    if op == 63:
        if xo10 in _OP63_X:
            name, form = _OP63_X[xo10]
            return name, form(w)
        if xo5 in _OP63_A:
            return _OP63_A[xo5], _fa(w)
    if op == 4:
        if xo10 in _OP4_X:
            name, form = _OP4_X[xo10]
            return name, form(w)
        if ((w >> 1) & 0x3F) in _OP4_IDX:
            return _OP4_IDX[(w >> 1) & 0x3F], _psqx(w)
        if xo5 in _OP4_A:
            return _OP4_A[xo5], _fa(w)
    return None


def decode(word: int, addr: int) -> Insn:
    r = _lookup(word)
    if r is None:
        return Insn(addr, word, "unknown", {})
    return Insn(addr, word, r[0], r[1])
