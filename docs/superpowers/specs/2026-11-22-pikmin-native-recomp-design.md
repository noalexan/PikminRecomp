# Pikmin native recompilation: design

Date: 2026-11-22
Status: approved 2026-11-22

## Intent

Play the GameCube Pikmin (PAL, game ID GPIP01) as a native Linux program on the
user's laptop, better than the original hardware allows: **widescreen, high
resolution and high framerate**. This is a personal project. The point is the
enhanced experience, not historical accuracy.

Approach: statically recompile the game's PowerPC code to C, and replace the
Nintendo SDK libraries with native implementations at function granularity.
Graphics, input, disc and save layers come from
[aurora](https://github.com/encounter/aurora).

## Goals

1. Boot the game from the user's own disc image and play it start to finish
   natively on Linux x86-64, with no emulator.
2. Render at any resolution, up to the laptop's native 2560x1600.
3. Widescreen at any aspect ratio, including 16:10 (the laptop panel) and 16:9.
   The 3D view gets wider, the HUD keeps its proportions and stays anchored
   correctly, and nothing pops in at the new screen edges.
4. High framerate: run game logic at the display's refresh rate (60/120/144),
   uncapped from the original 25/50 Hz PAL timing.
5. Working audio (music and SFX), controller and keyboard input, memory card
   saves and cutscene videos.

## Non-goals

- Matching (byte-identical) decompilation or readable source code.
- Bit-exact or frame-exact behaviour. The user's bar is **"feels right"**: rare
  quirks at high framerate are acceptable.
- Other regions or revisions (NTSC-U/J), the Wii "New Play Control" version,
  Windows/macOS builds. Nothing should rule them out, but no work is spent on
  them.
- Netplay, achievements, mod loaders, texture packs (aurora may give texture
  packs for free; that's a bonus, not a goal).

## Anti-goals (would count as failure even if it runs)

- Any game code, game data or recompiled output from the disc being committed,
  distributed or bundled. The program requires the user's own disc image at
  runtime. `orig/`, `build/`, `ghidra/` and generated C stay gitignored.
- High framerate that makes the game feel wrong in normal play: game speed
  changing, Pikmin behaving erratically, timers running fast. If a system can't
  run at a variable step, it gets fixed or locally clamped. It doesn't block the
  rest.
- Anything requiring root to build or run, or installing into `$HOME` outside
  the project (user preference: system packages via `dnf`, everything else
  project-local).

## Constraints and known facts

- **Target machine:** Fedora 45, Wayland, AMD Ryzen with Radeon 610M iGPU
  (RADV, Vulkan 1.4; a weak GPU, only 2 CUs), 32 threads, 30 GB RAM, 2560x1600
  16:10 panel. Performance budget: steady 60 fps at native resolution on this
  iGPU. Higher refresh is a stretch goal.
- **Toolchain:** cmake, ninja, gcc-c++, clang, SDL3-devel and vulkan-loader-devel
  are installed via dnf. The user edits with nvim only: no IDE project files.
- **Input artifacts** (already in the repo, all gitignored):
  - `orig/GPIP01/sys/main.dol`: the executable. Single DOL; no RELs.
  - `orig/GPIP01/files/dataDir/build.map`: full CodeWarrior link map shipped
    on the disc. Verified to match this DOL exactly. Gives every function's
    address, size, name and source object/library (~29k symbols).
  - `build/main.elf`: DOL + map as a symbolized ELF (`tools/dol2elf.py`).
  - `ghidra/PikminGPIP01.gpr`: Ghidra project with the Gekko/Broadway language,
    r13/r2/GQR0 preset. Readable through REA. A reference for analysis only,
    not part of the build.
  - Tools: `tools/extract_gcm.py`, `tools/dol2elf.py`,
    `tools/make_ghidra_project.sh`, `tools/ghidra/*.java`.
- **Code composition** (.text by library, from the map):
  - ~85% game and engine: `plugPiki{Kando,Yamashita,Nishimura,Nakata,Colin,Ogawa}.a`,
    `sysCommon.a`, `sysDolphin.a`.
  - ~4.6% `jaudio.o`: Nintendo's JAudio sound engine.
  - ~7% Nintendo SDK: `gx`, `os`, `dvd`, `card`, `pad`, `vi`, `si`, `exi`,
    `ar`/`arq`, `mtx`, ...
  - The rest: `MSL_C`, `Runtime.PPCEABI` (C/C++ runtime), `hvqm4dec.a`
    (cutscene video decoder), `TRK_MINNOW_DOLPHIN`/`amcExi2`/`OdemuExi2`
    (debugger stubs, unused at runtime).
- **Frame timing:** the engine exposes a per-frame delta time
  (`System::getFrameTime()` returns the float at `gsys+0x290`). That suggests a
  variable timestep in the game logic, but it is **unverified** how widely it's
  honoured.
- **Small-data registers:** r13 = `_SDA_BASE_` 0x803e9c60, r2 = `_SDA2_BASE_`
  0x803f5120.
- **Licensing:** aurora is MIT. If code is ported from Dolphin (GPL-2.0+), e.g.
  for audio DSP HLE, the project becomes GPL. Acceptable for a personal
  project, but note it in the README when it happens.

## Approaches considered

1. **Recompile game code + replace the SDK with native code (chosen).** Game,
   engine and audio-engine code is translated PPC→C. SDK functions are
   intercepted by symbol and implemented natively, mostly on aurora. Rendering
   is native, so resolution and widescreen are first-class, and enhancement
   patches are C at named functions.
2. Recompile everything including the SDK, and emulate the hardware below it
   (GX command FIFO, DSP, VI registers), Dolphin-style. More faithful, but much
   more work, and resolution/widescreen happen through an emulated GPU instead
   of API calls. Rejected: it works against the enhancement goals.
3. Dolphin + patch codes (widescreen/60 fps codes written using the map).
   Cheapest, but not native. Kept only as a fallback if the recomp stalls.

The user picked approach 1. For high framerate, the user picked **running the
game logic at the display rate with a smaller delta** over render-only
interpolation. Interpolation stays available as a local fallback for systems
that misbehave.

## Design

### Repository layout (proposal)

```
recomp/              PPC→C recompiler (Python, run at build time)
runtime/             C++20 host runtime
  cpu/               guest context, memory, helper ops, function table
  hle/               SDK replacements: os, dvd, card, pad, vi, gx shims, ar, audio
  enhance/           widescreen, framerate, resolution patches
  main.cpp
extern/aurora/       git submodule
tools/               existing extraction/analysis tools
build/               generated C and binaries (gitignored)
```

Build: CMake + Ninja. The recompiler runs as a CMake custom command, input
`build/main.elf`, output `build/gen/*.c`. Generated code is split into many
translation units (e.g. one per original source object from the map) so builds
parallelise across the 32 threads and rebuilds stay incremental.

### 1. Recompiler (`recomp/`)

- **Input:** `build/main.elf` (bytes + full symbols). Function boundaries,
  sizes and owning objects come from the map, not from heuristics.
- **Output:** one C function per guest function,
  `void f_80xxxxxx_Name(Ctx *c)`, operating on a guest CPU context:
  - GPR[32], FPR[32] as paired doubles (ps0/ps1), CR (as 8 fields), LR, CTR,
    XER (CA/SO/OV), GQR[8], FPSCR (rounding mode and the bits the game
    actually reads; not full exception emulation), MSR stub.
- **Instruction coverage:** full Gekko user-mode integer, FP and paired-single
  sets, including `psq_l`/`psq_st` with GQR-driven (de)quantization.
  Supervisor and cache instructions (`mtmsr`, `dcbi`, `icbi`, `mtspr` to HID
  etc.) become no-ops or runtime hooks; they live almost entirely in the SDK,
  which is replaced.
- **Control flow:**
  - Intra-function branches become `goto` labels.
  - Jump tables (`bctr` after a table load) are resolved statically from
    `.data`/`.rodata` into `switch`. An unresolved `bctr` falls back to the
    dispatch table.
  - Direct `bl` becomes a direct C call. `b` to another function's entry
    (a tail call) becomes call+return.
  - `blr` returns. Indirect calls (`bctrl`, `blrl`: vtables, function pointers,
    callbacks) look up guest address → host function in a generated sorted
    table (`lookup_func(addr)`); a miss is a fatal error that names the address.
- **Memory access:** guest RAM is one host buffer (24 MB main RAM; ARAM
  handled separately, see HLE). Loads and stores byteswap (the guest is
  big-endian). Virtual address → offset by masking (`0x80000000` cached and
  `0xC0000000` uncached mirrors both map to physical). Accesses to the hardware
  register range `0xCC000000+` are routed to an MMIO handler. See the GX FIFO
  note below: this path is required, not hypothetical.
- **Overrides:** a config file (`recomp/overrides.toml`) lists guest functions
  that are *not* translated but bound to a host implementation (the SDK, plus
  enhancement patches). The recompiler emits calls to the host symbol instead.
  Overrides are selectable per function or per library (e.g. all of `gx.a`).
- **Correctness tooling:** an instruction-semantics test suite (small hand
  written PPC sequences with expected results, plus randomized tests against a
  reference such as a Dolphin interpreter run, or a tiny PPC interpreter written
  for the purpose). The builder chooses the reference.

### 2. Runtime (`runtime/`)

- **Startup:** parse args/config, open the disc image (path from config or CLI;
  `nod` through aurora supports ISO/RVZ/etc.), load the DOL sections into guest
  RAM, zero BSS, set up r1/r2/r13, initialise the OS globals block at
  `0x80000000` (BootInfo: memory size, console type, arena bounds, bus/CPU
  clocks) that the game reads, then call the translated `main` path. Skip
  `__start`/`__init_hardware` and run the parts that matter: static
  constructors (`.ctors`), `__init_user`.
- **Threads:** the game uses OS threads (`OSCreateThread`: DVD loading, audio,
  etc.). Guest threads map to **host fibers/coroutines on one host thread**,
  scheduled by priority with switches at OS calls (`OSSleepThread`,
  `OSWaitCond`, message queues, `OSYieldThread`). That matches the original
  single-core cooperative-by-priority model and avoids data races in translated
  code. Each guest thread has its own `Ctx` and guest stack. (Builder may
  revisit if a pre-emption-dependent pattern shows up.)
- **Interrupts and callbacks** (VI retrace callbacks, DVD/ARAM completion,
  alarms via `OSSetAlarm`) are delivered at safe points: frame boundaries and
  OS calls. They call translated code through `lookup_func`.

### 3. SDK replacement (`runtime/hle/`)

Each overridden SDK function reads its arguments from `Ctx` per PPC EABI (r3–r10,
f1–f8, return in r3/f1) and converts guest pointers to host pointers.

| Library | Replacement |
|---|---|
| `gx.a` | aurora GX. Pointer arguments translated; arrays and display lists point into guest RAM. |
| `vi.a` | aurora VI + frame pacing (see Framerate). |
| `pad.a`, `si.a` | aurora PAD (SDL gamepads, GC adapter, keyboard). |
| `dvd.a` | aurora DVD (nod) over the user's disc image; async reads complete via callbacks. |
| `card.a` | aurora CARD: Dolphin-compatible `.gci`/`.raw` saves under `$XDG_DATA_HOME/pikminrecomp/`. |
| `os.a` | Host: threads, mutex/cond/message queues, alarms, time base, arena/heap, interrupts (no-op), cache ops (no-op), `OSReport` → stderr, `OSPanic` → abort with message. |
| `ar.a`, `arq.a` | ARAM as a separate 16 MB host buffer; DMA = memcpy plus a completion callback. |
| `mtx.a` | Native C (or keep translated; it's pure math with paired singles). |
| `exi.a`, debugger libs | Stubbed. |
| `MSL_C`, `Runtime.PPCEABI` | Translated as normal code (simplest), with selective overrides (`memcpy`, `memset`, `sqrt`...) only if profiling says so. |

**GX inline FIFO writes (key detail):** GX vertex submission
(`GXPosition3f32`, `GXColor4u8`, `GXTexCoord2f32`, …) is a set of *inline
macros* in the SDK headers. They compile into stores to the write-gather pipe at
`0xCC008000` **inside game code**, so overriding `gx.a` doesn't catch them. The
MMIO handler must collect WGPIPE writes and feed them to aurora as a GX command
stream, either by decoding them against the current vertex descriptor into
aurora's immediate-mode calls, or by passing the byte stream to aurora's FIFO /
display-list parser if one is exposed. Similarly, functions like
`GXCallDisplayList` receive big-endian display lists from game files.

**Endianness at the aurora boundary (risk, verify first):** aurora is used by
source ports that load original big-endian assets (display lists, textures,
vertex arrays). The builder must confirm early, with a small spike, which aurora
inputs expect big-endian data in memory (guest RAM is big-endian), and which
expect native structures that the shims must convert (e.g. `GXColor`,
`Mtx` passed by pointer, `GXTexObj` initialisation from a guest pointer to
texel data).

### 4. Audio

JAudio (`jaudio.o`) is translated as normal code. JAudio drives the audio DSP
with its own microcode (the "Zelda"-family ucode that Dolphin HLEs as
`UCode_Zelda`, which supports Pikmin). The runtime implements the DSP mailbox
and the ARAM/DSP interface and **HLEs that ucode natively**, mixing into an
SDL3 audio stream. Dolphin's `UCode_Zelda` is the reference implementation
(see the licensing note). The audio DMA rate is the original 32 kHz; resample
in SDL.

Audio is milestone-gated (it's the most self-contained hard part), so the game
must be able to run with audio stubbed (DSP mailbox answers as if idle).

### 5. Cutscene video

`hvqm4dec.a` (HVQM4 decoder) is translated as normal code. Its output frames
go to the screen through whatever GX path the game already uses (textured quad)
and need no special handling beyond a correct translation.

### 6. Enhancements (`runtime/enhance/`)

- **Resolution:** native via aurora's resolution scaling. Internal render
  resolution is configurable, defaulting to the window size. EFB copies (render
  to texture, e.g. for effects) must scale consistently. This is aurora's
  concern, but needs testing for this game's effects.
- **Widescreen:**
  - Window aspect is free (16:10 and 16:9 must both work).
  - 3D: patch the game's projection setup (camera/perspective functions found
    via the map, e.g. `Graphics`/camera classes in `sysDolphin`/`plugPiki`) to
    widen the horizontal FOV at constant vertical FOV (Hor+).
  - Culling: patch frustum/visibility tests that assume 4:3, so objects at
    the new side edges don't pop.
  - 2D/HUD: the 640x480 ortho space keeps its aspect and is centred, with HUD
    elements anchored to screen edges where they were edge-anchored (counters,
    map, menus) via per-element patches. Fullscreen 2D (title, menus, movies) is
    pillarboxed unless an element is explicitly made wide.
- **Framerate (decided: logic at display rate):**
  - Replace VI retrace pacing with host frame pacing (vsync'd present, or a
    configured cap).
  - Feed the measured frame delta into the engine's delta time
    (`gsys+0x290` / wherever it's set, found in `System`), clamped to a sane
    range (e.g. ≤ 1/20 s) to prevent big steps after a hitch.
  - Audit systems that count frames instead of using delta (timers, animation
    frame stepping, AI ticks, particle emitters, day-length clock, camera
    smoothing). Fix per system: scale by delta, or run that subsystem on a
    fixed-rate accumulator. Last resort is render interpolation for that system.
  - Config: target FPS (default = display refresh), and a "original 25/50 Hz"
    mode for comparison.
- **PAL timing:** GPIP01 assumes 50 Hz video timing (and may have 50 Hz-scaled
  constants). Once delta is decoupled, the target is real-time speed equal to
  the original game, verified against Dolphin side by side.

### 7. Configuration and UX

- Config file at `$XDG_CONFIG_HOME/pikminrecomp/config.toml`: disc image path,
  resolution scale, target FPS, aspect mode (auto/4:3), fullscreen, controller
  bindings (aurora persists those itself), and game language. The PAL disc picks
  its language from the console's SRAM settings, which the runtime fakes;
  default English. *(Builder's call: English default.)*
- Input defaults: SDL gamepad mapping out of the box, plus a full keyboard
  layout so the game is playable without a controller. *(Builder's call.)*
- First run without a disc path: print a clear message (or show a minimal
  aurora/ImGui prompt) saying where to set it. Never search the filesystem.
- A debug overlay (aurora's Dear ImGui) for FPS, frame time, current game
  scene and toggles for enhancements.

## Milestones (proposed order)

0. **Spikes:** (a) aurora endianness/inputs check with one known display list
   from the disc; (b) recompile + run a handful of pure functions (MSL `strlen`,
   `mtx` math) against expected results.
1. **Recompiler complete:** all non-overridden functions translate and the
   generated C compiles and links with stub overrides.
2. **Boots:** runs through init and the first `OSReport` output, with threads
   and DVD reads working.
3. **First pixels:** Nintendo logo / title screen rendered through aurora,
   including the WGPIPE path.
4. **Playable:** input works; can start a game, control Olimar and Pikmin, and
   finish a day. Audio stubbed.
5. **Saves and movies:** CARD saves round-trip; opening cutscene plays.
6. **Audio:** music and SFX through the DSP ucode HLE.
7. **Enhancements:** high-res verified, widescreen (3D + HUD + culling),
   high framerate with the frame-count audit.
8. **Full playthrough** on the laptop at 60 fps native resolution; fix what
   breaks.

## Verification

- Instruction-semantics tests for the recompiler (run in CI-less local `ctest`).
- Reference behaviour from Dolphin (`dnf install dolphin-emu`): its debugger
  with `build.map` loaded gives ground truth for register/memory state at chosen
  breakpoints, for diffing against the recomp's state in a debug build.
- Per-milestone smoke checks are listed above; the enhancement goals are judged
  by the user playing it ("feels right").

## Left to the builder

- Recompiler internals (IR or direct emission), C code style, how CR and flags
  are represented, optimisation of generated code.
- Fiber library choice (e.g. ucontext, boost.context, hand-rolled).
- Which reference executes instruction tests.
- The exact list of game functions to patch for widescreen/framerate (found
  during milestone 7 using the map and Ghidra/REA).
- Whether `mtx.a` and parts of MSL are native or translated.
- Splitting of generated code into translation units.
