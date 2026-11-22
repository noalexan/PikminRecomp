from tests.sem.harness import run_case

M32 = 0xFFFFFFFF
LT, GT, EQ, SO = 8, 4, 2, 1
CA = 0x20000000
OV = 0x40000000
XSO = 0x80000000


def test_addi_r0_is_literal_zero():
    assert run_case("addi 3,0,5", {"r0": 100}).r[3] == 5


def test_addis():
    assert run_case("addis 3,4,1", {"r4": 2}).r[3] == 0x10002


def test_addc_sets_carry():
    s = run_case("addc 3,4,5", {"r4": M32, "r5": 1})
    assert s.r[3] == 0 and s.xer & CA


def test_adde_uses_carry():
    assert run_case("adde 3,4,5", {"r4": 1, "r5": 1, "xer": CA}).r[3] == 3


def test_subfic_carry_clear():
    s = run_case("subfic 3,4,0", {"r4": 1})
    assert s.r[3] == M32 and not s.xer & CA


def test_addze_addme():
    assert run_case("addze 3,4", {"r4": 5, "xer": CA}).r[3] == 6
    s = run_case("addme 3,4", {"r4": 5, "xer": 0})
    assert s.r[3] == 4 and s.xer & CA


def test_subfe_and_neg():
    s = run_case("subfe 3,4,5", {"r4": 3, "r5": 10, "xer": CA})  # 10 - 3
    assert s.r[3] == 7 and s.xer & CA
    assert run_case("neg 3,4", {"r4": 5}).r[3] == (-5) & M32


def test_add_record_and_so():
    assert run_case("add. 3,4,5", {"r4": 1, "r5": 2}).cr_field(0) == GT
    assert run_case("add. 3,4,5", {"r4": 1, "r5": 2, "xer": XSO}).cr_field(0) == GT | SO


def test_addo_overflow():
    s = run_case("addo. 3,4,5", {"r4": 0x7FFFFFFF, "r5": 1})
    assert s.r[3] == 0x80000000 and s.xer & OV and s.xer & XSO
    assert s.cr_field(0) == LT | SO


def test_multiply():
    assert run_case("mullw 3,4,5", {"r4": 7, "r5": (-6) & M32}).r[3] == (-42) & M32
    assert run_case("mulhw 3,4,5", {"r4": (-2) & M32, "r5": 3}).r[3] == M32
    assert run_case("mulhwu 3,4,5", {"r4": M32, "r5": 2}).r[3] == 1
    assert run_case("mulli 3,4,-3", {"r4": 5}).r[3] == (-15) & M32


def test_divide():
    assert run_case("divw 3,4,5", {"r4": (-7) & M32, "r5": 2}).r[3] == (-3) & M32
    assert run_case("divwu 3,4,5", {"r4": M32, "r5": 2}).r[3] == 0x7FFFFFFF
    run_case("divw 3,4,5", {"r4": 1, "r5": 0})  # must not trap
    run_case("divw 3,4,5", {"r4": 0x80000000, "r5": M32})


def test_rotates():
    assert run_case("rlwinm 3,4,2,0,29", {"r4": 0x40000001}).r[3] == 0x00000004
    assert run_case("rlwimi 3,4,8,16,23", {"r3": 0x11111111, "r4": 0xAB}).r[3] == 0x1111AB11
    assert run_case("rlwnm 3,4,5,0,31", {"r4": 0x80000001, "r5": 33}).r[3] == 3


def test_shifts():
    assert run_case("slw 3,4,5", {"r4": 1, "r5": 32}).r[3] == 0
    assert run_case("srw 3,4,5", {"r4": 0x80000000, "r5": 31}).r[3] == 1
    s = run_case("sraw 3,4,5", {"r4": (-3) & M32, "r5": 1})
    assert s.r[3] == (-2) & M32 and s.xer & CA
    s = run_case("srawi 3,4,1", {"r4": (-3) & M32})
    assert s.r[3] == (-2) & M32 and s.xer & CA
    s = run_case("srawi 3,4,1", {"r4": 4})
    assert s.r[3] == 2 and not s.xer & CA


def test_bit_ops():
    assert run_case("cntlzw 3,4", {"r4": 0}).r[3] == 32
    assert run_case("cntlzw 3,4", {"r4": 1}).r[3] == 31
    assert run_case("extsb 3,4", {"r4": 0x80}).r[3] == 0xFFFFFF80
    assert run_case("extsh 3,4", {"r4": 0x8000}).r[3] == 0xFFFF8000
    v = {"r4": 0xF0F0F0F0, "r5": 0xFF00FF00}
    assert run_case("and 3,4,5", v).r[3] == 0xF000F000
    assert run_case("andc 3,4,5", v).r[3] == 0x00F000F0
    assert run_case("or 3,4,5", v).r[3] == 0xFFF0FFF0
    assert run_case("orc 3,4,5", v).r[3] == 0xF0FFF0FF
    assert run_case("xor 3,4,5", v).r[3] == 0x0FF00FF0
    assert run_case("nand 3,4,5", v).r[3] == 0x0FFF0FFF
    assert run_case("nor 3,4,5", v).r[3] == 0x000F000F
    assert run_case("eqv 3,4,5", v).r[3] == 0xF00FF00F


def test_andi_always_records():
    s = run_case("andi. 3,4,0", {"r4": 5})
    assert s.r[3] == 0 and s.cr_field(0) == EQ


def test_logical_immediates():
    assert run_case("ori 3,4,0x00FF", {"r4": 0x100}).r[3] == 0x1FF
    assert run_case("oris 3,4,0x1234", {"r4": 1}).r[3] == 0x12340001
    assert run_case("xori 3,4,0xFFFF", {"r4": 0xFFFF}).r[3] == 0
    assert run_case("xoris 3,4,0x8000", {"r4": 0}).r[3] == 0x80000000


def test_compares():
    assert run_case("cmpw 1,3,4", {"r3": (-1) & M32, "r4": 1}).cr_field(1) == LT
    assert run_case("cmplw 1,3,4", {"r3": M32, "r4": 1}).cr_field(1) == GT
    assert run_case("cmpwi 3,-5", {"r3": (-5) & M32}).cr_field(0) == EQ
    assert run_case("cmplwi 7,3,10", {"r3": 3}).cr_field(7) == LT
