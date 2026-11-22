# Runtime Boot Implementation Plan (Plan 2 of N)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A host executable `pikmin` that loads `main.dol`, runs the recompiled game with native replacements for the SDK libraries it calls, and boots headless through initialisation, threads, disc and ARAM loading into the game's frame loop (design milestone 2).

**Architecture:** A C++20 runtime library `pikmin_rt` provides the `ppc_*` hooks and strong `hle_*` definitions that replace the generated weak stubs. Guest threads are host fibers (`ucontext`) on one host thread, with a priority scheduler and safe-point delivery of "interrupt" callbacks. Disc access reads the user's ISO directly. Graphics, video, input, memory card and audio are headless stand-ins in this plan. Plan 3 replaces graphics, video, input and memory card with aurora.

**Tech Stack:** C++20 (gcc), CMake + Ninja, glibc `ucontext`. No new external dependencies.

**Spec:** `docs/superpowers/specs/2026-11-22-pikmin-native-recomp-design.md` (sections "2. Runtime", "3. SDK replacement", "4. Audio" (audio stubbed), "7. Configuration and UX", Milestones 2). Plan 1, already merged, is `docs/superpowers/plans/2026-11-22-recompiler.md`.

## Global Constraints

- No game data or generated code in git (`/orig/`, `/build/`, `/ghidra/` ignored). The disc image is passed at runtime: `--disc <path>`, otherwise `disc` in `$XDG_CONFIG_HOME/pikminrecomp/config.toml`. When neither is set, print one line naming both options and exit 2. Never search the filesystem.
- Runtime code is C++20 compiled with `-O2 -Wall -Wextra`. Generated code keeps Plan 1's flags (`-ffp-contract=off` etc.).
- Guest constants (GameCube SDK values, PAL disc GPIP01):
  - **Time:** bus clock 162,000,000 Hz, CPU clock 486,000,000 Hz, timebase = bus/4 = 40,500,000 ticks/s. OS time epoch is 2000-01-01 00:00:00.
  - **Memory:** RAM 24 MB, `PPC_RAM_SIZE` from `runtime/cpu/ppc.h`. ARAM 16 MB (0x01000000); `ARGetBaseAddress()` = 0x4000.
  - **Registers:** r2 = `_SDA2_BASE_` 0x803f5120, r13 = `_SDA_BASE_` 0x803e9c60. The main thread's r1 = `_stack_addr` 0x80401710.
  - **Arena:** ArenaLo = 0x80403720 (`_db_stack_addr` 0x80403710 rounded up to 32). ArenaHi = 0x817F0000.
  - **Runtime-reserved guest RAM:** [0x817F0000, 0x81800000): an interrupt-context stack (32 KB at 0x817F8000–0x81800000) and small runtime objects such as the GX FIFO object.
  - **BootInfo** at 0x80000000: bytes 0x00–0x1F are a copy of disc header bytes 0x00–0x1F; 0x20 magic 0x0D15EA5E; 0x24 version 1; 0x28 memory size 0x01800000; 0x2C console type 0x00000003; 0x30 ArenaLo; 0x34 ArenaHi; 0xF8 bus clock; 0xFC CPU clock.
- Thread priorities follow the SDK: 0 is highest, 31 lowest. The main thread has priority 16.
- HLE calling convention: arguments in r3–r10 and f1–f8; return values in r3 (64-bit in r3:r4, high word in r3) and f1.
- Guest structs are big-endian at fixed 32-bit offsets. Each struct the runtime reads or writes is documented by its offsets in the task that uses it.
- Audio is stubbed for this plan (spec §4 allows it). JAudio's entry points are overridden to no-ops (Task 8).

## Review Focus

1. **Busy-wait loops that make no OS call** (polling a flag that a callback sets) would deadlock a cooperative scheduler. Expected: the boot test detects a hang and names it, rather than spinning forever. *(Task 10: `--watchdog` reports the current guest function after 5 s with no retrace.)*
2. **Re-entrancy:** a callback (DVD done, alarm, retrace) must never run while interrupts are disabled, nor nested inside another callback. *(Task 4 test `test_callbacks_deferred_while_disabled`.)*
3. **Guest stack overflow or host stack exhaustion** from deep translated recursion on a fiber. Host fiber stacks are 8 MB, reserved with guard pages so overflow faults cleanly instead of corrupting memory. *(Task 3 test `test_deep_recursion_on_fiber`.)*
4. **Disc paths:** case, leading slash, `.`/`..` and unknown files. `DVDOpen` of a missing path must return FALSE and log the path, not crash. *(Task 6 tests.)*
5. **Wrong or unusable disc image** (not a GameCube disc, a different game ID): a one-line error naming the found ID and exit 2. *(Task 6 `test_rejects_wrong_game_id`, Task 9 CLI test.)*

---

### Task 1: Weak generated stubs + runtime skeleton

**Files:**
- Modify: `recomp/program.py` (hle stubs become weak), `tests/recomp/test_program.py`, `CMakeLists.txt`, `runtime/gen.cmake`
- Create: `runtime/CMakeLists.txt`, `runtime/rt/guest.hpp`, `runtime/rt/log.hpp`, `runtime/rt/log.cpp`, `tests/runtime/CMakeLists.txt`, `tests/runtime/check.hpp`, `tests/runtime/test_guest.cpp`

**Interfaces:**
- Produces:
  - Generated `hle_stubs.c` definitions carry `__attribute__((weak))`, so any strong `hle_*` in `pikmin_rt` wins at link time.
  - Library target `pikmin_rt` (static, C++20). Unit tests link `pikmin_rt` and provide their own `ppc_lookup`; the game executable links `pikmin_gen` + `pikmin_rt`.
  - `runtime/rt/guest.hpp` (namespace `rt`):
    - `uint32_t arg(ppc_ctx*, int i)` (r3+i); `double farg(ppc_ctx*, int i)` (f1+i ps0).
    - `void ret(ppc_ctx*, uint32_t)`; `void ret64(ppc_ctx*, uint64_t)` (r3 = high, r4 = low); `void fret(ppc_ctx*, double)`.
    - `uint32_t rd32(ppc_ctx*, uint32_t ea)`, `rd16`, `rd8`, `void wr32/wr16/wr8(ppc_ctx*, uint32_t ea, v)` (thin wrappers over `memory.h`).
    - `std::string rdstr(ppc_ctx*, uint32_t ea, size_t max = 4096)`.
    - `uint8_t* host(ppc_ctx*, uint32_t ea, uint32_t len)`: RAM only; calls `ppc_bad_access` if the range leaves RAM.
  - `runtime/rt/log.hpp`: `void log(const char* channel, const char* fmt, ...)` (printf-style) writes `[channel] message\n` to stderr. `void log_once(const char* key, const char* fmt, ...)` logs only the first time per key.
  - `tests/runtime/check.hpp`: `CHECK(expr)` and `int check_result()` (returns 1 if any check failed), shared by every runtime test executable.
- [ ] **Step 1: Write failing tests.**
  - `tests/recomp/test_program.py::test_hle_stubs_are_weak`: every `void hle_` line in `hle_stubs.c` starts with `__attribute__((weak)) void hle_`.
  - `tests/runtime/test_guest.cpp`:
    - `arg` reads r3/r4.
    - `ret64(c, 0x0000000100000002)` gives r3 = 1, r4 = 2.
    - `wr32` then `rd32` round-trips at 0x80001000 with big-endian bytes in RAM.
    - `rdstr` stops at NUL.
    - `host(c, 0x81FFFFF0, 32)` calls the test's `ppc_bad_access`, which longjmps (pattern from `tests/cpu/test_ops.c`).
- [ ] **Step 2: Run** `pytest tests/recomp/test_program.py -q -k weak` and `cmake --build build/cmake && ctest --test-dir build/cmake -R test_guest`. Expected: FAIL.
- [ ] **Step 3: Implement** the weak attribute in `program.py`, `guest.hpp`, `log.cpp` and the CMake targets (`add_subdirectory(runtime)` and `add_subdirectory(tests/runtime)` from the top level; the runtime library doesn't need the disc).
- [ ] **Step 4: Run** both commands again. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): weak HLE stubs, runtime library skeleton and guest ABI helpers`.

### Task 2: Guest printf formatter + OSReport/OSPanic

**Files:**
- Create: `runtime/rt/format.hpp`, `runtime/rt/format.cpp`, `runtime/hle/os_report.cpp`, `tests/runtime/test_format.cpp`

**Interfaces:**
- Produces:
  - `std::string rt::guest_format(ppc_ctx*, uint32_t fmt_ea, int first_gpr, int first_fpr)`. Varargs come from GPRs r[first_gpr..10] then guest stack overflow area (r1 + 8 onward, per EABI), and floats from f[first_fpr..8]. Supports flags `-+ #0`, width/precision (including `*`), length `h hh l ll`, and conversions `d i u x X o c s p f F e E g G %`. `ll` consumes an aligned GPR pair (odd register start rounded up to even, per EABI).
  - `hle_OSReport(c)`: format from r3, print to stdout unchanged (no channel prefix; the game supplies its own newlines).
  - `hle_OSPanic(c)`: args (file, line, fmt, …); prints `OSPanic at <file>:<line>: <message>` to stderr and calls `std::abort()`.
- [ ] **Step 1: Write failing tests** in `test_format.cpp`:
  - `"%d %s"` with r4 = −5 and r5 → "pikmin" gives `"-5 pikmin"`.
  - `"%08X"` with 0xBEEF gives `"0000BEEF"`.
  - `"%.2f"` with f1 = 3.14159 gives `"3.14"`.
  - `"%5.1f|%-4d|"` gives the expected padding.
  - `"%lld"` with first_gpr = 4 takes r5:r6 (pair alignment), value −1 gives `"-1"`.
  - Nine `%d` args overflow r4–r10 and read the rest from the stack at r1 + 8.
  - `"%%"` gives `"%"`.
  - `"%s"` with a NULL pointer gives `"(null)"`.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_format`. Expected: FAIL.
- [ ] **Step 3: Implement** the formatter by building each conversion spec and delegating the actual formatting to `snprintf`.
- [ ] **Step 4: Run** the test. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): guest printf formatter, OSReport, OSPanic`.

### Task 3: Fiber scheduler and OS threads

**Files:**
- Create: `runtime/rt/sched.hpp`, `runtime/rt/sched.cpp`, `runtime/hle/os_thread.cpp`, `tests/runtime/test_sched.cpp`

**Interfaces:**
- Produces (namespace `rt`):
  - `class Scheduler` (singleton `sched()`):
    - `void init(ppc_ctx* main_ctx, uint32_t main_thread_ea)`.
    - `GuestThread* create(uint32_t thread_ea, uint32_t entry, uint32_t param, uint32_t stack_top, int prio, bool detached)`: suspended, suspend count 1.
    - `void resume(GuestThread*)`, `void suspend(GuestThread*)`, `void yield()`.
    - `void sleep_on(uint32_t queue_ea)` and `void wakeup(uint32_t queue_ea)`: OSThreadQueue semantics, keyed by guest address.
    - `void exit_current(uint32_t value)`, `void cancel(GuestThread*)`, `bool join(GuestThread*, uint32_t* value)`.
    - `GuestThread* current()`, `GuestThread* by_ea(uint32_t)`, `int active_count()`.
    - `void reschedule()`: switch to the highest-priority ready thread. Equal priority is FIFO.
    - `void run_idle(std::function<bool()> step)`: called when no thread is ready; `step` advances time and delivers events, returning false when nothing can ever become ready (deadlock).
  - `struct GuestThread { uint32_t ea; int prio; enum State {Ready, Running, Waiting, Suspended, Moribund} state; ppc_ctx ctx; ... }`. Host fiber stacks are 8 MB `mmap`'d with a guard page below.
  - Each thread's `ppc_ctx` is a copy of the main ctx (mem, lc, r2, r13, GQRs). Then r1 = (stack_top − 8) & ~7, r3 = param, and the stack back-chain word at r1 is 0. The fiber body is `ppc_lookup(entry)(&ctx); exit_current(ctx.r[3]);`.
  - Guest OSThread fields the runtime writes (the game only reads these): +0x2C8 state (u16: 1 READY, 2 RUNNING, 4 WAITING, 8 MORIBUND), +0x2CA attr (u16, 1 = detached), +0x2CC suspend count (s32), +0x2D0 effective priority (s32), +0x2D4 base priority (s32), +0x2D8 exit value (u32).
  - HLE: `OSCreateThread(thread, func, param, stack, stackSize, prio, attr)` → BOOL; `OSResumeThread` → previous suspend count; `OSSleepThread(queue)`, `OSWakeupThread(queue)`, `OSInitThreadQueue(queue)` (writes head and tail = 0), `OSYieldThread`, `OSGetCurrentThread`, `OSExitThread(val)`, `OSCancelThread`, `OSJoinThread(thread, valPtr)` → BOOL, `OSIsThreadTerminated` → BOOL, `OSGetThreadPriority` → s32, `OSCheckActiveThreads` → s32.
  - A higher-priority thread made ready by `resume` or `wakeup` preempts the caller immediately.
- [ ] **Step 1: Write failing tests** in `test_sched.cpp`. The test's `ppc_lookup` maps fake guest addresses to host functions that use the HLE through `ppc_ctx`.
  - `test_resume_runs_higher_priority_first`: main (16) creates T (prio 10) that appends "T" to a log; resume(T) runs it before main appends "M"; log = "TM".
  - `test_lower_priority_runs_when_main_sleeps`: T (20) runs only after main calls `sleep_on(q)`; then T calls `wakeup(q)` and main continues.
  - `test_equal_priority_yield_round_robin`: two prio-16 threads with yield; the log interleaves.
  - `test_join_returns_exit_value`: T returns 7 from its entry; join gives 7; `OSIsThreadTerminated` is TRUE.
  - `test_guest_stack_pointer`: inside T, `ctx.r[1] == ((stack_top − 8) & ~7)` and `ctx.r[3] == param`.
  - `test_deep_recursion_on_fiber`: a host recursion of depth 50,000 with 64-byte frames inside a fiber completes.
  - `test_deadlock_detected`: main sleeps on a queue nobody wakes; `run_idle`'s step returns false; the scheduler reports "deadlock: all threads waiting" and returns an error code instead of hanging.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_sched`. Expected: FAIL.
- [ ] **Step 3: Implement** `sched.cpp` and `os_thread.cpp` with `getcontext`/`makecontext`/`swapcontext`.
- [ ] **Step 4: Run** the test. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): fiber scheduler and OS thread HLE`.

### Task 4: Time, alarms, interrupts and safe-point callbacks; mutex, cond, message queue

**Files:**
- Create: `runtime/rt/clock.hpp`, `runtime/rt/clock.cpp`, `runtime/rt/irq.hpp`, `runtime/rt/irq.cpp`, `runtime/hle/os_time.cpp`, `runtime/hle/os_sync.cpp`, `tests/runtime/test_sync.cpp`, `tests/runtime/test_irq.cpp`
- Modify: `runtime/rt/guest.hpp` (add `HLE_ENTRY(name)`: records `rt::current_function` = name + guest LR, then calls `rt::safe_point()`; every HLE function from Task 2 on starts with it)

**Interfaces:**
- Produces:
  - `rt::Clock`: `uint64_t ticks()` (guest timebase). In real mode it's host monotonic time × 40.5 MHz plus the OS-epoch offset; in fast mode it's a virtual counter that `advance_to(t)` moves forward. The mode is selected at init (`Clock::Mode::Real`/`Fast`).
  - `rt::Irq`: `bool enabled()`, `bool disable()` (returns the previous state), `void restore(bool)`, `void post(std::function<void(ppc_ctx*)> cb)` (queues a guest callback), `void deliver()` (runs queued callbacks if interrupts are enabled and we're not already inside a callback). Callbacks run on a dedicated interrupt `ppc_ctx` whose r1 = 0x81800000 − 16, with interrupts disabled during the callback.
  - `rt::safe_point()`: calls `Irq::deliver()`. Every HLE function entry (the `HLE_ENTRY()` macro in `guest.hpp`) calls it, so callbacks arrive at OS calls.
  - Timed events: `rt::schedule_at(uint64_t tick, std::function<void()> fn)`. Expired events run in `run_idle` and at safe points. Real mode sleeps the host until the next event; fast mode jumps the clock.
  - HLE:
    - `OSGetTime` → 64-bit ticks; `OSGetTick` → low 32 bits.
    - `OSTicksToCalendarTime(ticks64 in r3:r4, cal)`: writes s32 sec, min, hour, mday, mon (0-based), year, wday, yday, msec, usec at +0x00 … +0x24.
    - `OSDisableInterrupts` → previous (0/1); `OSRestoreInterrupts(level)` → previous.
    - `OSInitMutex(m)`, `OSLockMutex`, `OSUnlockMutex`: recursive per owner thread. Guest OSMutex fields: +0x08 owner thread ea, +0x0C count; the waiting queue lives host-side, keyed by the mutex ea.
    - `OSInitCond`, `OSWaitCond(cond, mutex)` (fully releases the mutex, sleeps, reacquires with the previous count), `OSSignalCond` (wakes all waiters).
    - `OSInitMessageQueue(mq, msgArray, count)`. Guest OSMessageQueue fields: +0x10 msgArray, +0x14 msgCount, +0x18 firstIndex, +0x1C usedCount. `OSSendMessage(mq, msg, flags)` → BOOL, where flags 1 = block; `OSReceiveMessage(mq, msgPtr, flags)` → BOOL.
- [ ] **Step 1: Write failing tests**:
  - `test_sync.cpp`:
    - A mutex blocks a second thread until unlock.
    - Recursive lock by the owner doesn't block.
    - A cond wait/signal handoff.
    - A message queue of capacity 2: non-blocking send to a full queue returns FALSE; a blocking receive by prio-10 T wakes on main's send and preempts; FIFO order is preserved.
  - `test_irq.cpp`:
    - `test_callbacks_deferred_while_disabled`: post a callback while disabled; `deliver()` doesn't run it; after restore(true) it runs exactly once.
    - `test_no_nested_callbacks`: a callback that posts another callback sees it run only after the first returns.
    - `test_fast_clock_jumps_to_next_event`: an event at +1 s runs in `run_idle` with no host sleep (measured under 100 ms wall time).
    - `test_calendar_epoch`: ticks 0 → 2000-01-01 00:00:00, wday 6 (Saturday).
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R "test_sync|test_irq"`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** the tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): clock, interrupts, safe points, mutex/cond/message queue`.

### Task 5: Boot: DOL loader, BootInfo, arena, OSInit, static constructors, main

**Files:**
- Create: `runtime/rt/boot.hpp`, `runtime/rt/boot.cpp`, `runtime/hle/os_misc.cpp`, `tests/runtime/test_boot.cpp`, `tests/runtime/test_ctors_bounds.py`

**Interfaces:**
- Consumes: `Scheduler`, `Irq`, `Clock` (Tasks 3–4); `ppc_lookup` (generated).
- Produces:
  - `rt::load_dol(ppc_ctx*, std::span<const uint8_t> dol)`: copies the 18 sections, zeroes BSS (DOL header 0xD8 address / 0xDC size), and returns the entry point. It throws `rt::BootError` with a message for a malformed DOL.
  - `rt::boot_guest(ppc_ctx*, const DiscHeader&, uint32_t ctors_start, uint32_t ctors_end)`:
    - Writes BootInfo (Global Constraints) and sets registers.
    - Calls every non-zero word of `.ctors` [`_ctors`, `_ctors_end`) through `ppc_lookup`. Both addresses come from the map's linker symbols, baked into `boot.cpp` as constants checked by `test_boot`.
    - Then calls `main` (0x800055c0) with r3 = 0 (argc) and r4 = 0 (argv).
  - HLE:
    - `OSInit` is idempotent: it initialises the arena and clock once (the runtime already did, so the game's own call is a no-op).
    - `OSGetArenaLo`, `OSGetArenaHi`, `OSSetArenaLo`, `OSSetArenaHi`.
    - `OSInitAlloc(start, end, maxHeaps)` returns `start + maxHeaps*0x0C` rounded up to 32 (the SDK heap-descriptor array).
    - `OSGetLanguage` → config `language` (default 0 = English); `OSGetSoundMode` → 1 (stereo); `OSSetSoundMode` no-op; `OSGetResetSwitchState` → 0; `OSSetProgressiveMode` no-op.
    - Cache ops `DCFlushRange`, `DCFlushRangeNoSync`, `DCInvalidateRange`, `DCStoreRange`, `DCStoreRangeNoSync`, `PPCSync`: no-ops. `DCZeroRange(addr, len)` zeroes memory.
    - `OSResetSystem` and `_ExitProcess`: log, then `std::exit(0)`.
    - `OSAllocFromHeap` stays unimplemented (weak stub aborts with its name) until bring-up shows it's reached.
- [ ] **Step 1: Write failing tests** in `test_boot.cpp`:
  - A synthetic DOL (built in the test) with one text section, one data section and BSS loads to the right addresses, with BSS zeroed and the entry point returned.
  - A truncated DOL throws `BootError`.
  - BootInfo words land at the Global Constraints offsets.
  - `OSInitAlloc(0x80500000, 0x81000000, 4)` returns 0x80500040.
  - `DCZeroRange` zeroes exactly the range.
  - `tests/runtime/test_ctors_bounds.py` (pytest, skips without the disc): parses the `kCtorsStart`/`kCtorsEnd` constants out of `runtime/rt/boot.cpp` and checks them against the map's `_ctors`/`_ctors_end` linker symbols.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_boot`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** the test. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): DOL loader, BootInfo, arena and OS misc HLE`.

### Task 6: Disc image (ISO) and DVD HLE

**Files:**
- Create: `runtime/rt/disc.hpp`, `runtime/rt/disc.cpp`, `runtime/hle/dvd.cpp`, `tests/runtime/test_disc.cpp`

**Interfaces:**
- **Ruling** to confirm at handoff: the spec says DVD goes through aurora (nod). This plan reads plain ISO/GCM images natively instead. Aurora's DVD API works on host structs and pointers, while the guest passes big-endian guest structs, so a native reader is simpler and needs no Rust toolchain. Compressed formats (RVZ/CISO) can come later via nod.
- Produces:
  - `rt::Disc::open(path)` → `Disc` or throws `DiscError`. It validates the GameCube magic 0xC2339F3D at 0x1C, and that the game ID is `GPIP01` (otherwise the message names the ID found).
  - `const DiscHeader& header()` (first 0x440 bytes).
  - `int path_to_entry(std::string_view path) const`: case-insensitive, leading `/` optional, handles `.`/`..`; returns −1 if not found.
  - `FileRef file(int entry) const` (offset, length), `size_t read(uint64_t offset, std::span<uint8_t>) const`.
  - HLE. Guest DVDFileInfo layout: DVDCommandBlock 0x30 bytes (+0x08 command, +0x0C state, +0x10 offset, +0x14 length, +0x18 addr, +0x20 transferredSize, +0x28 callback, +0x2C userData), then +0x30 startAddr, +0x34 length, +0x38 callback.
    - `DVDInit`.
    - `DVDOpen(path, fi)` → BOOL; logs `dvd: open <path>` on success (info) and `dvd: not found <path>` on a miss.
    - `DVDFastOpen(entry, fi)` → BOOL; `DVDConvertPathToEntrynum(path)` → s32; `DVDClose` → TRUE.
    - `DVDReadPrio(fi, addr, len, offset, prio)` → bytes read (synchronous).
    - `DVDReadAsyncPrio(fi, addr, len, offset, cb, prio)` → TRUE. The data is copied at completion; completion fires at least 1 ms of clock time later via `schedule_at`, then `Irq::post` calls `cb(result=len, fi)`. Command-block state goes 1 (busy) → 0 (end).
    - `DVDGetDriveStatus` → 1 while any async read is pending, else 0.
    - `DVDGetCurrentDiskID` → 0x80000000.
    - `DVDPrepareStreamAsync(fi, len, offset, cb)` → TRUE, calling `cb(0, fi)` via `Irq::post`. `DVDCancelStream(block)` → 0. Disc audio streaming is stubbed with audio.
- [ ] **Step 1: Write failing tests** in `test_disc.cpp`. The test builds a tiny synthetic GCM in memory: header with ID `GPIP01`, an FST containing `/a.txt` and `/dir/B.bin`, and data.
  - `path_to_entry("a.txt") == path_to_entry("/A.TXT")`; `"dir/../a.txt"` resolves; `"nope"` gives −1.
  - Reading `/dir/B.bin` returns its bytes.
  - `test_rejects_wrong_game_id`: ID `GXXE01` throws `DiscError` with a message containing `GXXE01`.
  - Bad magic throws.
  - DVD HLE through a fake ctx:
    - `DVDOpen` fills startAddr and length.
    - `DVDReadPrio` copies into guest RAM.
    - `DVDReadAsyncPrio`'s callback runs only after `Irq::deliver()` following the clock passing completion, with r3 = len and r4 = fi.
    - A missing path returns FALSE and logs it.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_disc`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** the test. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): ISO disc reader and DVD HLE`.

### Task 7: ARAM and MMIO register file

**Files:**
- Create: `runtime/hle/ar.cpp`, `runtime/rt/mmio.cpp`, `tests/runtime/test_ar_mmio.cpp`

**Interfaces:**
- Produces:
  - ARAM: a 16 MB host buffer.
    - `ARInit(stack, n)` → 0x4000; `ARGetBaseAddress` → 0x4000; `ARQInit` no-op.
    - `ARQPostRequest(req, owner, type, prio, src, dst, len, cb)`: type 0 copies RAM→ARAM, type 1 copies ARAM→RAM, both immediately. `cb(req)` is delivered via `Irq::post`. ARAM addresses outside 16 MB raise `ppc_bad_access`.
  - `ppc_mmio_read` / `ppc_mmio_write` (replacing `runtime/cpu/stubs.c` for the game executable; `stubs.c` stays for Plan 1's tests). A register file maps physical address → last written value; reads return it, default 0.
    - Writes to WGPIPE 0x0C008000 are counted (`rt::mmio_wgpipe_bytes()`) and dropped in headless mode.
    - Each distinct register is logged once via `log_once`.
- [ ] **Step 1: Write failing tests**:
  - RAM→ARAM→RAM round-trips 64 bytes.
  - The ARQ callback is deferred until `deliver()`.
  - An out-of-range ARAM address faults.
  - An MMIO write then read of 0x0C00500A returns the value.
  - A WGPIPE write increments the byte counter by the access size.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_ar_mmio`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** the test. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): ARAM HLE and MMIO register file`.

### Task 8: Headless VI, PAD, CARD, GX, audio and debug-IO stand-ins

**Files:**
- Create: `runtime/hle/vi.cpp`, `runtime/hle/pad.cpp`, `runtime/hle/card.cpp`, `runtime/hle/gx_headless.cpp`, `runtime/hle/audio_stub.cpp`, `runtime/hle/debug_io.cpp`, `tests/runtime/test_headless.cpp`
- Modify: `recomp/overrides.toml` (add `[functions]` entries for JAudio entry points)

**Interfaces:**
- Produces:
  - **VI.** The retrace rate is 50 Hz (PAL) unless `VIConfigure`'s render mode +0x00 viTVmode selects 60 Hz (EuRGB60 = 5, NTSC = 0). Retraces are clock events.
    - `VIWaitForRetrace` sleeps the current thread until the next retrace. Each retrace increments the count, then posts the post-retrace callback (if set) with r3 = count.
    - `VIGetRetraceCount`; `VISetPostRetraceCallback(cb)` → previous callback.
    - `VIInit`, `VIConfigure`, `VISetNextFrameBuffer`, `VIFlush`, `VISetBlack`: no-ops apart from mode tracking.
  - **PAD.** `PADInit` → 1. `PADRead(status)` writes 4 × 12-byte PADStatus with err (+0x0A, s8) = −1 (no controller) and all other bytes zero, then returns 0. `PADReset`/`PADRecalibrate` → 1; `PADClamp`/`PADSetSpec`/`PADControlMotor` are no-ops.
  - **CARD** ("no card inserted"). `CARDInit` no-op; `CARDProbe` → 0; every other referenced CARD function returns −3 (CARD_RESULT_NOCARD) without calling callbacks. `CARDGetXferredBytes` → 0.
  - **GX.** All 77 referenced GX functions are no-ops (X-macro list from `hle_surface`), except `GXInit(base, size)`, which returns guest address 0x817F0000 (a zeroed 0x80-byte GXFifoObj in reserved RAM).
  - **Audio.** `ai.a` and `dsp.a` HLE are no-ops. `AIGetStreamSampleCount`/`AIGetStreamTrigger` → 0, `DSPCheckMailFromDSP` → 0, `DSPCheckMailToDSP` → 0. `overrides.toml [functions]` sets `Jac_Init = "override"` and `StartAudioThread = "override"`, with `hle_Jac_Init` and `hle_StartAudioThread` as no-ops in `audio_stub.cpp`.
  - **Debug IO.** `HIOInit` → 0 (FALSE), `HIOEnumDevices` → 0; the other HIO functions → 0. `InitializeUART` → 0; `WriteUARTN(buf, len)` writes the guest bytes to stderr and returns 0; `ReadUARTN` → 0.
- [ ] **Step 1: Write failing tests** in `test_headless.cpp`:
  - In fast clock mode, a thread calling `VIWaitForRetrace` 10 times sees `VIGetRetraceCount` advance by 10. The post-retrace callback runs 10 times with increasing r3.
  - `PADRead` writes err = −1 for all 4 ports.
  - `CARDMount` returns −3.
  - `GXInit` returns 0x817F0000.
  - `WriteUARTN` echoes its bytes (captured by redirecting stderr to a pipe).
  - Add to `tests/recomp/test_program.py`: `test_jaudio_entry_points_overridden` (no `void f_<Jac_Init addr>(` definition in the generated shards).
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_headless` and `pytest tests/recomp -q -k jaudio`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** both. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): headless VI/PAD/CARD/GX/audio/debug stand-ins`.

### Task 9: `pikmin` executable, config and CLI

**Files:**
- Create: `runtime/main.cpp`, `runtime/rt/config.hpp`, `runtime/rt/config.cpp`, `tests/runtime/test_config.cpp`, `tests/runtime/test_cli.py`
- Modify: `runtime/gen.cmake` (add the `pikmin` executable: `pikmin_gen` + `pikmin_rt`, without `pikmin_stubs`)

**Interfaces:**
- Produces:
  - Config, read from `$XDG_CONFIG_HOME/pikminrecomp/config.toml` (fallback `~/.config/...`) with a minimal TOML subset parser: `key = "string" | integer | true/false`, `#` comments. Keys: `disc` (string), `language` (int 0–5), `headless` (bool). An unknown key gives a warning, not an error.
  - CLI (overrides config): `pikmin [--disc PATH] [--headless] [--fast] [--frames N] [--watchdog SECONDS] [--config PATH]`.
    - `--frames N` exits 0 after N retraces.
    - `--fast` uses the virtual clock.
    - `--watchdog S` (default 5): if no retrace happens for S seconds of host time, print `watchdog: no retrace for S s; current thread <ea> pc-function <name>`, then exit 3. The current function name comes from a `rt::current_function` breadcrumb that `HLE_ENTRY` records: the HLE name plus the guest LR.
    - Without a usable disc, print one line and exit 2.
  - For Plan 2, the executable requires `--headless` (or the config key). Without it, it says the windowed mode arrives with Plan 3 and exits 2.
- [ ] **Step 1: Write failing tests**:
  - `test_config.cpp` parses a sample config with comments, ints, strings and bools; an unknown key gives a warning.
  - `tests/runtime/test_cli.py` (pytest, skips if `build/cmake/pikmin` doesn't exist):
    - No disc and an empty config dir (`XDG_CONFIG_HOME=tmp`) gives exit 2, with stderr mentioning `--disc` and `config.toml`.
    - A `--disc` pointing at a non-GameCube file gives exit 2 with a one-line message.
    - Running without `--headless` gives exit 2 mentioning Plan 3's windowed mode.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_config` and `pytest tests/runtime -q`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** both. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): pikmin executable, config and CLI`.

### Task 10: Headless boot bring-up

**Files:**
- Create: `tests/runtime/test_boot_game.py`, `docs/superpowers/notes/boot-bringup.md` (a log of what each fix was and why)
- Modify: whichever runtime/HLE module the bring-up shows is wrong or missing

**Interfaces:**
- Consumes: everything above.
- The gate (Milestone 2), all on the user's disc: `build/cmake/pikmin --disc ~/Downloads/GPIP01/game.iso --headless --fast --frames 600` must
  1. exit 0,
  2. print at least one line of game `OSReport` output on stdout,
  3. log at least 5 successful `dvd: open` lines,
  4. create at least 2 guest threads (logged by `OSCreateThread`),
  5. print no `ppc_hle_missing`, `ppc_unimpl` or `watchdog` lines.
- [ ] **Step 1: Write the failing gate test** `tests/runtime/test_boot_game.py::test_headless_boot_600_frames`. It asserts the five conditions above, skips if the disc or the binary is missing, and has a 300 s timeout.
- [ ] **Step 2: Run** `pytest tests/runtime/test_boot_game.py -v`. Expected: FAIL, most likely at the first `ppc_hle_missing`, a watchdog, or a deadlock.
- [ ] **Step 3: Bring-up loop.** Repeat until the gate passes:
  1. Read the first failure.
  2. Add a unit test that reproduces it in the owning module's test file (Tasks 2–9), and watch it fail.
  3. Fix it.
  4. Run the full suite (`pytest -q && ctest --test-dir build/cmake`).
  5. Append a line to `docs/superpowers/notes/boot-bringup.md`: the symptom, the cause, the fix, and the test name.
  6. Commit `fix(runtime): <what>`.

  Bounds on scope:
  - **Missing HLE functions:** implement only what the boot reaches.
  - **Audio-related failures:** extend the JAudio override list in `overrides.toml` with a note, rather than emulating the DSP.
  - **A busy-wait hang that the watchdog names:** fix it by making the HLE function it polls a safe point, or by delivering the awaited event sooner. Never by patching game code.
- [ ] **Step 4: Run** the gate: `pytest tests/runtime/test_boot_game.py -v`. Expected: PASS.
- [ ] **Step 5: Run everything:** `pytest -q && cmake --build build/cmake && ctest --test-dir build/cmake --output-on-failure`. Expected: all pass.
- [ ] **Step 6: Commit** `test(runtime): headless boot gate passes (600 frames)`.
