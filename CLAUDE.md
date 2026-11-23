# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A static recompilation of the GameCube game **Pikmin (PAL, game ID GPIP01)** into C, running natively on Linux. The PowerPC executable (`main.dol`) is translated function by function into C at build time. The Nintendo SDK libraries it calls are replaced by host code (`hle_*`), with graphics and input on [aurora](https://github.com/encounter/aurora) (GX on WebGPU/Dawn, SDL3).

- Design (goals, non-goals, approach): `docs/superpowers/specs/2026-11-22-pikmin-native-recomp-design.md`.
- Plans for the work done so far: `docs/superpowers/plans/`.
- Bring-up logs, which record non-obvious root causes: `docs/superpowers/notes/`.

**Game data never goes in git.** `/orig/` (the extracted disc), `/build/` (including generated C) and `/ghidra/` are gitignored. The disc image lives outside the repo (`~/Downloads/GPIP01/game.iso`). Tests that need game data skip cleanly when it's missing.

## Commands

```sh
# One-time: extract the disc (gives orig/GPIP01/sys/main.dol, sys/paths.txt and files/dataDir/build.map)
python3 tools/extract_gcm.py ~/Downloads/GPIP01/game.iso orig/GPIP01

# Configure and build (the first configure fetches aurora + Dawn into build/cmake/_deps;
# the recompiler runs automatically when the DOL, the map, overrides.toml or recomp/*.py change)
cmake -S . -B build/cmake -G Ninja
cmake --build build/cmake

# Tests
pytest -q                                          # recompiler, semantics harness, runtime/CLI/boot gates
pytest tests/sem/test_flow.py -q -k jump_table     # a single Python test
ctest --test-dir build/cmake --output-on-failure   # C/C++ suites (runtime units, GPU readback tests)
ctest --test-dir build/cmake -R test_sched         # a single C/C++ suite

# Run the game (fullscreen by default; config in ~/.config/pikminrecomp/config.toml)
build/cmake/pikmin                                  # data from orig/GPIP01; --disc ISO|DIR overrides
build/cmake/pikmin --disc ISO --headless --fast --frames 600            # no GPU, virtual clock
build/cmake/pikmin --disc ISO --offscreen --fast --frames 750 \
    --input-script script.txt --screenshot 700:build/shot.ppm          # real GPU, no window
build/cmake/pikmin --record-input build/rec.txt                        # replay later with --input-script

# Regenerate the C by hand / inspect the recompiler report
python3 -m recomp --dol orig/GPIP01/sys/main.dol --map orig/GPIP01/files/dataDir/build.map \
    --overrides recomp/overrides.toml --out build/cmake/gen   # see build/cmake/gen/report.txt
```

- **Python** tests need `powerpc64-linux-gnu-as` (dnf: `binutils-powerpc64-linux-gnu`). The semantics harness in `tests/sem/harness.py` assembles PPC snippets, translates them, compiles them with `cc` and checks the register state.
- **GPU tests** (`tests/gfx`) use an offscreen aurora surface and pixel readback, and exit 77 (skip) when there's no Vulkan adapter.

## Architecture

### Build-time pipeline: `recomp/`
1. `image.py` parses the DOL and the CodeWarrior link map.
   - The map gives every function's address, size, name and owning library/object, plus `(entry of X)` secondary entry points.
   - That makes function boundaries exact, with no heuristics.
2. `decode.py` decodes Gekko instructions, paired singles included.
3. `translate.py` + `emit/*` emit one C function per guest function, `void f_<addr>(ppc_ctx *c)`.
   - Branches inside a function become `goto`s.
   - Calls become direct C calls. Self-recursion is a real call. Only `bl` to the next instruction (the PC-read idiom) becomes a goto.
   - Jump tables are resolved to `switch`es. Indirect calls go through `ppc_lookup(addr)`.
4. `program.py` decides overrides from `recomp/overrides.toml`.
   - `[libraries] override` lists whole SDK libraries.
   - `[functions]` lists per-function exceptions: `"override"`, `"translate"`, `"poll"` (translated, with safe points at entry and loop heads) or `"hook"` (translated as `f_<addr>_orig`; every call goes to host `hle_<name>`, which may call the original; used by `runtime/enhance/`).
   - Overridden functions become calls to `hle_<name>`, with **weak** abort stubs in `hle_stubs.c`.
   - Output: 64 shard files, a sorted function table and `report.txt`.
- Generated code must be compiled with `-ffp-contract=off` (no FMA fusion) and `-fwrapv -fno-strict-aliasing`.

### Guest CPU model: `runtime/cpu/`
- `ppc_ctx` holds the guest registers: GPRs, FPRs as paired doubles, CR, XER, GQRs.
- `memory.h` handles guest memory:
  - 24 MB big-endian RAM at `0x80000000` (with the `0xC0000000` uncached mirror),
  - the 16 KB locked cache at `0xE0000000`,
  - everything at `phys >= 0x0C000000` going to `ppc_mmio_read/write`.
- The small-data bases: r13 = `0x803e9c60`, r2 = `0x803f5120`.

### Host runtime: `runtime/rt` + `runtime/hle`
- **Linking:** `pikmin` links `pikmin_rt` as a **whole archive**, so its strong `hle_*` definitions always beat the generated weak stubs. A missing implementation shows up at runtime as `ppc_hle_missing`. `tests/runtime/test_cli.py` checks the GX shims are strong.
- **Threads:** guest threads are `ucontext` fibers on one host thread (`rt/sched.cpp`). They follow SDK priority semantics (0 highest, preemption on wake), and an `OSThread` struct re-used by a new thread retires the old entry.
- **Interrupts:** guest "interrupts" (DVD/ARQ completion, retrace and alarm callbacks) are queued and delivered at **safe points** (`rt/irq.cpp`).
  - Every HLE function starts with `HLE_ENTRY(c, "Name")`, which is a safe point.
  - Callbacks run on a separate context with interrupts disabled. Interrupt-enable state is per thread.
  - Guest busy-wait loops with no OS call can't receive callbacks. Fix those without changing what game code computes, in one of two ways:
    - complete the awaited event sooner (as `ARQPostRequest` does), or
    - mark the function `"poll"` in `overrides.toml`, which keeps it translated but makes its entry and loop heads scheduling points (`ppc_poll`), the chance an interrupt would get on hardware. JAudio uses this for 13 functions.
- **Time:** `rt/clock.cpp` provides the 40.5 MHz timebase. Real mode follows the host clock; `--fast` mode is a virtual clock that jumps to the next event when all threads sleep.
- **HLE conventions:**
  - Arguments come from `rt::arg(c, i)` (r3…) and `rt::farg` (f1…); return with `rt::ret`.
  - Guest structs are big-endian at SDK offsets.
  - A `GXColor` argument arrives as a *pointer to a copy*.
  - Guest addresses become host pointers through `mem_host`/`rt::host`.
- **Disc:** `rt/disc.cpp` reads plain ISO/GCM images directly. aurora's DVD layer is not used, because it works on host structs.
- **Saves:** the memory card (`hle/card.cpp`) is file-backed in `$XDG_DATA_HOME/pikminrecomp/card_a`.
- **Graphics** (`rt/gfx.cpp`, `hle/gx/*`):
  - GX calls are marshalled into aurora's PC GX API.
  - Write-gather-pipe stores (`0xCC008000`, from inline `GXPosition*` macros in game code) go byte for byte into aurora's FIFO (`rt/mmio.cpp`).
  - An open `GXBegin` is closed at the next GX call; on the GameCube `GXEnd` is an empty inline.
  - Frames are presented at **`GXCopyDisp`**, not at retrace. Presenting at retrace shows half-drawn frames.
  - aurora's `emulateXfb` is **off**, because it presents black on real windows; offscreen tests never exercise that path. `PIKMIN_XFB=1` re-enables it.
  - aurora's PC `GXTexObj` is 64 bytes against the GameCube's 32, so texture objects use host-side shadows keyed by guest address.
- **Audio:** JAudio runs as translated code.
  - `hle/dsp.cpp` emulates the DSP mailbox and raises JAudio's own `__DSPHandler` as an interrupt.
  - `hle/dsp/zelda.*` is a port of Dolphin's `UCode_Zelda`, which does the mixing, ADPCM decoding from ARAM and reverb. **This makes the project GPL-2.0-or-later** (see `NOTICE`).
  - `hle/ai.cpp` runs AI DMA as 32029 Hz clock events.
  - `rt/audio_out.cpp` streams to SDL3, only in real-time non-headless runs; config `audio = false` turns it off.
  - `DVDReadPrio` blocks its thread for about 0.5 ms of guest time. JAudio's load callbacks depend on that ordering.
- **Input:** `rt/pad_layout.cpp` sets the default keyboard and gamepad (DualShock 4) layouts. `hle/pad.cpp` converts aurora's `PADStatus` to the guest's 12-byte layout. `--input-script` drives port 0 deterministically for tests.

### Enhancements: `runtime/enhance/`
- Game functions are patched through `"hook"` entries in `overrides.toml`, never by editing generated code.
- **Widescreen (Hor+):** 4:3 cameras get the window aspect, with the same vertical FOV, for both projection and frustum culling. 2D/ortho drawing is pillarboxed to 4:3, except full-screen fades, screen-copy effects and creature life gauges. Cinematic cameras (`CamDataInfo`: title screen, cutscenes) are widened too. The title and memory-card/file-select screens are also scissored to the 4:3 area, because they park panes just off the 640×480 screen.
- **HUD:** gameplay HUD clusters shrink toward the edge or corner they touch (`hud_scale`, default 0.5).
- **Frame rate:** the game loop runs at the display refresh (240 fps with vsync). The frame time is scaled by 0.833 to match PAL gameplay speed (25 fps capped at 1/30 s). Cutscenes and camera smoothing are converted to original-frame units. Per-frame steps (particle emission `SetPtclsLife`, the menu cursor trail `SpectrumCursorMgr`) run once per elapsed original frame (`enh::orig_frame_steps`, shared so they stay in lockstep; `enhance/particles.cpp`). The 50 Hz retrace still drives input scripts and screenshots.
- **Boot logo:** in real-time runs the boot loads (`System::endLoading` hook) end no sooner than 3 s after the first one starts, so the red Nintendo logo stays up about as long as it did from a real disc. `--fast` keeps the original timeline.
- **Camera:** right stick orbits and tilts (elevation 8–82°); L1 + right stick swarms; keyboard arrows = camera, Left Shift + arrows = swarm.
- **In-game settings and quit** (no ImGui; drawn by the game's own `zen::DrawMenu`):
  - Title menu: Start / Options / (Challenge Mode) / **Quit Game**. Options adds **Display** (Screen, Aspect, V-sync) and **Game** (FPS, Camera, HUD size). Pause menu adds **Options** (Sound, Rumble, Display, Game) and **Quit Game**. Quit asks "Quit Pikmin?" (No by default) and waits for an open memory-card file.
  - Screens are made at boot in memory (`enhance/menu_entries.cpp`, `enhance/settings_screens.cpp`, `rt::blo` edits, `rt::arc::add`): patched `m_select`/`m_selec2`/`pause`/`option.blo` and new `x_disp`/`x_game` (from the English Sound screen), `x_popt`, `x_quit` in every `blo__<lang>.arc`.
  - `enhance/settings_menu.cpp` hooks `ogScrTitleMgr` (ctor/update; draw via `enh::title_draw` from the widescreen hook) and `ogScrPauseMgr` (ctor/update/draw). The `DrawMenu::update` hook hides added items from the game (`enh::take_menu_choice`). Values apply live and changed keys are written to `config.toml` (`ui/settings.cpp`, `rt::save_config_value`).
  - **F11** toggles fullscreen (saved). **Esc** and the DS4 touchpad act as Start.
- **Debug HUD (F3):** ImGui corner panel (`ui/debug_hud.cpp`, drawn through `rt::gfx_set_hud`): fps / ms (0.5 s), 1% low (4 s), frame-time graph (drops red), game/present/pacing breakdown, CPU (`/proc`), GPU name/backend/busy % (Dawn adapter; NVML via dlopen or amdgpu sysfs, `rt/sysinfo`), last drop and cause. Frame records in `rt/perf` (host steady clock only). A drop is a frame over 2× max(target, 0.5 s average); each is logged as `[perf] drop N ms at retrace R: cause`. State saved as `debug_hud`; `--debug-hud` shows it for one run.
- **Config / CLI:** `aspect` (`auto`|`4:3`), `target_fps` (`auto`|`original`|N), `hud_scale`, `camera` (`modern`|`original`); flags `--aspect --target-fps --hud-scale --camera`. `--headless` defaults to original timing. `PIKMIN_TIMING=1`, `PIKMIN_HUD_DUMP=1`.
- **Clock:** safe points read `CLOCK_MONOTONIC_COARSE` and the precise clock only when an event is within 5 ms. This laptop's clock source is HPET, which made precise reads cap the loop at about 85 fps.

### aurora
- Fetched at a pinned commit by `extern/aurora.cmake` and patched with `extern/aurora-patches/*.patch`.
  - The hash of the patches' contents is part of the FetchContent declaration, so editing a patch re-applies them on the next configure.
- Patch 0001 makes `GXSetArray(..., size=0)` mean "unknown". Each draw's indices are scanned, and the upload extent grows geometrically. `AuroraSetUnknownArrayLimit` bounds it to guest RAM.
- The GX logical framebuffer is 640×480. Readback (`AuroraReadbackCaptureColor`) reads the EFB of the frame in progress.

## Debugging the game

- **Crash reports.** Fatal signals (`SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`), `ppc_bad_access` / `ppc_unimpl` / `ppc_hle_missing` and the **watchdog** (no retrace for 5 s) all write one report (`rt/crash.cpp`) to stderr and the run log, then re-raise so a core is still dumped:
  - it is framed by `=== pikmin crash: <reason> ===` … `=== end ===`;
  - it contains the build, fault or guest address, retrace count, last HLE call, current guest thread with its `lr`/`r1`/`r3`–`r5` (or the IRQ context), and the host backtrace (`f_<guest addr>` frames);
  - it ends with the last 256 recent events.
  - `python3 tools/resolve_bt.py LOG` maps the last report's frames and `lr` to guest function names and objects. It works on Release builds too (`-g1`).
- **Run logs:** every run writes `$XDG_STATE_HOME/pikminrecomp/logs/pikmin-<time>-<pid>.log` (default `~/.local/state`; the last 10 are kept). The header records the git hash, build type, command line, config, mode, clock, GPU adapter and display mode. All `rt::log` output and the guest's `OSReport` text go there too.
- **Recent events:** a 4096-entry in-memory ring (`rt/trace.hpp`) records HLE entries, DVD opens and reads, thread switches, IRQ callbacks, display copies and scene changes (section-init hooks in `hle/sections.cpp`; also logged as `[scene]`). `PIKMIN_TRACE=hle,dvd,thread,irq,frame,scene` (or `all`) prints channels live. Entries carry the retrace count, not clock ticks: reading the clock advances virtual time in `--fast` mode.
- **Replaying:** `--record-input PATH` writes port-0 input per retrace in `--input-script` format (a line per change, flushed at once), so a `--fast` crash can be replayed exactly. Real-time runs aren't fully deterministic; the recording's header names the clock mode.
- **Triage:** group crashes by their top guest frames; fix each root cause in the HLE/runtime with a note in `docs/superpowers/notes/`, never in generated code.
- **Testing the crash paths:** `--crash-test segv|bad_access|abort|double_fault|hang[:RETRACE]` (default retrace 30).
- **Decompile guest code** by name with Ghidra. The project is `ghidra/PikminGPIP01.gpr`, built by `tools/make_ghidra_project.sh` with the Gekko language and r13/r2/GQR0 preset:
  ```sh
  JAVA_HOME=/usr/lib/jvm/java-25-openjdk /opt/ghidra/support/analyzeHeadless ghidra PikminGPIP01 \
      -process main.elf -noanalysis -readOnly -scriptPath tools/ghidra -postScript Decompile.java <name>...
  ```
- **`tools/peek.sh SCRIPT FRAME`** runs offscreen with an input script and writes `build/peek_FRAME.png`, using its own data dir.
  - Input script lines are `FRAME BUTTONS [STICKX STICKY]`, e.g. `300 START`, `303 -`, `660 - 0 127`.
  - Menus follow the analog stick, not the D-pad.
  - Screenshots are keyed to the retrace count.
- **Diagnostic env vars:** `PIKMIN_GFX_STATS=1` (per-frame aurora stats), `PIKMIN_GFX_TRACE=1` (display-copy calls), `PIKMIN_ARRAY_DEBUG=1` (large vertex-array uploads), `PIKMIN_TEST_LOG=1` (aurora info logs in GPU tests), `PIKMIN_AUDIO_DUMP=out.wav` (mixed audio in any mode), `PIKMIN_AUDIO_DEBUG=1`, `PIKMIN_POLL_TRACE=1`.
- **Real-window output** can differ from offscreen output. Check it with `spectacle -b -n -a -o out.png` while the game runs.

## Installation and Steam

- **Install rules** are in `runtime/gen.cmake`: the `pikmin` binary, `packaging/pikminrecomp.desktop.in` (configured with an absolute `Exec` path), and the original icon (`packaging/pikminrecomp.svg`, plus a 256 px PNG).
- **Installed per user** to `~/.local`. This is the one user-approved exception to "nothing in `$HOME`".
  ```sh
  # Release build in its own directory, reusing the aurora/Dawn sources fetched by build/cmake
  D=$PWD/build/cmake/_deps
  cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local \
      -DFETCHCONTENT_FULLY_DISCONNECTED=ON $(for n in aurora dawn imgui nlohmann_json sqlite3 tracy xxhash; do \
      echo -DFETCHCONTENT_SOURCE_DIR_$(echo $n | tr a-z A-Z)=$D/$n-src; done)
  cmake --build build/release --target pikmin && cmake --install build/release
  ```
  The installed copy is the Release build (about 69 MB, with `-g1` line tables for crash reports); `build/cmake` stays the Debug dev build (about 595 MB, mostly debug info). Re-run the build and install after changes: the installed binary is a copy.
- **The launcher passes no arguments;** the game data comes from the installed tree (see below).
- **Steam:** a non-Steam shortcut "Pikmin Recomp" lives in `~/.local/share/Steam/userdata/476780297/config/shortcuts.vdf` (binary VDF; appid 3992718089). Edit it only while Steam is closed (check with `ps -eo comm`, not `pgrep -f`, which matches itself). Parse and append rather than overwrite if the file has other entries.

## Game data: no ISO at runtime

- **`main.dol` is baked into the binary.** `runtime/gen.cmake` writes `dol_blob.S`, which `.incbin`s `orig/GPIP01/sys/main.dol` (text included, so `rt::load_dol` is unchanged). `main.cpp` boots from `pikmin_dol_start/end`.
- **`rt::Disc` has two backends:** an ISO/GCM image, or an extracted tree (`sys/boot.bin`, `sys/fst.bin`, `sys/paths.txt`, `files/...`). The tree reuses the disc's FST for entry numbers and offsets. `sys/paths.txt` (line i = `files/`-relative path of entry i, from `tools/fst_paths.py`; `extract_gcm.py` writes it) handles the Shift-JIS file names. Reads outside files return zeros.
- **Lookup order:** `--disc PATH` (image or tree), then `disc = "..."` in config, then `<bindir>/../share/pikminrecomp/data` (installed), then `orig/GPIP01` in the checkout. So `build/cmake/pikmin` and the installed `pikmin` both run with no arguments.
- **`cmake --install`** copies the tree (about 655 MB) to `${CMAKE_INSTALL_DATADIR}/pikminrecomp/data`. The binary and the data are Nintendo's: keep them out of git and never distribute them.

## Windows build (cross-compiled from Linux)

`pikmin.exe` is built on this machine with Fedora's MinGW-w64 GCC; Linux stays the primary platform.
Design: `docs/superpowers/specs/2026-11-23-windows-port-design.md`.

```sh
# once: sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static mingw64-vulkan-headers \
#           mingw64-vulkan-loader mingw64-libpng mingw64-zlib mingw64-zstd mingw64-freetype wine
D=$PWD/build/cmake/_deps
cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
    $(for n in aurora dawn imgui nlohmann_json sqlite3 tracy xxhash; do echo -DFETCHCONTENT_SOURCE_DIR_$(echo $n | tr a-z A-Z)=$D/$n-src; done)
cmake --build build/win --target pikmin            # also copies the DLLs it needs next to it
cmake --install build/win --prefix DIR              # DIR/pikmin.exe + DLLs + DIR/data (Nintendo data: never distribute)
WINEPREFIX=... wine build/win/pikmin.exe --headless --fast --frames 600 --disc Z:/path/game.iso   # smoke test
```
- **Graphics:** Dawn's Vulkan backend only (`extern/aurora.cmake`: D3D11/D3D12 and Windows UI surfaces need
  Windows SDK headers MinGW lacks). `extern/mingw-shim/` maps MSVC header spellings (`Windows.h`, `Wbemidl.h`);
  aurora's MSVC link names (`DXGI.lib`, `comsuppw.lib`) are rewritten after it is added. fmt is fetched (no MinGW package).
- **Weak symbols:** COFF has no ELF-style weak override, so on Windows `pikmin_hle_stubs` is built from
  `tools/strong_hle_stubs.py` (the generated stubs minus every `hle_*` the runtime defines, made strong), and the
  runtime's default hooks use `PIKMIN_WEAK` (`rt/weak.hpp`, empty on Windows).
- **OS layer:** `rt/fiber.hpp` (ucontext / Win32 fibers), `rt/platform.hpp` (files with `pread` via `ReadFile`
  offsets and UTF-16 paths, dirs, pid/tid, exe path, `dlopen`/`LoadLibrary`, user dirs: `%APPDATA%\pikminrecomp`
  for config/saves, `%LOCALAPPDATA%\pikminrecomp\logs`). Crash reports use an unhandled-exception filter and
  `CaptureStackBackTrace` (raw addresses + module base); the watchdog reports from its own thread. CPU % shows n/a.
- **Verified here:** headless `--fast` boot identical to Linux (600 frames); offscreen title screenshot under Wine
  (Vulkan → host GPU); folder install boots with no arguments. **Not verified:** a real Windows machine (window,
  input, audio, fullscreen).

## Launcher (`launcher/`)

The only published artifact: a Qt 6/QML app (Forest "liquid glass" UI) that self-updates from GitHub
releases of `noalexan/PikminRecomp`, checks the toolchain and the disc (`GPIP01`, SHA-1 of `main.dol`
and `fst.bin` in `src/core/rom.hpp`), downloads the source of its own version (`v<ver>` tag), extracts
the disc, builds and installs the game into `~/.local/share/pikminrecomp-launcher/` (`%LOCALAPPDATA%`),
and plays it. Design: `docs/superpowers/specs/2026-11-23-launcher-design.md`.

```sh
cmake -S launcher -B build/launcher -G Ninja && cmake --build build/launcher && build/launcher/core_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl \
    build/launcher/pikmin-launcher --screenshot out.png --preview ready|building    # look, no network/build
PIKMIN_LAUNCHER_SOURCE=$PWD build/launcher/pikmin-launcher --auto-build ISO     # build this checkout, quit
launcher/packaging/windows.sh 0.1.0     # -> build/PikminRecompLauncher-0.1.0-windows.zip (+ sha256 line)
launcher/packaging/appimage.sh 0.1.0    # -> ...-x86_64.AppImage (needs linuxdeploy + qt plugin in build/tools)
```
Releases: tag `vX.Y.Z`, attach both files, and put their `sha256 <file> <hex>` lines in the release
notes (the launcher verifies them when self-updating). Offscreen screenshots need the OpenGL RHI
backend: the default software renderer skips MultiEffect (no blur).

## Conventions

- Install system dependencies with `dnf`. Anything else is project-local (`build/`) or root-owned in `/opt` (Ghidra). Never install into `$HOME`.
- The user works in nvim only, so don't add IDE project files.
- Every HLE function must call `HLE_ENTRY` first. Graphics shims use `GX_SHIM` instead, which also makes them inert in `--headless` mode.
- Exits from inside a guest fiber must use `rt::exit_process()` (it flushes and `_Exit`s). `std::exit` runs static destructors that unmap the fiber stack still in use.
