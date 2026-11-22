import math
import struct

from tests.sem.harness import run_case


def f32(x: float) -> float:
    return struct.unpack(">f", struct.pack(">f", x))[0]


def low_word(x: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", x))[0] & 0xFFFFFFFF


def test_double_arithmetic():
    r = {"f2": 6.0, "f3": 1.5, "f4": 2.0}
    assert run_case("fadd 1,2,3", r).f[1][0] == 7.5
    assert run_case("fsub 1,2,3", r).f[1][0] == 4.5
    assert run_case("fmul 1,2,4", r).f[1][0] == 12.0  # frA * frC
    assert run_case("fdiv 1,2,4", r).f[1][0] == 3.0


def test_fdivs_by_zero_is_inf():
    assert run_case("fdivs 1,2,3", {"f2": 1.0, "f3": 0.0}).f[1][0] == math.inf


def test_single_rounding():
    s = run_case("fmuls 1,2,3", {"f2": 0.1, "f3": 3.0})
    assert s.f[1][0] == f32(0.1 * 3.0)
    assert run_case("frsp 1,2", {"f2": 0.1}).f[1][0] == f32(0.1)


def test_fmadds_is_fused():
    a, cc, b = 1 + 2**-30, 1 - 2**-30, -1.0
    s = run_case("fmadds 1,2,3,4", {"f2": a, "f3": cc, "f4": b})
    assert s.f[1][0] == -(2**-60)


def test_fused_family():
    r = {"f2": 2.0, "f3": 3.0, "f4": 1.0}
    assert run_case("fmadd 1,2,3,4", r).f[1][0] == 7.0
    assert run_case("fmsub 1,2,3,4", r).f[1][0] == 5.0
    assert run_case("fnmadd 1,2,3,4", r).f[1][0] == -7.0
    assert run_case("fnmsub 1,2,3,4", r).f[1][0] == -5.0


def test_sign_ops_and_move():
    assert run_case("fneg 1,2", {"f2": 2.5}).f[1][0] == -2.5
    assert run_case("fabs 1,2", {"f2": -2.5}).f[1][0] == 2.5
    assert run_case("fnabs 1,2", {"f2": 2.5}).f[1][0] == -2.5
    assert run_case("fmr 1,2", {"f2": 9.0}).f[1][0] == 9.0


def test_fsel():
    r = {"f3": 1.0, "f4": 2.0}
    assert run_case("fsel 1,2,3,4", {**r, "f2": -1.0}).f[1][0] == 2.0
    assert run_case("fsel 1,2,3,4", {**r, "f2": 0.0}).f[1][0] == 1.0
    assert run_case("fsel 1,2,3,4", {**r, "f2": math.nan}).f[1][0] == 2.0


def test_fctiwz_saturates():
    assert low_word(run_case("fctiwz 1,2", {"f2": 1e10}).f[1][0]) == 0x7FFFFFFF
    assert low_word(run_case("fctiwz 1,2", {"f2": -3.9}).f[1][0]) == 0xFFFFFFFD


def test_fcmpu_nan_is_unordered():
    assert run_case("fcmpu 1,2,3", {"f2": math.nan, "f3": 1.0}).cr_field(1) == 1
    assert run_case("fcmpo 1,2,3", {"f2": 1.0, "f3": 2.0}).cr_field(1) == 8


def test_estimates():
    assert run_case("fres 1,2", {"f2": 4.0}).f[1][0] == 0.25
    assert run_case("frsqrte 1,2", {"f2": 4.0}).f[1][0] == 0.5


def test_fpscr_round_trip():
    s = run_case("mtfsfi 7,3\nmffs 1")
    assert s.fpscr & 0xF == 3
    assert low_word(s.f[1][0]) & 0xF == 3


def test_single_op_writes_both_lanes():
    s = run_case("fadds 1,2,3", {"f2": 1.0, "f3": 2.0, "f1": (0.0, 99.0)})
    assert s.f[1] == (3.0, 3.0)
