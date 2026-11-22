"""Branches, calls, jump tables and CR logical instructions."""
from __future__ import annotations

from recomp.decode import Insn
from recomp.emit import FnState, RecompError, emits


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


# ---- condition evaluation ------------------------------------------------------------

def _cond(bo: int, bi: int, allow_ctr: bool = True) -> tuple[str, str]:
    """(pre-statement, C boolean expression) for BO/BI."""
    pre, terms = "", []
    if not bo & 0x04:
        if not allow_ctr:
            raise RecompError("CTR-decrementing bcctr is invalid")
        pre = "c->ctr--;"
        terms.append("c->ctr == 0" if bo & 0x02 else "c->ctr != 0")
    if not bo & 0x10:
        terms.append(f"ppc_cr_bit(c, {bi})" if bo & 0x08 else f"!ppc_cr_bit(c, {bi})")
    return pre, " && ".join(terms) if terms else "1"


def _guard(pre: str, cond: str, action: str) -> str:
    if cond == "1":
        return f"{pre} {{ {action} }}".strip()
    return f"{pre} if ({cond}) {{ {action} }}".strip()


def _foreign(i: Insn, st: FnState, target: int) -> str:
    """C symbol for a branch target outside this function, or raise."""
    sym = st.ctx.call_target(target)
    if sym is not None:
        return sym
    body = st.ctx.is_function_body(target)
    if body is not None:
        raise RecompError(f"branch from 0x{i.addr:08x} into body of {body} at 0x{target:08x}")
    raise RecompError(f"branch from 0x{i.addr:08x} to unknown address 0x{target:08x}")


def _transfer(i: Insn, st: FnState, target: int, link: bool) -> str:
    lr = f"c->lr = 0x{i.addr + 4:08x}u; " if link else ""
    if st.contains(target):
        return f"{lr}goto L_{target:08x};"
    sym = _foreign(i, st, target)
    return f"{lr}{sym}(c);" if link else f"{sym}(c); return;"


@emits("b")
def _b(i: Insn, st: FnState) -> str:
    return _transfer(i, st, branch_target(i), bool(i.a["lk"]))


@emits("bc")
def _bc(i: Insn, st: FnState) -> str:
    pre, cond = _cond(i.a["bo"], i.a["bi"])
    return _guard(pre, cond, _transfer(i, st, branch_target(i), bool(i.a["lk"])))


@emits("bclr")
def _bclr(i: Insn, st: FnState) -> str:
    pre, cond = _cond(i.a["bo"], i.a["bi"])
    if i.a["lk"]:
        action = f"uint32_t t = c->lr; c->lr = 0x{i.addr + 4:08x}u; ppc_lookup(t)(c);"
    else:
        action = "return;"
    return _guard(pre, cond, action)


@emits("bcctr")
def _bcctr(i: Insn, st: FnState) -> str:
    pre, cond = _cond(i.a["bo"], i.a["bi"], allow_ctr=False)
    if i.a["lk"]:
        return _guard(pre, cond, f"c->lr = 0x{i.addr + 4:08x}u; ppc_lookup(c->ctr)(c);")
    targets = st.jump_tables.get(i.addr)
    if targets is not None:
        cases = " ".join(f"case 0x{t:08x}u: goto L_{t:08x};" for t in sorted(set(targets)))
        action = f"switch (c->ctr) {{ {cases} default: ppc_lookup(c->ctr)(c); return; }}"
    else:
        st.unresolved_bcctr.append(i.addr)
        action = "ppc_lookup(c->ctr)(c); return;"
    return _guard(pre, cond, action)


# ---- jump tables ----------------------------------------------------------------------

def find_jump_table(st: FnState, idx: int) -> list[int] | None:
    """Resolve `lis/addi base; lwzx t,base,idx; mtctr t; bctr` to in-function targets."""
    insns = st.insns
    lo = max(0, idx - 8)
    ctr_reg = table_regs = None
    for k in range(idx - 1, lo - 1, -1):
        j = insns[k]
        if ctr_reg is None:
            if j.op == "mtspr" and j.a["spr"] == 9:
                ctr_reg = j.a["rs"]
            elif _writes_reg(j) is not None:
                return None
            continue
        if table_regs is None and j.op == "lwzx" and j.a["rd"] == ctr_reg:
            table_regs = (j.a["ra"], j.a["rb"])
            for base_reg in table_regs:
                addr = _materialized(insns, k, base_reg, lo)
                if addr is None:
                    addr = reg_constant(insns, k, base_reg)
                if addr is not None:
                    tab = st.ctx.jump_table(addr)
                    if tab is None:
                        continue
                    table, n = tab
                    targets = [st.ctx.read_u32(table + 4 * e) for e in range(n)]
                    if all(st.contains(t) for t in targets):
                        return targets
            return None
    return None


def _writes_reg(j: Insn) -> int | None:
    return j.a.get("rd") if "rd" in j.a else None


def _materialized(insns: list[Insn], before: int, reg: int, lo: int) -> int | None:
    """Constant held in `reg` just before insns[before], from lis/addis + addi/ori pairs."""
    low = None
    for k in range(before - 1, max(-1, lo - 8), -1):
        j = insns[k]
        if low is None:
            if j.op == "addi" and j.a["rd"] == reg and j.a["ra"] == reg:
                low = j.a["simm"]
                continue
            if j.op == "ori" and j.a["ra"] == reg and j.a["rs"] == reg:
                low = j.a["uimm"]
                continue
        if j.op == "addis" and j.a["rd"] == reg and j.a["ra"] == 0:
            return ((j.a["simm"] << 16) + (low or 0)) & 0xFFFFFFFF
        if j.a.get("rd") == reg or (j.op in ("ori", "oris", "rlwinm") and j.a.get("ra") == reg):
            return None
    return None


def writes_reg(j: Insn, reg: int) -> bool:
    """Whether instruction j may write GPR reg."""
    a = j.a
    if j.op == "lmw":
        return reg >= a["rd"]
    if a.get("rd") == reg:
        return True
    if "ra" in a and a["ra"] == reg:
        if j.op.startswith(("st", "psq_st")):
            return j.op.endswith(("u", "ux"))
        if j.op.startswith(("l", "psq_l")):
            return j.op.endswith(("u", "ux"))
        return "rs" in a and not j.op.startswith(("dcb", "icb", "mt"))
    return False


_CALLS = ("bclr", "bcctr", "b", "bc")


def reg_constant(insns: list[Insn], use: int, reg: int) -> int | None:
    """Value of `reg` at insns[use] when it is set exactly once, to a constant, before the use.

    Handles CodeWarrior hoisting a table address into a register near function entry
    (`lis rX,hi ; addi rY,rX,lo`). Later writes (e.g. the epilogue's lmw) are tolerated
    unless a backward branch could carry them back to the use. Volatile registers must
    not cross a call.
    """
    before = [k for k in range(use) if writes_reg(insns[k], reg)]
    if len(before) != 1:
        return None
    d = before[0]
    window = {insns[k].addr for k in range(d + 1, use + 1)}
    first_later = next((k for k in range(use + 1, len(insns)) if writes_reg(insns[k], reg)), None)
    if first_later is not None:
        for j in insns[first_later:]:
            t = branch_target(j)
            if t is not None and t in window:
                return None
    if reg < 14 and any(j.op in _CALLS and j.a.get("lk") for j in insns[d + 1:use]):
        return None
    j = insns[d]
    if j.op == "addis" and j.a["ra"] == 0:
        return (j.a["simm"] << 16) & 0xFFFFFFFF
    if j.op == "addi" and j.a["ra"] != 0:
        hi = _recent_lis(insns, d, j.a["ra"])
        return None if hi is None else (hi + j.a["simm"]) & 0xFFFFFFFF
    if j.op == "ori":
        hi = _recent_lis(insns, d, j.a["rs"])
        return None if hi is None else hi | j.a["uimm"]
    return None


def _recent_lis(insns: list[Insn], before: int, reg: int) -> int | None:
    """High half from the nearest preceding write to reg, if it is `lis reg,hi`."""
    for k in range(before - 1, -1, -1):
        j = insns[k]
        if writes_reg(j, reg):
            if j.op == "addis" and j.a["ra"] == 0:
                return (j.a["simm"] << 16) & 0xFFFFFFFF
            return None
    return None


# ---- CR logical -------------------------------------------------------------------------

_CRLOGIC = {"crand": "a & b", "cror": "a | b", "crxor": "a ^ b", "crnand": "!(a & b)",
            "crnor": "!(a | b)", "creqv": "!(a ^ b)", "crandc": "a & !b", "crorc": "a | !b"}


@emits(*_CRLOGIC)
def _crlogic(i: Insn, st: FnState) -> str:
    return (f"{{ uint32_t a = ppc_cr_bit(c, {i.a['crba']}), b = ppc_cr_bit(c, {i.a['crbb']}); "
            f"ppc_cr_setbit(c, {i.a['crbd']}, {_CRLOGIC[i.op]}); }}")


@emits("mcrf")
def _mcrf(i: Insn, st: FnState) -> str:
    return f"ppc_cr_set(c, {i.a['crfd']}, ppc_cr_get(c, {i.a['crfs']}));"


@emits("mcrxr")
def _mcrxr(i: Insn, st: FnState) -> str:
    return f"ppc_cr_set(c, {i.a['crfd']}, c->xer >> 28); c->xer &= 0x0FFFFFFFu;"
