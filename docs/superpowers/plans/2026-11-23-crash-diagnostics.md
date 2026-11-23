# Crash Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every run leaves a persistent log, and every crash, fatal guest error or watchdog leaves a report with enough evidence to find and replay the cause.

**Architecture:** A small `rt/diag` core owns the log file and an async-signal-safe writer that goes to both stderr and the log. `rt/trace` keeps a fixed ring of recent events, fed from existing choke points (HLE_ENTRY, DVD, scheduler, IRQ, GXCopyDisp, section init). `rt/crash` installs signal handlers on an alternate stack and writes one report format, shared by signals, `ppc_*` fatal hooks and the watchdog. `--record-input` writes pad state in `--input-script` format for replay.

**Tech Stack:** C++20 runtime (`pikmin_rt`), POSIX signals, glibc `backtrace`, CMake, pytest.

**Spec:** `CLAUDE.md`, section "Planned: crash diagnostics" (items 1–6).

## Global Constraints

- Log dir: `$XDG_STATE_HOME/pikminrecomp/logs/pikmin-<timestamp>.log`, default `~/.local/state`; keep the last **10** runs.
- Log is line-buffered and flushed on every fatal path.
- Log header: git hash, Debug or Release, config values, GPU adapter, display mode, clock mode (real/fast).
- Signals: `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`, handled on an alternate signal stack; report written async-signal-safely; then re-raise (default action, so `coredumpctl` gets a core).
- Report contents: signal + fault address; host backtrace (`f_<addr>` frames); current guest thread (OSThread ea, prio); `lr`, `r1`, `r3`–`r5` of its `ppc_ctx`; whether in an IRQ callback; retrace count; last HLE name + lr; recent-events dump.
- `ppc_bad_access`, `ppc_unimpl`, `ppc_hle_missing` and the watchdog write the same report.
- Ring: a few thousand entries (use **4096**), no I/O until a crash. `PIKMIN_TRACE=hle,dvd,...` prints chosen channels live.
- Release builds with `-g1`, keep `-rdynamic`.
- `--record-input PATH` writes pad input per retrace in `--input-script` format.
- Never patch generated code; game hooks go through `"hook"` in `recomp/overrides.toml`.
- Exits from a fiber use `rt::exit_process()`. Nothing installs into `$HOME` (the log/state dir is runtime data, like saves).

## Review Focus

1. **Crash inside the signal handler path itself** (e.g. a fault while walking a corrupt fiber stack): a second signal must not loop; handlers use `SA_RESETHAND` and a re-entry guard, so the second fault kills the process with the default action. Test: Task 3 `test_double_fault_terminates`.
2. **Fatal hook then `abort()` → SIGABRT handler**: must not write a second report. Test: Task 3 `test_bad_access_one_report`.
3. **Unwritable or missing state dir** (read-only `$XDG_STATE_HOME`, `$HOME` unset): the game still runs, stderr still gets everything, one warning line. Test: Task 1 `log_dir_unwritable`.
4. **Many runs at once / same second**: two runs started in the same second must not share a file; name includes the pid. Rotation deletes only `pikmin-*.log` files. Test: Task 1 `rotation_keeps_ten`.
5. **DVD path strings and other non-static text in the ring** must be copied, never stored as pointers into guest memory or temporaries. Test: Task 2 `ring_copies_text`.

---

## File Structure

- `runtime/rt/diag.hpp/.cpp` — log file (open, rotate, header), `diag_write` (async-signal-safe, to stderr + log fd), small async-safe formatters (`fmt_hex32`, `fmt_dec`).
- `runtime/rt/trace.hpp/.cpp` — event ring, channel mask from `PIKMIN_TRACE`, `trace_dump(int fd)`.
- `runtime/rt/crash.hpp/.cpp` — signal handlers, `crash_report(...)`, alt stack.
- `runtime/rt/build_info.cpp.in` + `runtime/write_build_info.cmake` — git hash and build type.
- `runtime/rt/input_record.hpp/.cpp` — `--record-input` writer.
- `runtime/hle/sections.cpp` — section-init hooks that record scene changes.
- Modified: `runtime/rt/log.cpp`, `runtime/rt/hooks.cpp`, `runtime/rt/guest.hpp` (HLE_ENTRY), `runtime/rt/sched.cpp`, `runtime/rt/irq.cpp`, `runtime/hle/dvd.cpp`, `runtime/hle/gx/gx_geometry.cpp`, `runtime/hle/pad.cpp`, `runtime/rt/gfx.cpp`, `runtime/main.cpp`, `runtime/CMakeLists.txt`, `runtime/gen.cmake`, `recomp/overrides.toml`, `tools/resolve_bt.py`, `CLAUDE.md`.

---

### Task 1: Persistent log file

**Files:**
- Create: `runtime/rt/diag.hpp`, `runtime/rt/diag.cpp`, `runtime/rt/build_info.cpp.in`, `runtime/write_build_info.cmake`
- Modify: `runtime/rt/log.cpp`, `runtime/rt/gfx.cpp`, `runtime/main.cpp`, `runtime/CMakeLists.txt`, every `fprintf(stderr, ...)` in `runtime/` except `cpu/stubs.c` and `rt/hooks.cpp` (Task 3 rewrites those)
- Test: `tests/runtime/test_diag.cpp`, `tests/runtime/test_cli.py`

**Interfaces:**
- Produces (namespace `rt`):
  - `std::string log_dir()` — `$XDG_STATE_HOME/pikminrecomp/logs` or `$HOME/.local/state/pikminrecomp/logs`.
  - `bool log_open(const std::string& dir, int keep = 10)` — creates dir, deletes the oldest `pikmin-*.log` so that at most `keep - 1` remain, opens `pikmin-YYYYmmdd-HHMMSS-<pid>.log`; false (and one stderr warning) if it can't.
  - `const std::string& log_path()` — empty if no file.
  - `int log_fd()` — -1 if none.
  - `void diag_write(const char* s, size_t n)` / `void diag_write(const char* s)` — async-signal-safe `write()` to fd 2 and the log fd (loops on short writes / EINTR).
  - `char* fmt_hex32(char out[11], uint32_t v)` (`"0x%08x"`), `char* fmt_dec(char out[21], int64_t v)` — async-safe.
  - `extern const char* const kBuildHash; extern const char* const kBuildType;`
- `rt::log` writes each line to stderr and, if open, the log file (formatted once into a buffer, then `diag_write`). Lines are written whole, so the file is effectively line-buffered with no stdio buffer to flush.

- [ ] **Step 1: Write failing tests** in `tests/runtime/test_diag.cpp` (register in `tests/runtime/CMakeLists.txt`):
  - `rotation_keeps_ten`: in a `mkdtemp` dir create 12 files `pikmin-2026010{1..12}-000000-1.log` (mtime ascending) plus `other.txt`; `log_open(dir, 10)`; expect exactly 10 `pikmin-*.log` files (9 old + new), the two oldest gone, `other.txt` present, and `log_path()` contains `"-" + std::to_string(getpid()) + ".log"`.
  - `log_line_reaches_file`: after `log_open`, `rt::log("test", "hello %d", 42)`; file contents contain `"[test] hello 42\n"`.
  - `log_dir_unwritable`: `log_open("/proc/nonexistent/x")` returns false; `log_fd() == -1`; `rt::log` still works.
  - `fmt_helpers`: `fmt_hex32(b, 0x8001234a)` == `"0x8001234a"`; `fmt_dec(b, -12)` == `"-12"`; `fmt_dec(b, 0)` == `"0"`.
- [ ] **Step 2:** `cmake --build build/cmake && ctest --test-dir build/cmake -R test_diag` → fails (does not compile).
- [ ] **Step 3: Implement** `diag.cpp`; `rt::log`/`log_once` route through it. Build info: `write_build_info.cmake` runs `git rev-parse --short HEAD` (+ `-dirty` if `git diff --quiet HEAD` fails) and writes `build_info.cpp` into the binary dir only when the content changed; an `add_custom_target(pikmin_build_info ALL ...)` runs it on every build with `BYPRODUCTS build_info.cpp`; `kBuildType` comes from `$<CONFIG>` (empty → `"Debug"`). Add `build_info.cpp` to `pikmin_rt`.
- [ ] **Step 4: Header and call sites.** In `main.cpp`, right after config/CLI parsing (before disc open): `log_open(log_dir())`, then log one `[run]` block: `pikmin <hash> (<type>)`, command line, each `Config` field (`disc`, `language`, `window_width`×`window_height`, `fullscreen`, `vsync`, `audio`, `aspect`, `target_fps`, `hud_scale`, `camera`), mode (`headless`/`offscreen`/`window`), clock (`fast`/`real-time`), and `log file: <path>`. In `gfx.cpp`, `aurora_log` sends `LOG_INFO` messages to the log file only (adds `void log_file_only(const char* channel, const char* fmt, ...)` to `log.hpp`), which records aurora's adapter/backend lines; after init log `display <w>x<h> @ <hz> Hz` from SDL's current display mode. Replace the other `fprintf(stderr, ...)` call sites in `runtime/` with `rt::log` (keep `fail()`'s one-line `pikmin: ...` stderr format, but also write it to the log).
- [ ] **Step 5: CLI test** in `tests/runtime/test_cli.py`: `test_run_writes_log_file` — `run(["--headless","--fast","--frames","60"], ...)` with `XDG_STATE_HOME=tmp/state`; exactly one `tmp/state/pikminrecomp/logs/pikmin-*.log`; it contains `"pikmin "`, `"(Debug)"` or `"(Release)"`, `"clock: fast"` and `"[boot]"`. Extend `run()` to accept extra env.
- [ ] **Step 6:** `ctest --test-dir build/cmake && pytest -q tests/runtime` → all pass.
- [ ] **Step 7: Commit** `feat(diag): persistent per-run log file with build/config header`.

### Task 2: Recent-events ring buffer

**Files:**
- Create: `runtime/rt/trace.hpp`, `runtime/rt/trace.cpp`, `runtime/hle/sections.cpp`
- Modify: `runtime/rt/guest.hpp` (HLE_ENTRY), `runtime/rt/sched.cpp` (`switch_to`), `runtime/rt/irq.cpp` (`run_queue`), `runtime/hle/dvd.cpp` (DVDOpen/DVDFastOpen/DVDReadPrio/async reads), `runtime/hle/gx/gx_geometry.cpp` (`hle_GXCopyDisp`), `recomp/overrides.toml`, `runtime/CMakeLists.txt`
- Test: `tests/runtime/test_trace.cpp`

**Interfaces:**
- Consumes: `rt::diag_write`, `rt::fmt_hex32`, `rt::fmt_dec`, `rt::log` (Task 1).
- Produces (namespace `rt`):
  - `enum class Ev : uint8_t { Hle, Dvd, Thread, Irq, Frame, Scene };` channel names `"hle","dvd","thread","irq","frame","scene"`.
  - `struct TraceEntry { uint64_t tick; uint32_t a, b; Ev kind; const char* name; char text[40]; };` — `name` must be a string literal / static; `text` is a copy (truncated, NUL-terminated).
  - `void trace(Ev kind, const char* name, uint32_t a = 0, uint32_t b = 0, const char* text = nullptr)` — inline fast path: write slot `g_trace_pos++ & 4095`; if the channel's bit is set in `g_trace_live`, also `rt::log("trace", ...)`.
  - `uint32_t trace_parse_channels(const char* spec)` — comma list → bitmask; `"all"` = every channel; unknown names ignored.
  - `void trace_init()` — reads `PIKMIN_TRACE`.
  - `void trace_dump(int max_entries)` — async-safe, oldest first, via `diag_write`; format `  <tick> <channel> <name> a=<hex> b=<hex> <text>`.
  - `void trace_reset()` — tests only.
- Events: HLE_ENTRY → `trace(Ev::Hle, fname, lr)`; `ppc_poll` does **not** record (too hot). DVD: open → `(Dvd, "open", entrynum, 0, path)`; read → `(Dvd, "read", offset, length, <entry path or "">)`. `switch_to` → `(Thread, "switch", old->ea, t->ea)`. `run_queue` each callback → `(Irq, "callback")`. `hle_GXCopyDisp` → `(Frame, "copydisp", vi_retrace_count())`. Tick = `rt::clock().ticks()`.
- Scene: `"hook"` entries for `init__13TitlesSectionFv`, `init__14NinLogoSectionFv`, `init__15GameExitSectionFv`, `init__16GameSetupSectionFv`, `init__16IntroGameSectionFv`, `init__16MapSelectSectionFv`, `init__16MovSampleSectionFv`, `init__16OnePlayerSectionFv`, `init__17CardSelectSectionFv`, `init__18GameCreditsSectionFv`, `init__18NewPikiGameSectionFv`, `init__21GameStageClearSectionFv`, `init__22GameCourseClearSectionFv`, `initStage__15GameCoreSectionFv`. Each `hle_<name>` does `HLE_ENTRY`, `trace(Ev::Scene, "<Demangled>::init")`, always also `rt::log("scene", ...)` (cheap, rare, useful in every log), then calls `f_<addr>_orig` (weak-declared, addresses from `build/cmake/gen/report.txt`). Use one macro for the 14 bodies.

- [ ] **Step 1: Write failing tests** in `tests/runtime/test_trace.cpp`:
  - `ring_wraps_oldest_first`: `trace_reset()`; record 5000 `Ev::Hle` with `a = i`; capture dump (redirect via a test hook `trace_dump_to(int fd, int max)` writing to a pipe); first line has `a=0x000003a8` (5000-4096 = 904), last `a=0x00001387`; 4096 lines.
  - `ring_copies_text`: record with a stack buffer `"dvd/path.bin"`, overwrite the buffer, dump contains `dvd/path.bin`.
  - `parse_channels`: `"hle,dvd"` → bits Hle|Dvd; `"all"` → all six; `"bogus,irq"` → Irq; `""`/null → 0.
- [ ] **Step 2:** build + `ctest -R test_trace` → fails.
- [ ] **Step 3: Implement** `trace.{hpp,cpp}`; wire `trace_init()` in `main.cpp` after `log_open`. Change `trace_dump(int max)` to call `trace_dump_to(-1, max)` meaning "diag_write".
- [ ] **Step 4: Instrument** the call sites listed above, add the 14 hooks + `sections.cpp`.
- [ ] **Step 5: Live channel check:** `PIKMIN_TRACE=scene,dvd build/cmake/pikmin --headless --fast --frames 400 2>&1 | grep -c '\[trace\] dvd'` → > 0, and `grep '\[scene\]'` shows `NinLogoSection::init` then `TitlesSection::init`.
- [ ] **Step 6: Performance check:** real-time window run with `PIKMIN_TIMING=1` for ~20 s at the title; fps unchanged vs. before Task 2 (within noise; note both numbers in the commit message).
- [ ] **Step 7:** `ctest` + `pytest -q` pass. **Commit** `feat(diag): recent-events ring buffer and PIKMIN_TRACE`.

### Task 3: Crash handler and unified fatal report

**Files:**
- Create: `runtime/rt/crash.hpp`, `runtime/rt/crash.cpp`
- Modify: `runtime/rt/hooks.cpp` (`ppc_bad_access`, `ppc_unimpl`, `ppc_hle_missing`), `runtime/main.cpp` (watchdog, `--crash-test`, install), `runtime/hle/vi.cpp` (crash-test trigger), `runtime/CMakeLists.txt`
- Test: `tests/runtime/test_crash.py`

**Interfaces:**
- Consumes: `diag_write`, `fmt_*`, `log_path`, `trace_dump` (Tasks 1–2); `rt::sched().current()`, `rt::irq().in_callback()`, `rt::vi_retrace_count()`, `rt::current_function`.
- Produces (namespace `rt`):
  - `void crash_install()` — `sigaltstack` (64 KiB, static buffer) on the game thread, `sigaction` with `SA_SIGINFO | SA_ONSTACK | SA_RESETHAND` for the five signals.
  - `void crash_report(const char* reason, uint32_t guest_addr, const void* host_fault, void* const* frames, int nframes)` — async-safe, writes the report once (atomic `g_reported` flag; later calls return immediately).
  - `enum class CrashTest { None, Segv, BadAccess, Abort, DoubleFault, Hang };` and `void crash_test_arm(CrashTest kind, uint32_t at_retrace)`; `vi.cpp` calls `crash_test_check()` each retrace.
- Report layout (exact first line, tests grep it): `=== pikmin crash: <reason> ===`, then lines `build <hash> (<type>)`, `fault address <host ptr>` (signals) or `guest address 0x........` (bad access/unimpl), `retrace <n>`, `last HLE <name> lr 0x........`, `guest thread 0x........ prio <n>` (or `guest thread none`), `in IRQ callback: yes|no`, `lr 0x........ r1 0x........ r3 0x........ r4 0x........ r5 0x........` (from the current thread's `ctx`; the IRQ context when in a callback — expose `Irq::ctx()`), `backtrace:` + `backtrace_symbols_fd` lines, `recent events (last 256):` + `trace_dump(256)`, `log file <path>`, `=== end ===`.
- Signal handler: build the report from the `ucontext_t` (the faulting PC goes first in the backtrace frames), then `raise(sig)` (handler already reset by `SA_RESETHAND`). Call `backtrace()` once in `crash_install()` so libgcc is loaded before any signal.
- Fatal hooks: `crash_report(...)` then `std::abort()`; the SIGABRT handler sees `g_reported` and only re-raises.
- Watchdog: `on_watchdog_signal` calls `crash_report("watchdog: no retrace for N s", ...)` then `rt::exit_process(3)`; the watchdog thread's own pre-signal `fprintf` goes away (the report covers it).
- `--crash-test KIND[:RETRACE]` (undocumented in usage; KIND = `segv|bad_access|abort|double_fault|hang`, default retrace 30): `segv` writes through a null host pointer; `bad_access` calls `ppc_bad_access(0x7ffffff0)`; `abort` calls `std::abort()`; `double_fault` faults, and the report path faults again (a test hook in `crash_report` dereferences null when the kind is `DoubleFault`); `hang` spins in a host loop with no safe point.

- [ ] **Step 1: Write failing tests** in `tests/runtime/test_crash.py` (run headless `--fast`, `XDG_STATE_HOME=tmp`, `preexec_fn` sets `RLIMIT_CORE` to 0; skip without game data):
  - `test_segv_report`: `--crash-test segv`; returncode `-11`; stderr and the single log file both contain `=== pikmin crash: SIGSEGV`, `retrace 30`, `guest thread 0x8`, `backtrace:`, `recent events`, `[Hh]le` entries, `=== end ===`.
  - `test_bad_access_one_report`: `--crash-test bad_access`; returncode `-6`; exactly one `=== pikmin crash:` in stderr; contains `guest address 0x7ffffff0` and an `f_` frame.
  - `test_abort_report`: `--crash-test abort` → `-6`, `SIGABRT` report.
  - `test_double_fault_terminates`: `--crash-test double_fault` → terminates within the 60 s timeout with `-11`; stderr has the first report's header line.
  - `test_watchdog_report`: `--watchdog 1` plus `--crash-test hang` → returncode 3, `=== pikmin crash: watchdog`.
- [ ] **Step 2:** `pytest -q tests/runtime/test_crash.py` → fails.
- [ ] **Step 3: Implement** `crash.cpp`, rewrite the three hooks in `hooks.cpp` (drop `print_backtrace`), wire watchdog and `--crash-test` in `main.cpp`, `crash_install()` right after `log_open`.
- [ ] **Step 4:** tests pass; `pytest -q` + `ctest` all pass.
- [ ] **Step 5: Commit** `feat(diag): crash handler with guest context, backtrace and recent events`.

### Task 4: Release symbols and crash-log resolution

**Files:**
- Modify: `runtime/gen.cmake`, `runtime/CMakeLists.txt`, `tools/resolve_bt.py`
- Test: `tests/test_resolve_bt.py`

**Interfaces:**
- Consumes: report format from Task 3.
- Produces: `tools/resolve_bt.py LOG` — finds the **last** `=== pikmin crash:` block (falls back to the old `backtrace` marker for older logs) and prints one line per frame: `  f_<addr>  <name>  <object>` (object = the map's owning `.o`/library); also resolves `lr 0x...` and `last HLE ... lr 0x...` addresses to the containing function. `recomp/image.py` already exposes the function's object; if it doesn't, add `obj` to its function record.

- [ ] **Step 1: Write failing test** `tests/test_resolve_bt.py::test_resolves_crash_report` (skip without `orig/`): write a synthetic log with two crash blocks, the second containing `./pikmin(f_8004281c+0x10) [0x...]` and `lr 0x8002a560 r1 ...`; run the tool; output names `update__11CullFrustumFffff` and its object, the lr line names `update__11CamDataInfoFfR8Matrix4f`, and nothing from the first block.
- [ ] **Step 2:** run → fails. **Step 3:** implement. **Step 4:** passes.
- [ ] **Step 5: Release flags:** add `$<$<CONFIG:Release>:-g1>` to `pikmin_gen`, `pikmin_rt` and `pikmin` compile options (`-rdynamic` stays). Rebuild `build/release` (CLAUDE.md command), check `readelf -S build/release/pikmin | grep debug_line` is present and the binary size (record it in CLAUDE.md's "about 42 MB" line). Run `build/release/pikmin --headless --fast --crash-test bad_access` and confirm `f_` frames, then `tools/resolve_bt.py` on its log resolves them.
- [ ] **Step 6: Commit** `feat(diag): Release line tables; resolve_bt reads crash logs`.

### Task 5: Input recording for replay

**Files:**
- Create: `runtime/rt/input_record.hpp`, `runtime/rt/input_record.cpp`
- Modify: `runtime/hle/pad.cpp` (where the guest's 12-byte pad status for port 0 is produced), `runtime/main.cpp`, `runtime/rt/input_script.hpp/.cpp` (shared button-name table)
- Test: `tests/runtime/test_record_input.py`

**Interfaces:**
- Consumes: the button-name table and line format of `rt/input_script.cpp` (`FRAME BUTTONS [STICKX STICKY]`, `-` = none).
- Produces: `bool input_record_open(const std::string& path)`; `void input_record(uint32_t retrace, uint16_t buttons, int8_t sx, int8_t sy)` — writes a line only when the state differs from the last written one; buttons joined with `+` exactly as `input_script` parses them (export `std::string format_buttons(uint16_t)` from `input_script.cpp` next to its parser so both use one table). First line is a comment `# pikmin <hash> clock=<fast|real-time> config=<path>` (extend the script parser to skip `#` lines if it doesn't already). `fflush` after each line, so a crash keeps everything.
- The log header (Task 1) adds `input recording: <path>`; the crash report adds the same line.

- [ ] **Step 1: Write failing test** `test_record_roundtrip` (skip without data): run `--headless --fast --frames 800 --input-script <s4-style script written by the test: START, A, stick down, A> --record-input tmp/rec.txt`; then parse `rec.txt` and the original script into per-frame states and assert equal for every frame 0..800 (sticks included). Then replay `rec.txt` with `--record-input tmp/rec2.txt` and assert `rec2.txt` equals `rec.txt` apart from the comment line.
- [ ] **Step 2:** fails. **Step 3:** implement; hook `input_record` where `hle/pad.cpp` finalizes port 0 for a retrace. **Step 4:** passes; full `pytest -q` + `ctest`.
- [ ] **Step 5: Commit** `feat(diag): --record-input writes replayable input scripts`.

### Task 6: Docs

**Files:** Modify `CLAUDE.md`; Create `docs/superpowers/notes/crash-diagnostics.md`.

- [ ] **Step 1:** Replace "Planned: crash diagnostics" with a short "Crash diagnostics" section under "Debugging the game": log location and retention, report format, `PIKMIN_TRACE` channels, `--record-input`, `--crash-test`, `resolve_bt.py LOG`. Add `--record-input` to the usage string and the Commands block. Keep item 6 (triage policy) as one line.
- [ ] **Step 2:** The note records the non-obvious parts: why `SA_RESETHAND` + the `g_reported` flag, why `backtrace()` is primed at install, why `ppc_poll` isn't traced, the measured fps impact.
- [ ] **Step 3: Commit** `docs: crash diagnostics`.
