# PPC→C Recompiler Implementation Plan (Plan 1 of N)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Translate every non-SDK function of Pikmin's `main.dol` into C that compiles and links, with tests proving the instruction semantics. This covers milestones 0b and 1 of the design.

**Architecture:** A Python package `recomp/` reads `main.dol` + `build.map`, decodes PowerPC (Gekko) instructions, and emits one C function per guest function operating on a `ppc_ctx` register struct and a big-endian guest RAM buffer. C helper headers in `runtime/cpu/` define the context, memory access and instruction helpers. SDK libraries listed in `recomp/overrides.toml` are not translated: calls to them go to `hle_*` host symbols (abort stubs in this plan; real implementations come in Plan 2).

**Tech Stack:** Python 3.15 (stdlib only; `tomllib`), pytest, C17 for generated and helper code, gcc, CMake ≥ 3.25 + Ninja, GNU binutils for powerpc (test assembly only).

**Spec:** `docs/superpowers/specs/2026-11-22-pikmin-native-recomp-design.md` (sections "Constraints and known facts", "1. Recompiler", the SDK table in "3. SDK replacement", "Milestones" 0–1).

## Global Constraints

- No game bytes, generated C or other disc-derived output in git: `/orig/`, `/build/` and `/ghidra/` stay ignored. Tests that need the disc skip cleanly (pytest `skip` / ctest exit 77) when `orig/GPIP01/sys/main.dol` is missing.
- Inputs: `orig/GPIP01/sys/main.dol` and `orig/GPIP01/files/dataDir/build.map` (CRLF line endings, Latin-1). Output: `build/gen/`.
  - **Deviation from spec:** the spec names `build/main.elf` as the input. The recompiler reads the DOL + map directly instead, because only the map carries the library/object ownership and `(entry of X)` secondary entry points the recompiler needs. The ELF is derived from the same two files anyway.
- System packages via `dnf` only; nothing installed into `$HOME`; no root needed to build or run.
- Guest small-data bases: r13 = 0x803e9c60, r2 = 0x803f5120.
- Guest RAM: 24 MB (0x01800000). Gekko locked cache: 16 KB at 0xE0000000.
- Generated function names: `f_%08x` (lowercase hex guest address). Override host names: `hle_<name>`, where `<name>` is the guest symbol with every character outside `[A-Za-z0-9_]` replaced by `_`.
- Libraries overridden in this plan (from the spec's SDK table plus the debugger/hardware libs present in the map): `gx.a vi.a pad.a si.a dvd.a card.a os.a ar.a exi.a ai.a dsp.a db.a hio.a base.a amcExi2.a amcnotstub.a OdemuExi2.a odenotstub.a TRK_MINNOW_DOLPHIN.a`. Translated: everything else, including `mtx.a`, `MSL_C.PPCEABI.bare.H.a`, `Runtime.PPCEABI.H.a`, `jaudio.o`, `hvqm4dec.a` and all game libraries.

## Review Focus

1. **Huge generated build:** about 2.2 MB of code becomes several hundred thousand lines of C. A full build must finish in under 15 minutes on 32 threads, with no single TU over 4 GB RSS. *(Pinned in Task 10, Step 6.)*
2. **Branches into the middle of another function** (a target that is neither a map entry nor an `(entry of)` point) must fail generation with both addresses named, not silently miscompile. *(Task 6, Step 1, `test_branch_into_foreign_body_raises`.)*
3. **Stale output:** editing `recomp/overrides.toml` or any `recomp/*.py` must regenerate `build/gen/` on the next build. *(Task 10, Step 5.)*
4. **Missing or wrong input files** (no DOL, a map from another build) must produce a one-line error, not a traceback or garbage C. *(Task 1, `test_missing_dol_message`, `test_map_dol_mismatch`.)*
5. **Float edge cases:** single rounding (`frsp`, `fmuls`), `fctiwz` saturation, NaN handling and divide by zero must match Gekko. *(Task 7 cases.)*

---

### Task 1: Toolchain + image loader

**Files:**
- Create: `recomp/__init__.py`, `recomp/image.py`, `tests/recomp/test_image.py`, `tests/conftest.py`, `pyproject.toml` (pytest config only: `testpaths = ["tests"]`)

**Interfaces:**
- Produces:
  - `class ImageError(Exception)`
  - `@dataclass(frozen=True) Function(name: str, addr: int, size: int, library: str | None, obj: str, parent: str | None)`. `parent` is set for `(entry of X)` lines, with `size` = bytes from this entry to the parent's end.
  - `@dataclass(frozen=True) DataSymbol(name: str, addr: int, size: int, section: str)`
  - `class Image` with `functions: list[Function]` (sorted by addr), `data: list[DataSymbol]`, `read_u32(addr: int) -> int` (big-endian, raises `ImageError` outside loaded sections), `function_at(addr: int) -> Function | None` (exact entry match), `function_containing(addr: int) -> Function | None` (parent functions only), `data_at(addr: int) -> DataSymbol | None`
  - `load_image(dol: Path, map_path: Path) -> Image`
  - `tests/conftest.py` fixture `game_image` (session scope) that skips when the DOL is missing.

- [ ] **Step 1: Install test tooling** (user runs it; needs sudo): `sudo dnf install -y python3-pytest binutils-powerpc64-linux-gnu`. Verify: `pytest --version` and `powerpc64-linux-gnu-as --version` both print versions.
- [ ] **Step 2: Write the failing tests** in `tests/recomp/test_image.py`:
  - `test_parses_layout_line`: a synthetic map containing `.text section layout` and the line `  00000040 000114 80003140  4 __start \tos.a __start.c` gives `Function("__start", 0x80003140, 0x114, "os.a", "__start.c", None)`.
  - `test_object_without_library`: line `  00000000 000088 800055c0  4 main \tsysBootup.o ` gives `library=None, obj="sysBootup.o"`.
  - `test_entry_of_line`: parent `__save_fpr` at 0x80216bac size 0x4c, plus the line `  00211604 000000 80216bc4 _savefpr_20 (entry of __save_fpr) \tRuntime.PPCEABI.H.a runtime.c`, gives `parent="__save_fpr", size=0x80216bf8-0x80216bc4`.
  - `test_skips_section_markers_and_ellipsis`: lines whose name equals the section (`.text`) or starts with `...` produce nothing.
  - `test_missing_dol_message`: `load_image(Path("nope.dol"), map)` raises `ImageError` whose message contains `nope.dol` and `not found`.
  - `test_map_dol_mismatch`: a map whose `.text` start address differs from the DOL's raises `ImageError` mentioning `does not match`.
  - `test_real_image` (uses `game_image`): `function_at(0x80003140).name == "__start"`, `function_at(0x8021b194).name == "strlen"`, `function_at(0x80216bc4).parent == "__save_fpr"`, `len([f for f in fns if f.parent]) == 395`, `read_u32(0x800055c0) == 0x7c0802a6`.
- [ ] **Step 3: Run** `pytest tests/recomp/test_image.py -v`. Expected: FAIL (module missing).
- [ ] **Step 4: Implement** `recomp/image.py`. Parse section layouts for `.init`/`.text` into `Function`s, and the other sections into `DataSymbol`s (`@NNN` labels included, since jump tables are `@` objects). Parse the `Memory map:` block to validate it against the DOL's section addresses. Strip `\r`.
- [ ] **Step 5: Run** the tests. Expected: PASS (`test_real_image` passes, or skips if the disc is missing).
- [ ] **Step 6: Commit** `feat(recomp): DOL + link map image loader`.

### Task 2: Instruction decoder

**Files:**
- Create: `recomp/decode.py`, `tests/recomp/test_decode.py`

**Interfaces:**
- Produces: `@dataclass(frozen=True) Insn(addr: int, word: int, op: str, a: dict[str, int])`, and `decode(word: int, addr: int) -> Insn`.
  - `op` is the base mnemonic, without `.`/`o` suffixes and without simplified forms: `addi`, not `li`; `bc`/`bclr`/`bcctr`, not `beq`/`blr`; `or`, not `mr`.
  - Unknown encodings give `op="unknown"`.
  - Argument keys: `rd ra rb rs rc_reg frd fra frb frc frs crfd crfs crbd crba crbb bo bi bd li aa lk rc oe simm uimm d sh mb me spr tbr crm fm nb l w i ps_d`. Signed fields are already sign-extended; branch displacements are in bytes.
  - Coverage: all Gekko user-mode integer, load/store (including update, indexed, `lmw`/`stmw`, `lwbrx`/`sthbrx`, `lswi`/`stswi`), branch, CR-logical, FP (including `fsel`, `fres`, `frsqrte`, `mffs`/`mtfsf*`), paired single (opcode 4 incl. `ps_*`, `psq_lx/stx/lux/stux`; opcodes 56/57/60/61 `psq_l/lu/st/stu`), system (`mfspr`/`mtspr`, `mftb`, `mfmsr`/`mtmsr`, `mfsr`/`mtsr`, `sync`, `isync`, `eieio`, `dcbf/dcbi/dcbst/dcbt/dcbtst/dcbz/dcbz_l/icbi`, `sc`, `rfi`, `tw`/`twi`).
- [ ] **Step 1: Write failing tests**:
  - `test_known_words`: `0x7c0802a6 → mfspr spr=8 rd=0`; `0x90010004 → stw rs=0 ra=1 d=4`; `0x9421ffe8 → stwu d=-0x18`; `0x4e800020 → bclr bo=20 lk=0`; `0x4e800420 → bcctr`; `0x5483103a → rlwinm rs=4 ra=3 sh=2 mb=0 me=29 rc=0`; `0x806d2d6c → lwz rd=3 ra=13 d=0x2d6c`; `0xe0030000 → psq_l frd=0 ra=3 w=0 i=0 ps_d=0`; `0x48000001` (bl +0) `→ b li=0 lk=1`.
  - `test_unknown_word`: `0x00000000 → op "unknown"`.
  - `test_whole_dol_decodes` (uses `game_image`): every word of every non-overridden function (overridden libraries as in Global Constraints) decodes to an op other than `unknown`. On failure it prints the first 20 offenders as `addr word function`.
- [ ] **Step 2: Run** `pytest tests/recomp/test_decode.py -v`. Expected: FAIL.
- [ ] **Step 3: Implement** `decode` as table-driven dispatch on the primary opcode, then the extended opcode (31: bits 1–10; 63: bits 1–10, with A-form ops on bits 1–5; 4: Gekko paired-single sub-tables).
- [ ] **Step 4: Run** the tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat(recomp): Gekko instruction decoder`.

### Task 3: C runtime CPU headers

**Files:**
- Create: `runtime/cpu/ppc.h`, `runtime/cpu/memory.h`, `runtime/cpu/ops.h`, `runtime/cpu/ps.h`, `tests/cpu/test_ops.c`, `tests/cpu/CMakeLists.txt`, `CMakeLists.txt` (top-level: project, C17, `enable_testing()`, `add_subdirectory(tests/cpu)`)

**Interfaces:**
- Produces (C, all `static inline` unless noted):
  - `ppc.h`: `typedef struct { double ps0, ps1; } ppc_ps;` and
    `typedef struct ppc_ctx { uint32_t r[32]; ppc_ps f[32]; uint32_t cr, lr, ctr, xer, fpscr, hid2; uint32_t gqr[8]; uint8_t *mem; uint8_t *lc; } ppc_ctx;`
    `typedef void (*ppc_fn)(ppc_ctx *);`
    Extern hooks: `ppc_fn ppc_lookup(uint32_t addr);`, `_Noreturn void ppc_unimpl(ppc_ctx *, uint32_t addr, uint32_t word);`, `_Noreturn void ppc_bad_access(uint32_t ea);`, `uint64_t ppc_timebase(void);`, `uint32_t ppc_mmio_read(uint32_t phys, int size);`, `void ppc_mmio_write(uint32_t phys, uint32_t value, int size);`
  - `memory.h`: `mem_r8/r16/r32/r64(ppc_ctx*, uint32_t ea)`, `mem_w8/w16/w32/w64(ppc_ctx*, uint32_t ea, value)`, `mem_rf32(c, ea) -> float`, `mem_rf64 -> double`, `mem_wf32`, `mem_wf64`, `mem_host(c, ea) -> uint8_t*` (RAM only).
    Address routing: `ea` in `[0xE0000000, 0xE0004000)` → `c->lc`. Otherwise `phys = ea & 0x1FFFFFFF`: `phys < 0x01800000` → `c->mem[phys]`; `phys >= 0x0C000000` → `ppc_mmio_*`; anything else → `ppc_bad_access`. Values are stored big-endian.
  - `ops.h`: `ppc_rotl(uint32_t, unsigned)`, `ppc_mask(unsigned mb, unsigned me)` (wraps when mb > me), `ppc_cr_set(c, field, nibble)`, `ppc_cr_get(c, field)`, `ppc_cr_bit(c, bi)`, `ppc_cmp_s(c, field, int32_t a, int32_t b)`, `ppc_cmp_u(c, field, uint32_t a, uint32_t b)`, `ppc_rc(c, uint32_t result)` (field 0 vs 0, signed, SO copied from XER), `ppc_cmp_f(c, field, double a, double b)` (unordered → FU=1), `ppc_get_ca/ppc_set_ca`, `ppc_round_single(double) -> double`, `ppc_fctiw(double, int round_toward_zero) -> uint32_t`.
    CR layout as hardware: field n occupies bits `28-4n .. 31-4n`, nibble LT=8 GT=4 EQ=2 SO=1. XER: SO=0x80000000 OV=0x40000000 CA=0x20000000.
  - `ps.h`: `ppc_psq_load(c, ea, int w, int gqr_idx, double *ps0, double *ps1)` and `ppc_psq_store(c, ea, double ps0, double ps1, int w, int gqr_idx)`. GQR fields: ST_TYPE bits 0–2, ST_SCALE bits 8–13, LD_TYPE bits 16–18, LD_SCALE bits 24–29 (6-bit signed). Types: 0 = f32, 4 = u8, 5 = u16, 6 = s8, 7 = s16. Load: `value * 2^-scale`; store: `value * 2^scale`, then saturate to the type's range, truncating toward zero. `w=1` → one element, ps1 = 1.0.
- [ ] **Step 1: Write failing C tests** in `tests/cpu/test_ops.c` (a tiny `CHECK(expr)` macro; exit 1 on failure). Cases:
  - `ppc_mask(0,29)==0xFFFFFFFC`, `ppc_mask(30,1)==0xC0000003`, `ppc_rotl(0x80000001,1)==3`.
  - `ppc_cmp_s(c,0,-1,1)` → field 0 == 8; `ppc_cmp_u(c,1,0xFFFFFFFF,1)` → field 1 == 4.
  - With XER SO set, `ppc_rc(c,0)` → field 0 == 3.
  - Write32 at 0x80001000 of 0x11223344 → `mem[0x1000..3] == {0x11,0x22,0x33,0x44}`, and `mem_r32(c,0xC0001000) == 0x11223344`.
  - `mem_w32(c,0xCC008000,5)` → the test's `ppc_mmio_write` hook records `(0x0C008000, 5, 4)`.
  - `mem_w32(c,0xE0000010,7)` lands in `c->lc[0x10..]`, not in RAM.
  - psq: GQR0 load type u8 scale 0 with byte 200 → 200.0; GQR1 load type s16 scale 8 with 0xFF38 → −0.78125; GQR2 store type u8 scale 1, 1.5 → byte 3; store 300.0 as u8 → 255; −5.0 → 0.
  - `ppc_fctiw(3.9,1)==3`, `ppc_fctiw(-3.9,1)==0xFFFFFFFD`, `ppc_fctiw(1e10,1)==0x7FFFFFFF`, `ppc_fctiw(-1e10,1)==0x80000000`, `ppc_fctiw(NAN,1)==0x80000000`.
  - `ppc_round_single(0.1) == (double)0.1f`.
- [ ] **Step 2: Run** `cmake -S . -B build/cmake -G Ninja && cmake --build build/cmake && ctest --test-dir build/cmake -R test_ops --output-on-failure`. Expected: FAIL (headers missing).
- [ ] **Step 3: Implement** the four headers.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): PPC context, memory and op helpers`.

### Task 4: Translator core + semantics harness + integer instructions

**Files:**
- Create: `recomp/translate.py`, `recomp/emit/__init__.py`, `recomp/emit/integer.py`, `tests/sem/harness.py`, `tests/sem/harness_main.c.in`, `tests/sem/test_integer.py`

**Interfaces:**
- Consumes: `decode`, `Insn` (Task 2); the C headers (Task 3).
- Produces:
  - `class RecompError(Exception)`.
  - `class TranslateContext` (protocol) with `read_u32(addr) -> int`, `call_target(addr) -> str | None` (C symbol of a function *entry* or override), `is_function_body(addr) -> str | None` (name of a parent function whose body contains addr, for errors), and `jump_table(addr) -> tuple[int, int] | None` (table address, entry count) for a data symbol starting at addr.
  - `translate_function(ctx: TranslateContext, name: str, start: int, end: int, comment: str = "") -> str`. Returns a full C definition `void name(ppc_ctx *c) { ... }`.
  - `translate_words(words: list[int], addr: int, name: str, *, data: dict[int, bytes] = {}, tables: dict[int, int] = {}, entries: dict[int, str] = {}) -> str`. Builds a standalone context for tests: `entries` maps other function addresses to C symbols; any other branch target outside the words is treated as foreign.
  - `EMITTERS: dict[str, Callable[[Insn, FnState], str]]` in `recomp/emit/__init__.py`, plus `FnState` (holds ctx, current function range and the set of label addresses). Each emit module registers its ops. An op without an emitter emits `ppc_unimpl(c, addr, word);` and is counted in `FnState.unimpl`.
  - Harness: `run_case(asm: str, regs: dict[str, int | float | tuple[float, float]] = {}, mem: dict[int, bytes] = {}, **translate_kwargs) -> State`, where `State` has `r: list[int]`, `f: list[tuple[float, float]]`, `cr`, `xer`, `lr`, `ctr`, `fpscr`, `gqr`, and `mem(addr, n) -> bytes`.
    `run_case` assembles at 0x80003000 with `powerpc64-linux-gnu-as -a32 -mbig -mgekko` (if that gas lacks `-mgekko`, use `-m750cl`, and note which in the harness docstring), extracts `.text` with `powerpc64-linux-gnu-objcopy -O binary`, translates, compiles with `cc -std=c17 -O1 -Iruntime/cpu`, runs the result, and parses its JSON register dump. Guest RAM in the harness is 0x01800000 bytes; r1 defaults to 0x80400000. `harness_main.c.in` provides the extern hooks; its `ppc_lookup` returns a stub that sets r3 = 0x42.
- [ ] **Step 1: Write failing tests** in `tests/sem/test_integer.py`, one per behaviour (asm → expected):
  - `addi r3,r0,5` → r3 = 5 (rA=0 means literal 0); `addis r3,r4,1` with r4 = 2 → 0x10002.
  - `addc r3,r4,r5` with 0xFFFFFFFF, 1 → r3 = 0, CA set; `adde r3,r4,r5` with CA = 1, 1 + 1 → 3; `subfic r3,r4,0` with r4 = 1 → 0xFFFFFFFF, CA clear; `addze`, `addme`, `subfe`, `neg`.
  - `add. r3,r4,r5` → CR0 = GT for positive results, and SO copied when XER.SO is set; `addo.` with 0x7FFFFFFF + 1 sets OV|SO.
  - `mullw`, `mulhw` (−2 × 3 high word = 0xFFFFFFFF), `mulhwu`, `divw` (−7/2 = −3), `divwu`, and `divw` by zero (result unspecified; must not trap).
  - `rlwinm r3,r4,2,0,29`, `rlwimi`, `rlwnm`, `slw` with shift 32 → 0, `srw`, `sraw` with a negative value and a 1-bit lost → CA set, `srawi r3,r4,1` with r4 = −3 → −2, CA set.
  - `cntlzw` of 0 → 32; `extsb` 0x80 → 0xFFFFFF80; `extsh`; `and/andc/or/orc/xor/nand/nor/eqv`; `andi.` always sets CR0; `ori/oris/xori/xoris`.
  - `cmpw cr1,r3,r4`, `cmplw`, `cmpwi`, `cmplwi`: the expected CR field values.
- [ ] **Step 2: Run** `pytest tests/sem/test_integer.py -v`. Expected: FAIL.
- [ ] **Step 3: Implement** `translate.py` (prologue, labels for every in-function branch target, sequential emission, a final fall-through as described in Task 6) and `emit/integer.py`.
- [ ] **Step 4: Run** the tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat(recomp): translator core and integer instructions`.

### Task 5: Loads and stores

**Files:**
- Create: `recomp/emit/loadstore.py`, `tests/sem/test_loadstore.py`

**Interfaces:**
- Consumes: `EMITTERS`, `run_case`; the `mem_*` helpers.
- [ ] **Step 1: Write failing tests**:
  - `lwz`/`lhz`/`lha` (0x8000 → 0xFFFF8000)/`lbz`, plus the `x` forms; `lwzu` updates rA to the EA; `stw/sth/stb` and their `u`/`x` forms check the bytes in memory.
  - `lmw r29,0(r3)` / `stmw` round-trip three registers; `lwbrx`/`stwbrx`/`lhbrx`/`sthbrx` reverse the bytes.
  - `lfs` loads single bits into a double (1.5f → 1.5); `lfd`; `stfs` rounds to single; `stfd`; `stfiwx` stores the low 32 bits of the FPR's raw double bits.
  - `lswi`/`stswi` with nb = 7.
  - An EA computed with rA = 0 means literal 0 (`lwz r3,0x1000(0)` reads physical 0x1000).
  - A store to 0xCC008000 reaches the harness MMIO hook (the hook logs into a buffer that `State` exposes as `mmio: list[tuple[int, int, int]]`).
- [ ] **Step 2: Run** `pytest tests/sem/test_loadstore.py -v`. Expected: FAIL.
- [ ] **Step 3: Implement** `emit/loadstore.py`.
- [ ] **Step 4: Run** the tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat(recomp): load/store instructions`.

### Task 6: Control flow

**Files:**
- Create: `recomp/emit/flow.py`, `tests/sem/test_flow.py`

**Interfaces:**
- Consumes: `TranslateContext.call_target`, `.is_function_body`, `.jump_table`; `ppc_lookup`.
- Rules (each one is a test below):
  - **Intra-function `b`/`bc`:** `goto L_%08x`. `bc` honours BO: decrement CTR, test CTR == 0 / != 0, test the CR bit true / false, or always.
  - **`bl`:** `c->lr = addr+4; <target>(c);`, where the target is the C symbol from `call_target`. A `bl` to a foreign non-entry address raises `RecompError("branch from 0x… into body of <name> at 0x…")`.
  - **`b`/`bc` to another function's entry** (no link): `<target>(c); return;` (a tail call).
  - **`bclr`:** conditionally `return;`. **`bclrl`:** `{ uint32_t t = c->lr; c->lr = addr+4; ppc_lookup(t)(c); }`.
  - **`bcctr` with a resolvable jump table:** the preceding `lwzx` base is materialised by `lis/addi` (or `addis/addi`/`lis/ori`) to an address where `jump_table` returns `(table, n)`, and every entry lies inside the function. Emit `switch (c->ctr) { case 0x…: goto L_…; … default: ppc_lookup(c->ctr)(c); return; }`.
  - **Unresolved `bcctr`:** `ppc_lookup(c->ctr)(c); return;`. **`bcctrl`:** `c->lr = addr+4; ppc_lookup(c->ctr)(c);`.
  - **Fall-through off the end** of a function whose last instruction isn't an unconditional branch: tail call the function at `end` (needed for `(entry of)` chains), or raise `RecompError` if there is none.
  - **CR logical ops** `crand/cror/crxor/crnand/crnor/creqv/crandc/crorc/mcrf`, plus `mcrxr`.
  - **`sc`/`rfi`/`tw`/`twi`:** `ppc_unimpl`.
- [ ] **Step 1: Write failing tests** (all via `run_case`):
  - `test_loop_ctr`: `li r3,0; li r4,5; mtctr r4; L: addi r3,r3,1; bdnz L` → r3 = 5.
  - `test_beq_taken_and_not`.
  - `test_bl_to_entry`: `entries={0x80004000: "f_80004000"}`. The harness defines `f_80004000` to set r3 = 7. `bl 0x80004000` → r3 = 7 and lr = 0x80003004.
  - `test_tail_call` (`b` to an entry, then return).
  - `test_bctrl_via_lookup` → r3 = 0x42.
  - `test_jump_table_switch`: table at 0x80300000 with 3 entries pointing at labels in the function, `tables={0x80300000: 3}`, `data` holding the entries; r3 = 2 selects case 2. Assert the generated C contains `switch`.
  - `test_branch_into_foreign_body_raises`: a context where 0x80004000 is inside function `foo` (0x80003ff0–0x80004100) but not an entry → `translate_words` raises `RecompError` whose message contains `0x80003000`, `0x80004000` and `foo`.
  - `test_fallthrough_tail_calls_next`.
  - `test_cror` with the expected CR results.
- [ ] **Step 2: Run** `pytest tests/sem/test_flow.py -v`. Expected: FAIL.
- [ ] **Step 3: Implement** `emit/flow.py`, plus jump-table detection by a backward scan of at most 8 instructions within the basic block.
- [ ] **Step 4: Run** the tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat(recomp): control flow, calls and jump tables`.

### Task 7: Floating point

**Files:**
- Create: `recomp/emit/fpu.py`, `tests/sem/test_fpu.py`

**Interfaces:**
- Semantics: FPRs hold doubles in `ps0`. Single-precision ops (`fadds`, `fmuls`, `fmadds`, …) compute in double and then `ppc_round_single`, writing **both** ps0 and ps1 (Gekko behaviour). Fused multiply-add uses `fma()`. `fres`/`frsqrte` compute exact `1/x` and `1/sqrt(x)`; Gekko's estimates are less precise, and exact is acceptable under the "feels right" bar. `fctiw`/`fctiwz` write the result to the low word of the FPR's raw bits. `mffs`/`mtfsf`/`mtfsfi`/`mtfsb0`/`mtfsb1` read and write `c->fpscr`; only the RN bits (rounding mode) need to affect arithmetic, via `fesetround` when RN ≠ 0, and the emitter may assume RN = 0 elsewhere. `fcmpu`/`fcmpo` set the CR field via `ppc_cmp_f`. Record forms (`.`) copy FPSCR bits 0–3 to CR1.
- [ ] **Step 1: Write failing tests**:
  - `fadd`, `fsub`, `fmul`, `fdiv`; `fdivs` 1/0 → +inf; `fmuls` of 0.1 × 3 equals `(float)(0.1*3)` rounding.
  - `frsp` rounding; `fmadds` fused (a case where fused and unfused differ in double: a = 1+2^-30, c = 1−2^-30, b = −1 → −2^-60 fused).
  - `fneg/fabs/fnabs/fmr`; `fsel` with a negative / zero / NaN selector; `fctiwz` on 1e10 → low word 0x7FFFFFFF.
  - `fcmpu cr1` with a NaN → CR1 = FU (1).
  - `fres(4.0) == 0.25`; `frsqrte(4.0) == 0.5`.
  - `mtfsfi` then `mffs` round-trip.
  - The single op writes ps1 = ps0.
- [ ] **Step 2: Run** `pytest tests/sem/test_fpu.py -v`. Expected: FAIL.
- [ ] **Step 3: Implement** `emit/fpu.py`.
- [ ] **Step 4: Run** the tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat(recomp): floating point instructions`.

### Task 8: Paired singles

**Files:**
- Create: `recomp/emit/paired.py`, `tests/sem/test_paired.py`

**Interfaces:**
- Consumes: `ppc_psq_load` / `ppc_psq_store` (Task 3).
- Semantics: every `ps_*` result is rounded to single per lane. Ops: `ps_add/sub/mul/div/madd/msub/nmadd/nmsub/neg/abs/nabs/mr/sel/res/rsqrte`, `ps_sum0/sum1`, `ps_muls0/muls1`, `ps_madds0/madds1`, `ps_merge00/01/10/11`, `ps_cmpu0/cmpo0/cmpu1/cmpo1`, and `psq_l/lu/lx/lux/st/stu/stx/stux`.
- [ ] **Step 1: Write failing tests**:
  - `psq_l f1,0(r3),0,0` with GQR0 = 0 and floats 1.5, 2.5 in memory → f1 = (1.5, 2.5).
  - `psq_l` with w = 1 → (1.5, 1.0).
  - `psq_l` via GQR2 = u8 scale 0 → bytes (200, 10) → (200.0, 10.0).
  - `psq_st` round-trip.
  - `ps_add` lanes; `ps_sum0 f1,f2,f3,f4` = (f2.ps0 + f4.ps1, f3.ps1); `ps_madds0`; `ps_merge10` = (a.ps1, b.ps0); `ps_cmpu1` sets the CR field.
  - Rounding: `ps_mul` lanes equal `(float)` products.
- [ ] **Step 2: Run** `pytest tests/sem/test_paired.py -v`. Expected: FAIL.
- [ ] **Step 3: Implement** `emit/paired.py`.
- [ ] **Step 4: Run** the tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat(recomp): Gekko paired-single instructions`.

### Task 9: System and SPR instructions

**Files:**
- Create: `recomp/emit/system.py`, `tests/sem/test_system.py`

**Interfaces:**
- Semantics:
  - `mfspr`/`mtspr` for LR (8), CTR (9), XER (1), GQR0–7 (912–919) and HID2 (920) map to ctx fields. Any other SPR reads 0 and ignores writes, with a comment naming the SPR.
  - `mftb` (tbr 268/269) splits `ppc_timebase()`.
  - `mfcr`/`mtcrf` (honouring the crm mask).
  - `mfmsr` returns 0x00008000 (EE set); `mtmsr`/`mfsr`/`mtsr` are no-ops.
  - `sync/isync/eieio/dcbf/dcbi/dcbst/dcbt/dcbtst/icbi` emit nothing (a comment).
  - `dcbz` zeroes the 32-byte block at `EA & ~31`; `dcbz_l` zeroes 32 bytes in the locked cache.
- [ ] **Step 1: Write failing tests**:
  - `mtctr`/`mfctr`, `mtlr`/`mflr`, `mtxer`/`mfxer`, `mtspr 912` (GQR0) round-trips.
  - `mtcrf 0x80,r3` changes only field 0.
  - `mfcr` reflects an earlier `cmpwi`.
  - `dcbz` over 0x80001010 zeroes 0x80001000–0x8000101F and nothing else.
  - `mftb`, with a harness timebase of 0x0000000100000002 → upper 1, lower 2.
- [ ] **Step 2: Run** `pytest tests/sem/test_system.py -v`. Expected: FAIL.
- [ ] **Step 3: Implement** `emit/system.py`.
- [ ] **Step 4: Run** the tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat(recomp): system and SPR instructions`.

### Task 10: Whole-program generation, overrides and build

**Files:**
- Create: `recomp/overrides.toml`, `recomp/program.py`, `recomp/__main__.py`, `runtime/cpu/stubs.c` (default extern hooks for linking: `ppc_mmio_*`, `ppc_timebase`, `ppc_bad_access`, `ppc_unimpl` and `_Noreturn void ppc_hle_missing(const char *name, uint32_t addr)` print and abort), `runtime/gen.cmake`, `tests/recomp/test_program.py`, `tests/game/test_real_functions.c`, `tests/game/CMakeLists.txt`
- Modify: `CMakeLists.txt` (include `runtime/gen.cmake` and `tests/game` when the DOL exists)

**Interfaces:**
- Consumes: `load_image` (Task 1), `translate_function` (Task 4), all emitters.
- Produces:
  - `overrides.toml`:
    ```toml
    [libraries]
    override = ["gx.a", "vi.a", ...]   # exactly the Global Constraints list

    [functions]
    # guest symbol = "override" | "translate"   (per-function exceptions)
    ```
  - `generate(img: Image, overrides: Path, out: Path) -> Report`, where `Report(functions: int, overridden: int, unimpl: list[tuple[int, int, str]], errors: list[str])`. It writes:
    - `out/gen_<obj>.c`: one file per source object, sanitised name; split a file into `_partN` when it passes 20,000 lines.
    - `out/functable.c`: a sorted `{addr, fn}` array plus `ppc_lookup` (binary search; on a miss, print the address and abort). The table covers translated functions and overrides.
    - `out/hle_stubs.c`: one `void hle_X(ppc_ctx *c)` per overridden function, calling `ppc_hle_missing("<guest name>", addr)`, which prints and aborts.
    - `out/gen.h`: prototypes.
    - `out/report.txt`.
  - CLI: `python3 -m recomp --dol <path> --map <path> --overrides recomp/overrides.toml --out build/gen`. Exit code is non-zero if `errors` is non-empty.
  - CMake: a custom command producing `build/gen/*` with `DEPENDS` on the DOL, the map, `recomp/overrides.toml` and every `recomp/**/*.py`. Library `pikmin_gen` (static, `-O2 -fno-strict-aliasing -fwrapv`), linked into `pikmin_link_check`, an executable whose `main` only prints `ppc_lookup(0x800055c0) != NULL`.
- [ ] **Step 1: Write failing tests** in `tests/recomp/test_program.py` (uses `game_image`):
  - `test_report_clean`: running `generate` into `tmp_path` gives `errors == []` and `unimpl == []`.
  - `test_overrides_applied`: no `gen_*.c` contains a definition of `f_801f4230` (OSInit, `os.a`); `hle_stubs.c` contains `hle_OSInit`; `functable.c` maps `0x801f4230` to `hle_OSInit`.
  - `test_entry_points_emitted`: `f_80216bc4` (`_savefpr_20`) is defined.
  - `test_main_calls`: the text of `f_800055c0` contains `f_` calls for `Initialise__6SystemFv` and `alloc__6SystemFUl`, at their map addresses.
- [ ] **Step 2: Write failing C tests** in `tests/game/test_real_functions.c`. The test loads the DOL sections into guest RAM itself, a ~20-line DOL section copy that exists only for the test (Plan 2 owns the real loader), and calls translated functions directly:
  - `strlen` (`f_8021b194`) on "pikmin" placed at 0x80500000 → r3 = 6.
  - `memset` (`f_80003384`) fills 16 bytes with 0xAB.
  - `__div2i` (`f_80216d30`) with r3:r4 = −1000000000000 and r5:r6 = 7 → r3:r4 = −142857142857.
  - `PSMTXIdentity` (`f_801fabdc`) then `PSMTXConcat` (`f_801fac08`) of a translation matrix with identity → the input matrix, compared as floats read big-endian from guest RAM. GQR0 = 0 and r2/r13 are set per Global Constraints.
  - Exit 77 if the DOL is missing.
- [ ] **Step 3: Run** `pytest tests/recomp/test_program.py -v`. Expected: FAIL.
- [ ] **Step 4: Implement** `program.py`, `__main__.py`, `overrides.toml`, `stubs.c` and `gen.cmake`. Then run `pytest tests/recomp/test_program.py -v`. Expected: PASS.
  - If `test_report_clean` lists unimpl ops or errors, fix them in the owning emitter task's module, with a new semantics test there.
- [ ] **Step 5: Verify regeneration (Review Focus 3).**
  - Run `cmake --build build/cmake --target pikmin_link_check`.
  - Then `touch recomp/overrides.toml`, rebuild, and confirm the build log shows the recompiler ran again.
  - Then rebuild with no changes and confirm it does nothing ("no work to do").
- [ ] **Step 6: Verify build cost (Review Focus 1).** Run `cmake --build build/cmake --clean-first` under `/usr/bin/time -v`.
  - Expected: wall time under 15 minutes, max RSS under 4 GB.
  - If a TU exceeds the budget, lower the split threshold, or compile files above a line count with `-O1`. Record the chosen values in `runtime/gen.cmake`.
- [ ] **Step 7: Run everything:** `pytest -v && ctest --test-dir build/cmake --output-on-failure`. Expected: all pass; game tests pass, or skip without the disc. `./build/cmake/pikmin_link_check` prints `1`.
- [ ] **Step 8: Commit** `feat(recomp): whole-program generation, overrides and build`. Confirm that `git status` shows no files under `build/` or `orig/`.
