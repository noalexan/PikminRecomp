import struct

from tests.sem.harness import run_case

A = 0x80001000  # scratch data address
BYTES = bytes.fromhex("8001020304050607")


def test_word_half_byte_loads():
    m = {A: BYTES}
    assert run_case("lwz 3,0(4)", {"r4": A}, m).r[3] == 0x80010203
    assert run_case("lhz 3,0(4)", {"r4": A}, m).r[3] == 0x8001
    assert run_case("lha 3,0(4)", {"r4": A}, m).r[3] == 0xFFFF8001
    assert run_case("lbz 3,1(4)", {"r4": A}, m).r[3] == 0x01


def test_indexed_loads():
    m = {A: BYTES}
    r = {"r4": A, "r5": 4}
    assert run_case("lwzx 3,4,5", r, m).r[3] == 0x04050607
    assert run_case("lhzx 3,4,5", r, m).r[3] == 0x0405
    assert run_case("lhax 3,4,5", {"r4": A, "r5": 0}, m).r[3] == 0xFFFF8001
    assert run_case("lbzx 3,4,5", r, m).r[3] == 0x04


def test_update_forms_write_ea():
    s = run_case("lwzu 3,4(4)", {"r4": A}, {A: BYTES})
    assert s.r[3] == 0x04050607 and s.r[4] == A + 4
    s = run_case("stwu 3,-8(4)", {"r3": 0xDEADBEEF, "r4": A + 8})
    assert s.r[4] == A and s.mem(A, 4) == bytes.fromhex("DEADBEEF")
    s = run_case("lbzux 3,4,5", {"r4": A, "r5": 2}, {A: BYTES})
    assert s.r[3] == 0x02 and s.r[4] == A + 2


def test_stores():
    s = run_case("stw 3,0(4)\nsth 3,4(4)\nstb 3,6(4)", {"r3": 0x11223344, "r4": A})
    assert s.mem(A, 7) == bytes.fromhex("11223344334444")
    s = run_case("stwx 3,4,5\nsthx 3,4,6\nstbx 3,4,7", {"r3": 0xAABBCCDD, "r4": A, "r5": 0, "r6": 4, "r7": 6})
    assert s.mem(A, 7) == bytes.fromhex("AABBCCDDCCDDDD")
    s = run_case("sthu 3,2(4)", {"r3": 0x1234, "r4": A})
    assert s.r[4] == A + 2 and s.mem(A + 2, 2) == bytes.fromhex("1234")


def test_multiple_round_trip():
    s = run_case("lmw 29,0(3)\nstmw 29,16(3)", {"r3": A}, {A: bytes(range(12))})
    assert s.r[29] == 0x00010203 and s.r[31] == 0x08090A0B
    assert s.mem(A + 16, 12) == bytes(range(12))


def test_byte_reversed():
    s = run_case("lwbrx 3,0,4\nlhbrx 5,0,4", {"r4": A}, {A: BYTES})
    assert s.r[3] == 0x03020180 and s.r[5] == 0x0180
    s = run_case("stwbrx 3,0,4\nsthbrx 3,4,5", {"r3": 0x11223344, "r4": A, "r5": 4})
    assert s.mem(A, 6) == bytes.fromhex("443322114433")


def test_float_loads_and_stores():
    m = {A: struct.pack(">f", 1.5) + struct.pack(">d", -2.25)}
    s = run_case("lfs 1,0(3)\nlfd 2,4(3)", {"r3": A}, m)
    assert s.f[1][0] == 1.5 and s.f[1][1] == 1.5 and s.f[2][0] == -2.25
    s = run_case("stfs 1,0(3)\nstfd 1,8(3)", {"r3": A, "f1": 0.1})
    assert s.mem(A, 4) == struct.pack(">f", 0.1)
    assert s.mem(A + 8, 8) == struct.pack(">d", 0.1)
    s = run_case("lfsx 1,3,4\nlfdux 2,3,5", {"r3": A, "r4": 0, "r5": 4}, m)
    assert s.f[1][0] == 1.5 and s.f[2][0] == -2.25 and s.r[3] == A + 4


def test_stfiwx_stores_low_word():
    raw = struct.unpack(">d", bytes.fromhex("1122334455667788"))[0]
    s = run_case("stfiwx 1,0,3", {"r3": A, "f1": raw})
    assert s.mem(A, 4) == bytes.fromhex("55667788")


def test_string_word_immediate():
    s = run_case("lswi 5,3,7\nstswi 5,4,7", {"r3": A, "r4": A + 16}, {A: bytes.fromhex("01020304050607ff")})
    assert s.r[5] == 0x01020304 and s.r[6] == 0x05060700
    assert s.mem(A + 16, 8) == bytes.fromhex("0102030405060700")


def test_ra_zero_is_literal_zero():
    s = run_case("lwz 3,0x1000(0)", {"r0": 0x80002000}, {0x80001000: bytes.fromhex("CAFEF00D")})
    assert s.r[3] == 0xCAFEF00D


def test_store_to_wgpipe_reaches_mmio():
    s = run_case("stw 3,0(4)", {"r3": 0x12345678, "r4": 0xCC008000})
    assert s.mmio == [(0x0C008000, 0x12345678, 4)]
