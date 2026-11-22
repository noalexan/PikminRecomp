from tests.sem.harness import run_case

A = 0x80001000


def test_spr_round_trips():
    s = run_case("mtctr 3\nmfctr 4", {"r3": 0x1234})
    assert s.ctr == 0x1234 and s.r[4] == 0x1234
    s = run_case("mtlr 3\nmflr 4\nmtlr 5", {"r3": 0x80001234, "r5": 0x80005678})
    assert s.r[4] == 0x80001234
    s = run_case("mtxer 3\nmfxer 4", {"r3": 0x20000000})
    assert s.xer == 0x20000000 and s.r[4] == 0x20000000
    s = run_case("mtspr 912,3\nmfspr 4,912", {"r3": 0x00070007})
    assert s.gqr[0] == 0x00070007 and s.r[4] == 0x00070007


def test_unknown_spr_reads_zero():
    assert run_case("mfspr 3,1008", {"r3": 99}).r[3] == 0  # HID0


def test_mtcrf_single_field():
    s = run_case("mtcrf 0x80,3", {"r3": 0xFFFFFFFF, "cr": 0x01234567})
    assert s.cr == 0xF1234567


def test_mfcr_reflects_compare():
    s = run_case("cmpwi 1,3,0\nmfcr 4", {"r3": 5})
    assert s.r[4] == 0x04000000


def test_dcbz_zeroes_block():
    s = run_case("dcbz 0,3", {"r3": A + 0x10}, {A - 4: b"\xAA" * 40})
    assert s.mem(A, 32) == bytes(32)
    assert s.mem(A - 4, 4) == b"\xAA" * 4 and s.mem(A + 32, 4) == b"\xAA" * 4


def _mftb(rd: int, tbr: int) -> str:
    # gas -mgekko lacks the mftb mnemonic; encode X-form opcode 31/371 directly.
    word = (31 << 26) | (rd << 21) | ((tbr & 31) << 16) | ((tbr >> 5) << 11) | (371 << 1)
    return f".long 0x{word:08x}"


def test_mftb_splits_timebase():
    s = run_case(_mftb(3, 268) + "\n" + _mftb(4, 269))
    assert s.r[3] == 2 and s.r[4] == 1


def test_cache_and_sync_ops_are_harmless():
    s = run_case("sync\nisync\neieio\ndcbf 0,3\ndcbst 0,3\ndcbt 0,3\nicbi 0,3\nmtmsr 3\nmfmsr 4", {"r3": A})
    assert s.r[4] == 0x00008000
