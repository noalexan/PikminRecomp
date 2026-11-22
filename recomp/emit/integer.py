"""Integer arithmetic, logical, rotate/shift and compare instructions."""
from __future__ import annotations

from recomp.decode import Insn
from recomp.emit import FnState, emits

R = FnState.r


def _mask(mb: int, me: int) -> int:
    a = 0xFFFFFFFF >> mb
    b = 0xFFFFFFFF if me == 31 else (~(0xFFFFFFFF >> (me + 1))) & 0xFFFFFFFF
    return (a & b) if mb <= me else (a | b)


def _block(*lines: str) -> str:
    return "{ " + " ".join(lines) + " }"


def _tail(i: Insn, dst: str, ov: str | None = None) -> list[str]:
    out = [f"{dst} = r;"]
    if ov is not None and i.a.get("oe"):
        out.append(f"ppc_set_ov(c, {ov});")
    if i.a.get("rc"):
        out.append("ppc_rc(c, r);")
    return out


# ---- immediates ----------------------------------------------------------------

@emits("addi")
def _addi(i, st):
    return f"{R(i.a['rd'])} = {st.ra0(i.a['ra'])} + {i.a['simm'] & 0xFFFFFFFF}u;"


@emits("addis")
def _addis(i, st):
    return f"{R(i.a['rd'])} = {st.ra0(i.a['ra'])} + {(i.a['simm'] << 16) & 0xFFFFFFFF}u;"


@emits("addic")
def _addic(i, st):
    imm = i.a["simm"] & 0xFFFFFFFF
    return _block(f"uint32_t a = {R(i.a['ra'])}; uint32_t r = a + {imm}u;",
                  "ppc_set_ca(c, r < a);", *_tail(i, R(i.a["rd"])))


@emits("subfic")
def _subfic(i, st):
    imm = i.a["simm"] & 0xFFFFFFFF
    return _block(f"uint32_t a = {R(i.a['ra'])}; uint32_t r = {imm}u - a;",
                  f"ppc_set_ca(c, {imm}u >= a);", f"{R(i.a['rd'])} = r;")


@emits("mulli")
def _mulli(i, st):
    return f"{R(i.a['rd'])} = (uint32_t)((int32_t){R(i.a['ra'])} * (int32_t){i.a['simm']});"


@emits("ori", "oris", "xori", "xoris")
def _logic_imm(i, st):
    imm = i.a["uimm"] << 16 if i.op.endswith("s") else i.a["uimm"]
    op = "|" if i.op.startswith("o") else "^"
    return f"{R(i.a['ra'])} = {R(i.a['rs'])} {op} {imm}u;"


@emits("andi", "andis")
def _andi(i, st):
    imm = i.a["uimm"] << 16 if i.op == "andis" else i.a["uimm"]
    return _block(f"uint32_t r = {R(i.a['rs'])} & {imm}u;", f"{R(i.a['ra'])} = r;", "ppc_rc(c, r);")


# ---- XO-form arithmetic -----------------------------------------------------------

def _ov_add(x: str, y: str) -> str:
    return f"((({x}) ^ r) & (({y}) ^ r)) >> 31"


@emits("add", "addc", "adde", "addze", "addme", "subf", "subfc", "subfe", "subfze", "subfme", "neg")
def _xo_arith(i, st):
    a, b, d = R(i.a["ra"]), R(i.a["rb"]), R(i.a["rd"])
    op = i.op
    x = "~a" if op.startswith("subf") else "a"
    if op in ("add", "subf"):
        sum_ = f"(uint64_t)({x}) + b" + (" + 1" if op == "subf" else "")
        ca = None
    elif op in ("addc", "subfc"):
        sum_ = f"(uint64_t)({x}) + b" + (" + 1" if op == "subfc" else "")
        ca = True
    elif op in ("adde", "subfe"):
        sum_ = f"(uint64_t)({x}) + b + ppc_get_ca(c)"
        ca = True
    elif op in ("addze", "subfze"):
        sum_ = f"(uint64_t)({x}) + ppc_get_ca(c)"
        ca = True
    elif op in ("addme", "subfme"):
        sum_ = f"(uint64_t)({x}) + 0xFFFFFFFFu + ppc_get_ca(c)"
        ca = True
    else:  # neg
        return _block(f"uint32_t a = {a}; uint32_t r = 0u - a;", *_tail(i, d, "a == 0x80000000u"))
    yv = "b" if op not in ("addze", "subfze", "addme", "subfme") else ("0xFFFFFFFFu" if "me" in op else "0u")
    lines = [f"uint32_t a = {a}; uint32_t b = {b}; (void)b;", f"uint64_t s = {sum_}; uint32_t r = (uint32_t)s;"]
    if ca:
        lines.append("ppc_set_ca(c, (uint32_t)(s >> 32));")
    lines += _tail(i, d, _ov_add(x, yv))
    return _block(*lines)


@emits("mullw")
def _mullw(i, st):
    return _block(f"int64_t p = (int64_t)(int32_t){R(i.a['ra'])} * (int32_t){R(i.a['rb'])};",
                  "uint32_t r = (uint32_t)p;", *_tail(i, R(i.a["rd"]), "p != (int64_t)(int32_t)p"))


@emits("mulhw")
def _mulhw(i, st):
    return _block(f"uint32_t r = (uint32_t)(((int64_t)(int32_t){R(i.a['ra'])} * (int32_t){R(i.a['rb'])}) >> 32);",
                  *_tail(i, R(i.a["rd"])))


@emits("mulhwu")
def _mulhwu(i, st):
    return _block(f"uint32_t r = (uint32_t)(((uint64_t){R(i.a['ra'])} * {R(i.a['rb'])}) >> 32);",
                  *_tail(i, R(i.a["rd"])))


@emits("divw")
def _divw(i, st):
    # Division by zero / overflow: result as Dolphin produces (sign fill), OV set.
    return _block(f"int32_t a = (int32_t){R(i.a['ra'])}, b = (int32_t){R(i.a['rb'])};",
                  "int bad = b == 0 || (a == INT32_MIN && b == -1);",
                  "uint32_t r = bad ? (a < 0 ? 0xFFFFFFFFu : 0u) : (uint32_t)(a / b);",
                  *_tail(i, R(i.a["rd"]), "bad"))


@emits("divwu")
def _divwu(i, st):
    return _block(f"uint32_t a = {R(i.a['ra'])}, b = {R(i.a['rb'])};",
                  "uint32_t r = b ? a / b : 0u;", *_tail(i, R(i.a["rd"]), "b == 0"))


# ---- logical ----------------------------------------------------------------------

_LOGIC = {"and": "s & b", "or": "s | b", "xor": "s ^ b", "nand": "~(s & b)", "nor": "~(s | b)",
          "eqv": "~(s ^ b)", "andc": "s & ~b", "orc": "s | ~b"}


@emits(*_LOGIC)
def _logic(i, st):
    return _block(f"uint32_t s = {R(i.a['rs'])}, b = {R(i.a['rb'])};", f"uint32_t r = {_LOGIC[i.op]};",
                  *_tail(i, R(i.a["ra"])))


@emits("cntlzw")
def _cntlzw(i, st):
    return _block(f"uint32_t s = {R(i.a['rs'])};", "uint32_t r = s ? (uint32_t)__builtin_clz(s) : 32u;",
                  *_tail(i, R(i.a["ra"])))


@emits("extsb", "extsh")
def _exts(i, st):
    t = "int8_t" if i.op == "extsb" else "int16_t"
    return _block(f"uint32_t r = (uint32_t)(int32_t)({t}){R(i.a['rs'])};", *_tail(i, R(i.a["ra"])))


# ---- rotate / shift ---------------------------------------------------------------

@emits("rlwinm")
def _rlwinm(i, st):
    m = _mask(i.a["mb"], i.a["me"])
    return _block(f"uint32_t r = ppc_rotl({R(i.a['rs'])}, {i.a['sh']}) & 0x{m:08X}u;", *_tail(i, R(i.a["ra"])))


@emits("rlwnm")
def _rlwnm(i, st):
    m = _mask(i.a["mb"], i.a["me"])
    return _block(f"uint32_t r = ppc_rotl({R(i.a['rs'])}, {R(i.a['rb'])} & 31) & 0x{m:08X}u;",
                  *_tail(i, R(i.a["ra"])))


@emits("rlwimi")
def _rlwimi(i, st):
    m = _mask(i.a["mb"], i.a["me"])
    return _block(f"uint32_t r = (ppc_rotl({R(i.a['rs'])}, {i.a['sh']}) & 0x{m:08X}u) | ({R(i.a['ra'])} & 0x{~m & 0xFFFFFFFF:08X}u);",
                  *_tail(i, R(i.a["ra"])))


@emits("slw", "srw")
def _slw(i, st):
    op = "<<" if i.op == "slw" else ">>"
    return _block(f"uint32_t s = {R(i.a['rs'])}, n = {R(i.a['rb'])} & 0x3F;",
                  f"uint32_t r = (n & 0x20) ? 0u : (s {op} n);", *_tail(i, R(i.a["ra"])))


@emits("sraw")
def _sraw(i, st):
    return _block(f"int32_t s = (int32_t){R(i.a['rs'])}; uint32_t n = {R(i.a['rb'])} & 0x3F;",
                  "uint32_t r; if (n & 0x20) { r = (uint32_t)(s >> 31); ppc_set_ca(c, s < 0); }",
                  "else { r = (uint32_t)(s >> n); ppc_set_ca(c, s < 0 && n && ((uint32_t)s << (32 - n)) != 0); }",
                  *_tail(i, R(i.a["ra"])))


@emits("srawi")
def _srawi(i, st):
    n = i.a["sh"]
    lost = f"((uint32_t)s & 0x{(1 << n) - 1:X}u) != 0" if n else "0"
    return _block(f"int32_t s = (int32_t){R(i.a['rs'])};", f"uint32_t r = (uint32_t)(s >> {n});",
                  f"ppc_set_ca(c, s < 0 && {lost});", *_tail(i, R(i.a["ra"])))


# ---- compares ---------------------------------------------------------------------

@emits("cmp")
def _cmp(i, st):
    return f"ppc_cmp_s(c, {i.a['crfd']}, (int32_t){R(i.a['ra'])}, (int32_t){R(i.a['rb'])});"


@emits("cmpl")
def _cmpl(i, st):
    return f"ppc_cmp_u(c, {i.a['crfd']}, {R(i.a['ra'])}, {R(i.a['rb'])});"


@emits("cmpi")
def _cmpi(i, st):
    return f"ppc_cmp_s(c, {i.a['crfd']}, (int32_t){R(i.a['ra'])}, {i.a['simm']});"


@emits("cmpli")
def _cmpli(i, st):
    return f"ppc_cmp_u(c, {i.a['crfd']}, {R(i.a['ra'])}, {i.a['uimm']}u);"
