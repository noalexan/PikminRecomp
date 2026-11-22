import pytest

from recomp.decode import decode
from tests.conftest import OVERRIDDEN_LIBS


@pytest.mark.parametrize("word,op,args", [
    (0x7C0802A6, "mfspr", {"spr": 8, "rd": 0}),
    (0x90010004, "stw", {"rs": 0, "ra": 1, "d": 4}),
    (0x9421FFE8, "stwu", {"rs": 1, "ra": 1, "d": -0x18}),
    (0x4E800020, "bclr", {"bo": 20, "lk": 0}),
    (0x4E800420, "bcctr", {"bo": 20, "lk": 0}),
    (0x5483103A, "rlwinm", {"rs": 4, "ra": 3, "sh": 2, "mb": 0, "me": 29, "rc": 0}),
    (0x806D2D6C, "lwz", {"rd": 3, "ra": 13, "d": 0x2D6C}),
    (0xE0030000, "psq_l", {"frd": 0, "ra": 3, "w": 0, "i": 0, "ps_d": 0}),
    (0x48000001, "b", {"li": 0, "aa": 0, "lk": 1}),
])
def test_known_words(word, op, args):
    insn = decode(word, 0x80003000)
    assert insn.op == op
    for k, v in args.items():
        assert insn.a[k] == v, k


def test_unknown_word():
    assert decode(0x00000000, 0x80003000).op == "unknown"


def test_whole_dol_decodes(game_image):
    bad = []
    for f in game_image.functions:
        if f.library in OVERRIDDEN_LIBS or f.parent is not None:
            continue
        for addr in range(f.addr, f.addr + f.size, 4):
            word = game_image.read_u32(addr)
            if decode(word, addr).op == "unknown":
                bad.append(f"{addr:08x} {word:08x} {f.name}")
    assert not bad, f"{len(bad)} undecodable words, first: " + "; ".join(bad[:20])
