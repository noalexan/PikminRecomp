import struct

from tests.sem.harness import run_case

A = 0x80001000
GQR_U8 = 4 << 16  # load type u8, scale 0


def f32(x: float) -> float:
    return struct.unpack(">f", struct.pack(">f", x))[0]


def test_psq_l_float_pair():
    s = run_case("psq_l 1,0(3),0,0", {"r3": A}, {A: struct.pack(">ff", 1.5, 2.5)})
    assert s.f[1] == (1.5, 2.5)


def test_psq_l_single_element():
    s = run_case("psq_l 1,0(3),1,0", {"r3": A}, {A: struct.pack(">ff", 1.5, 2.5)})
    assert s.f[1] == (1.5, 1.0)


def test_psq_l_quantized_u8():
    s = run_case("psq_l 1,0(3),0,2", {"r3": A, "gqr2": GQR_U8}, {A: bytes([200, 10])})
    assert s.f[1] == (200.0, 10.0)


def test_psq_st_round_trip_and_update():
    s = run_case("psq_stu 1,8(3),0,0\npsq_lx 2,0,3,0,0", {"r3": A, "f1": (0.5, -4.0)})
    assert s.r[3] == A + 8
    assert s.mem(A + 8, 8) == struct.pack(">ff", 0.5, -4.0)
    assert s.f[2] == (0.5, -4.0)


def test_ps_add_lanes():
    s = run_case("ps_add 1,2,3", {"f2": (1.0, 2.0), "f3": (10.0, 20.0)})
    assert s.f[1] == (11.0, 22.0)


def test_ps_sum0():
    s = run_case("ps_sum0 1,2,3,4", {"f2": (1.0, 2.0), "f3": (5.0, 6.0), "f4": (7.0, 8.0)})
    assert s.f[1] == (1.0 + 8.0, 6.0)


def test_ps_sum1():
    s = run_case("ps_sum1 1,2,3,4", {"f2": (1.0, 2.0), "f3": (5.0, 6.0), "f4": (7.0, 8.0)})
    assert s.f[1] == (5.0, 1.0 + 8.0)


def test_ps_madds0():
    s = run_case("ps_madds0 1,2,3,4", {"f2": (2.0, 3.0), "f3": (10.0, 99.0), "f4": (1.0, 1.0)})
    assert s.f[1] == (21.0, 31.0)


def test_ps_muls1():
    s = run_case("ps_muls1 1,2,3", {"f2": (2.0, 3.0), "f3": (99.0, 10.0)})
    assert s.f[1] == (20.0, 30.0)


def test_ps_merge10():
    s = run_case("ps_merge10 1,2,3", {"f2": (1.0, 2.0), "f3": (3.0, 4.0)})
    assert s.f[1] == (2.0, 3.0)


def test_ps_cmpu1():
    s = run_case("ps_cmpu1 1,2,3", {"f2": (0.0, 5.0), "f3": (0.0, 1.0)})
    assert s.cr_field(1) == 4


def test_ps_mul_rounds_each_lane():
    s = run_case("ps_mul 1,2,3", {"f2": (0.1, 0.2), "f3": (3.0, 3.0)})
    assert s.f[1] == (f32(0.1 * 3.0), f32(0.2 * 3.0))


def test_ps_moves_and_signs():
    assert run_case("ps_neg 1,2", {"f2": (1.0, -2.0)}).f[1] == (-1.0, 2.0)
    assert run_case("ps_abs 1,2", {"f2": (-1.0, -2.0)}).f[1] == (1.0, 2.0)
    assert run_case("ps_mr 1,2", {"f2": (0.1, 0.2)}).f[1] == (0.1, 0.2)
    assert run_case("ps_sel 1,2,3,4", {"f2": (-1.0, 1.0), "f3": (5.0, 5.0), "f4": (6.0, 6.0)}).f[1] == (6.0, 5.0)
