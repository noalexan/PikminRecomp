"""Special-purpose registers, CR moves, cache/sync and supervisor instructions."""
from __future__ import annotations

from recomp.emit import FnState, emits

R = FnState.r

_SPR_FIELDS = {1: "c->xer", 8: "c->lr", 9: "c->ctr", 920: "c->hid2"}
_SPR_FIELDS.update({912 + n: f"c->gqr[{n}]" for n in range(8)})


@emits("mfspr")
def _mfspr(i, st):
    field = _SPR_FIELDS.get(i.a["spr"])
    if field is None:
        return f"{R(i.a['rd'])} = 0u; /* SPR {i.a['spr']} not modelled */"
    return f"{R(i.a['rd'])} = {field};"


@emits("mtspr")
def _mtspr(i, st):
    field = _SPR_FIELDS.get(i.a["spr"])
    if field is None:
        return f"/* write to SPR {i.a['spr']} ignored */"
    return f"{field} = {R(i.a['rs'])};"


@emits("mftb")
def _mftb(i, st):
    shift = " >> 32" if i.a["tbr"] == 269 else ""
    return f"{R(i.a['rd'])} = (uint32_t)(ppc_timebase(){shift});"


@emits("mfcr")
def _mfcr(i, st):
    return f"{R(i.a['rd'])} = c->cr;"


@emits("mtcrf")
def _mtcrf(i, st):
    crm = i.a["crm"]
    mask = 0
    for field in range(8):
        if crm & (0x80 >> field):
            mask |= 0xF << (28 - 4 * field)
    return f"c->cr = (c->cr & 0x{~mask & 0xFFFFFFFF:08X}u) | ({R(i.a['rs'])} & 0x{mask:08X}u);"


@emits("mfmsr")
def _mfmsr(i, st):
    return f"{R(i.a['rd'])} = 0x00008000u; /* EE set */"


@emits("mfsr", "mfsrin")
def _mfsr(i, st):
    return f"{R(i.a['rd'])} = 0u; /* segment registers not modelled */"


@emits("mtmsr", "mtsr", "mtsrin", "sync", "isync", "eieio", "dcbf", "dcbi", "dcbst", "dcbt", "dcbtst",
       "icbi", "tlbie", "tlbsync", "tlbia")
def _nop(i, st):
    return f"/* {i.op}: no effect on host */"


@emits("dcbz", "dcbz_l")
def _dcbz(i, st):
    return (f"{{ uint32_t ea = ({st.ra0(i.a['ra'])} + {R(i.a['rb'])}) & ~31u; "
            "for (int k = 0; k < 32; k += 4) mem_w32(c, ea + k, 0u); }")
