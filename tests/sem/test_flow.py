import struct

import pytest

from recomp.translate import RecompError, translate_words
from tests.sem.harness import BASE, assemble, run_case

CALLEE = "void f_80004000(ppc_ctx *c) { c->r[3] = 7; }"
ENTRY = {0x80004000: "f_80004000"}


def test_loop_ctr():
    s = run_case("li 3,0\nL: addi 3,3,1\nbdnz L", {"ctr": 5})
    assert s.r[3] == 5 and s.ctr == 0


def test_beq_taken_and_not():
    asm = "cmpwi 3,0\nbeq L\nli 4,1\nb E\nL: li 4,2\nE: nop"
    assert run_case(asm, {"r3": 0}).r[4] == 2
    assert run_case(asm, {"r3": 1}).r[4] == 1


def test_bdz_and_condition_combined():
    # bdzt: decrement CTR, branch if CTR==0 and CR bit true
    asm = "cmpwi 3,0\nbdzt 2,L\nli 4,1\nb E\nL: li 4,2\nE: nop"
    assert run_case(asm, {"r3": 0, "ctr": 1}).r[4] == 2
    assert run_case(asm, {"r3": 0, "ctr": 2}).r[4] == 1


def test_bl_to_entry():
    s = run_case("bl .+0x1000", extra_c=CALLEE, entries=ENTRY)
    assert s.r[3] == 7 and s.lr == 0x80003004


def test_tail_call():
    s = run_case("li 3,1\nb .+0xffc", extra_c=CALLEE, entries=ENTRY)
    assert s.r[3] == 7
    assert "f_80004000(c); return;" in s.c_source


def test_conditional_bl():
    asm = "cmpwi 4,0\nbeql .+0xffc"
    assert run_case(asm, {"r4": 0}, extra_c=CALLEE, entries=ENTRY).r[3] == 7
    assert run_case(asm, {"r4": 1, "r3": 9}, extra_c=CALLEE, entries=ENTRY).r[3] == 9


def test_conditional_return():
    asm = "cmpwi 3,0\nbeqlr\nli 4,5"
    assert run_case(asm, {"r3": 0}).r[4] == 0
    assert run_case(asm, {"r3": 1}).r[4] == 5


def test_bctrl_via_lookup():
    s = run_case("bctrl", {"ctr": 0x80123456})
    assert s.r[3] == 0x42 and s.lr == 0x80003004


def test_blrl_via_lookup():
    s = run_case("blrl", {"lr": 0x80123456})
    assert s.r[3] == 0x42 and s.lr == 0x80003004


def test_jump_table_switch():
    table = 0x80300000
    labels = [BASE + 0x18, BASE + 0x20, BASE + 0x28]
    blob = struct.pack(">3I", *labels)
    asm = ("lis 5,0x8030\naddi 5,5,0\nslwi 0,3,2\nlwzx 0,5,0\nmtctr 0\nbctr\n"
           "li 4,10\nblr\nli 4,11\nblr\nli 4,12\nblr")
    s = run_case(asm, {"r3": 2}, {table: blob}, data={table: blob}, tables={table: 3})
    assert s.r[4] == 12
    assert "switch" in s.c_source


def test_branch_into_foreign_body_raises():
    words = assemble("bl .+0x1000")
    with pytest.raises(RecompError) as e:
        translate_words(words, BASE, "f_test", bodies={"foo": (0x80003FF0, 0x80004100)})
    msg = str(e.value)
    assert "0x80003000" in msg and "0x80004000" in msg and "foo" in msg


def test_fallthrough_tail_calls_next():
    words = assemble("li 3,1")[:-1]  # drop the harness blr
    src = translate_words(words, BASE, "f_test", entries={BASE + 4: "f_next"})
    assert src.rstrip().endswith("f_next(c); return;\n}") or "f_next(c); return;" in src


def test_cr_logical():
    # cr0 = EQ (bit 2), cr1 = GT (bit 5)
    base = {"r3": 0, "r4": 1}
    pre = "cmpwi 0,3,0\ncmpwi 1,4,0\n"
    assert run_case(pre + "cror 31,2,5", base).cr & 1 == 1
    assert run_case(pre + "crand 31,2,4", base).cr & 1 == 0
    assert run_case(pre + "crxor 31,2,5", base).cr & 1 == 0
    assert run_case(pre + "crnor 31,0,4", base).cr & 1 == 1
    s = run_case(pre + "mcrf 7,1", base)
    assert s.cr_field(7) == 4


def test_jump_table_hoisted_base():
    # CodeWarrior hoists the table address into a callee-saved register early in the function.
    table = 0x80300000
    labels = [BASE + 0x34, BASE + 0x3C]
    blob = struct.pack(">2I", *labels)
    asm = ("lis 4,0x8030\naddi 26,4,0\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
           "slwi 0,3,2\nlwzx 0,26,0\nmtctr 0\nbctr\n"
           "li 5,10\nblr\nli 5,11\nblr")
    s = run_case(asm, {"r3": 1}, {table: blob}, data={table: blob}, tables={table: 2})
    assert s.r[5] == 11
    assert "switch" in s.c_source


def test_jump_table_hoisted_base_with_epilogue_restore():
    # The base register is restored by lmw in the epilogue and the lis register is reused later.
    table = 0x80300000
    labels = [BASE + 0x1C, BASE + 0x24]
    blob = struct.pack(">2I", *labels)
    asm = ("lis 4,0x8030\naddi 26,4,0\nli 4,0\n"
           "slwi 0,3,2\nlwzx 0,26,0\nmtctr 0\nbctr\n"
           "li 5,10\nb X\nli 5,11\nX: lmw 26,0(1)\nblr")
    s = run_case(asm, {"r3": 1}, {table: blob}, data={table: blob}, tables={table: 2})
    assert s.r[5] == 11
    assert "switch" in s.c_source


def test_jump_table_base_from_stack_uses_function_tables():
    # Base address reloaded from a stack slot: fall back to tables whose entries all lie in this function.
    table = 0x80300000
    labels = [BASE + 0x14, BASE + 0x1C]
    blob = struct.pack(">2I", *labels)
    asm = ("lwz 26,8(1)\nslwi 0,3,2\nlwzx 0,26,0\nmtctr 0\nbctr\n"
           "li 5,10\nblr\nli 5,11\nblr")
    s = run_case(asm, {"r3": 1}, {table: blob, 0x80400008: struct.pack(">I", table)},
                 data={table: blob}, tables={table: 2})
    assert s.r[5] == 11
    assert "switch" in s.c_source


def test_long_lis_distance():
    table = 0x80300000
    labels = [BASE + 0x3C, BASE + 0x44]
    blob = struct.pack(">2I", *labels)
    asm = ("lis 5,0x8030\n" + "nop\n" * 9 + "addi 25,5,0\nslwi 0,3,2\nlwzx 0,25,0\nmtctr 0\nbctr\n"
           "li 5,10\nblr\nli 5,11\nblr")
    s = run_case(asm, {"r3": 1}, {table: blob}, data={table: blob}, tables={table: 2})
    assert s.r[5] == 11


def test_two_blrl_in_one_function():
    s = run_case("blrl\nmtlr 4\nblrl", {"lr": 0x80123456, "r4": 0x80123456})
    assert s.r[3] == 0x42


def test_self_recursive_call_is_real_call():
    # count(n) = n ? 1 + count(n - 1) : 0, with a stack frame.
    asm = ("count: stwu 1,-16(1)\nmflr 0\nstw 0,20(1)\ncmpwi 3,0\nbeq Z\n"
           "addi 3,3,-1\nbl count\naddi 3,3,1\nb D\n"
           "Z: li 3,0\nD: lwz 0,20(1)\nmtlr 0\naddi 1,1,16")
    s = run_case(asm, {"r3": 3})
    assert s.r[3] == 3
    assert s.r[1] == 0x80400000


def test_bl_to_alternate_entry_inside_function_is_call():
    s = run_case("bl .+8\nb E\nli 3,9\nE: nop", extra_c="void f_alt(ppc_ctx *c) { c->r[3] = 5; }",
                 entries={BASE + 8: "f_alt"})
    assert s.r[3] == 5


def test_pc_get_idiom_still_works():
    assert run_case("bl .+4\nmflr 3").r[3] == BASE + 4


def test_linked_branch_into_own_body_raises():
    words = assemble("bl .+8\nnop\nnop")
    with pytest.raises(RecompError, match="0x80003008"):
        translate_words(words, BASE, "f_test")
