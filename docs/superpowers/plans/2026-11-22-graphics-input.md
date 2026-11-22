# Graphics and Input Implementation Plan (Plan 3 of N)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run Pikmin in a window on the RTX 5070. The game's GX graphics go through aurora, the title screen renders, and keyboard and controller input reach the game (design milestones 0a, 3, 4).

**Architecture:** aurora (pinned commit, plus a small patch series) is built from source inside the build tree, Dawn included, and linked into `pikmin`.
- The 77 referenced `gx.a` functions become `hle_GX*` shims that marshal guest arguments (big-endian structs, matrices and colours passed by pointer, guest addresses) into aurora's PC GX API.
- Inline vertex writes to the write-gather pipe (`0xCC008000`) are fed byte for byte into aurora's FIFO.
- Frames are presented at each VI retrace. PAD reads come from aurora's SDL input, with a default keyboard layout.
- `--headless` keeps Plan 2's GPU-free path: the GX shims are inert when graphics are inactive.

**Tech Stack:** C++20, CMake + Ninja, aurora `f77492f74b0a81895511220bafa6bdc7e8bb9734` (encounter/aurora, MIT), Dawn (vendor build via aurora), SDL3 3.4 (system).

**Spec:** `docs/superpowers/specs/2026-11-22-pikmin-native-recomp-design.md` (sections "3. SDK replacement" (gx/vi/pad rows, GX inline FIFO writes, endianness risk), "6. Enhancements → Resolution", "7. Configuration and UX", Milestones 0a, 3, 4). Previous plans: `docs/superpowers/plans/2026-11-22-recompiler.md` and `docs/superpowers/plans/2026-11-22-runtime-boot.md`.

## Global Constraints

- No game data or generated code in git. The aurora and Dawn sources live only in the build tree (`build/cmake/_deps`).
- **System dependencies via dnf** (the user runs this once): `sudo dnf install -y fmt-devel xxhash-devel libpng-devel freetype-devel abseil-cpp-devel spirv-tools-devel`. SDL3-devel, zlib-ng-compat-devel, libzstd-devel, vulkan-headers, wayland-devel, libxkbcommon-devel, libX11-devel and libXrandr-devel are already installed.
- **aurora** comes via `FetchContent` pinned to the commit above, with `PATCH_COMMAND` applying `extern/aurora-patches/*.patch` in order.
  - **Deviation from spec:** the spec says "git submodule". FetchContent keeps our patches as reviewable files and keeps the aurora sources out of the repo; a patched submodule would always show as dirty.
  - aurora CMake options:
    - `AURORA_ENABLE_GX=ON`, `AURORA_ENABLE_DVD=OFF` (we read the ISO ourselves), `AURORA_ENABLE_CARD=OFF` (saves are a later milestone), `AURORA_ENABLE_THP=OFF`, `AURORA_ENABLE_RMLUI=OFF`, `AURORA_ENABLE_EXAMPLES=OFF`, `AURORA_ENABLE_TESTS=OFF`.
    - `AURORA_SDL3_PROVIDER=system`, `AURORA_DAWN_PROVIDER=vendor` (built from source: no unverified prebuilt binaries), `AURORA_DAWN_LINKAGE=static`.
  - Link `aurora::core`, `aurora::gx` and `aurora::pad` only. **Never link `aurora::main`**, because it defines `main`.
- **aurora configuration:** `mem1Size = 0` and `mem2Size = 0` (guest RAM and ARAM belong to the runtime), `emulateXfb = true`, `vsync = true`, `appName = "Pikmin Recomp"`. aurora already requests the high-performance adapter (the RTX 5070).
- **Guest ABI facts verified at real call sites:**
  - A `GXColor` / `GXColorS10` argument is a **pointer to a copy** in a GPR. Floats travel in f1–f8 independently of GPRs.
  - `GXSetCopyClear` is called with r3 = &color and r4 = z.
- **Guest struct and data facts:**
  - Matrices are big-endian f32 rows: Pos/Tex 3×4 = 12 floats, Nrm 3×3 = 9, Tex 2×4 = 8 when type is `GX_MTX2x4` (1), Projection 4×4 = 16.
  - `GXVtxDescList` entries are {u32 attr, u32 type}, terminated by attr `GX_VA_NULL` (0xFF).
  - `GXVtxAttrFmtList` entries are {u32 attr, u32 cnt, u32 type, u8 frac + 3 pad} (16 bytes), terminated by attr 0xFF.
  - `GXFogAdjTable` is 10 big-endian u16.
  - `GXTexObj` (32 bytes) and `GXLightObj` (64 bytes) are the same size in aurora and opaque to the game. aurora initialises them **in place** through a host pointer to the guest object.
- Keep Plan 2's guarantees: the `--headless` boot gate still passes, without a GPU.

## Review Focus

1. **Vertex arrays of unknown size.** The SDK's `GXSetArray` has no size, but aurora uploads `[data, data+size)`. Too small means garbage geometry; too large means huge per-frame uploads, which kill 240 fps later. *(Task 2's patch derives the size from the highest index each draw uses; test `test_array_size_inferred_from_indices`.)*
2. **Draws never explicitly ended.** On GameCube, `GXEnd` is an empty inline, so the game never calls it, while aurora's `GXEnd` publishes the draw. A draw left open would be lost or merged into the next. *(Task 5 closes any open draw at the next GX call and at present; test `test_inline_draw_closed_by_next_gx_call`.)*
3. **CPU-modified vertex data.** Games rewrite arrays each frame and call `GXInvalidateVtxCache`. Stale arrays mean frozen animation. *(Task 5 test `test_invalidate_vtx_cache_reuploads`.)*
4. **Window closed, minimised or resized.** Closing must exit cleanly (code 0); a minimised window must not stall game time.
5. **No usable GPU adapter.** Windowed mode must print one line and exit 2, rather than crash. `--headless` must still work. *(Task 3 tests.)*

---

### Task 1: aurora build, dependencies and GPU smoke test (milestone 0a, part 1)

**Files:**
- Create: `extern/aurora.cmake`, `extern/aurora-patches/README.md`, `tests/gfx/CMakeLists.txt`, `tests/gfx/gfx_test_util.hpp`, `tests/gfx/test_aurora_smoke.cpp`
- Modify: `CMakeLists.txt` (`option(PIKMIN_GRAPHICS "Build aurora graphics" ON)`, which includes `extern/aurora.cmake` and `tests/gfx`)

**Interfaces:**
- Produces:
  - Targets `aurora::core`, `aurora::gx` and `aurora::pad` (from the FetchContent build).
  - `tests/gfx/gfx_test_util.hpp`:
    - `bool gfx_test_init(uint32_t w, uint32_t h)`: `aurora_initialize` with a headless surface (`AURORA_SURFACE_HEADLESS`), the Vulkan backend and the Global Constraints config. Returns false when no adapter is available, in which case the test exits 77.
    - `std::vector<uint32_t> gfx_test_capture()`: ends the frame and returns RGBA8 pixels via `AuroraReadbackCaptureColor` / `AuroraReadbackGet`.
    - `uint32_t pixel(const std::vector<uint32_t>&, uint32_t x, uint32_t y)`.
- [ ] **Step 1: Install the dependencies** (the user runs this; it needs sudo): the dnf line from Global Constraints. Verify with `rpm -q fmt-devel xxhash-devel libpng-devel freetype-devel abseil-cpp-devel spirv-tools-devel`. Expected: all installed.
- [ ] **Step 2: Write the failing test** `tests/gfx/test_aurora_smoke.cpp`:
  - `gfx_test_init(64, 64)`, then `GXSetCopyClear({255, 0, 128, 255}, GX_MAX_Z24)`.
  - `aurora_begin_frame()`, then capture.
  - The centre pixel equals RGBA (255, 0, 128, 255).
- [ ] **Step 3: Run** `cmake -S . -B build/cmake -G Ninja && cmake --build build/cmake --target test_aurora_smoke`. Expected: FAIL (no `extern/aurora.cmake`).
- [ ] **Step 4: Implement** `extern/aurora.cmake` (FetchContent with the pinned commit, `PATCH_COMMAND` running `git apply` over the sorted patch list, the options above) and the test utilities. The first configure builds Dawn from source; log the wall time in the ledger.
- [ ] **Step 5: Run** `ctest --test-dir build/cmake -R test_aurora_smoke --output-on-failure`. Expected: PASS on the RTX 5070. Print the adapter name in the test output.
- [ ] **Step 6: Commit** `build: aurora (pinned, built from source) and GPU smoke test`.

### Task 2: aurora patch: vertex arrays of unknown size (milestone 0a, part 2)

**Files:**
- Create: `extern/aurora-patches/0001-gx-array-size-from-indices.patch`, `tests/gfx/test_aurora_arrays.cpp`

**Interfaces:**
- Produces: aurora's `GXSetArray(attr, data, 0, stride, le)` with **size 0 means unknown**. At draw time, for each attribute described as `GX_INDEX8`/`GX_INDEX16` whose array has unknown size:
  - the patch scans that draw's vertex data for the attribute's indices (big-endian u16 for INDEX16), and
  - uploads `[data, data + (maxIndex + 1) * stride)`.

  The cached range stays valid until the array is set again, `GXInvalidateVtxCache` is called, or a later draw needs a larger extent (the extent only grows).
- Patch site: `lib/gx/command_processor.cpp` (`push_gx_draw` and the array-cache code around the `cachedRange` uses). Keep the patch minimal; known sizes behave exactly as before.
- [ ] **Step 1: Write failing tests** in `tests/gfx/test_aurora_arrays.cpp`, calling aurora's GX API directly:
  - `test_array_size_inferred_from_indices`: a position array of 4 big-endian `s16` XYZ vertices covering the viewport, and a colour array of 4 RGBA8 entries all red. Set both with size 0. Draw a quad via `GXBegin(GX_QUADS, GX_VTXFMT0, 4)` with `GX_INDEX8` indices 0..3 (written with aurora's `GXPosition1x8` / `GXColor1x8`). The captured centre pixel is red.
  - `test_unknown_size_extent_grows`: a first draw uses indices 0..2, and a second draw in the same frame uses index 3, which lives beyond the first extent. Both render correctly; check pixels in both triangles.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_aurora_arrays --output-on-failure`. Expected: FAIL (an assert in aurora, or wrong pixels).
- [ ] **Step 3: Implement** the patch (author it in a scratch checkout of the pinned commit; `git diff` produces the patch file).
- [ ] **Step 4: Reconfigure** so FetchContent re-applies the patches, then run the test. Expected: PASS. Also re-run `test_aurora_smoke`.
- [ ] **Step 5: Commit** `fix(aurora): infer vertex array extent from indices when size is unknown`.

### Task 3: Graphics runtime: window, present at retrace, events, screenshots

**Files:**
- Create: `runtime/rt/gfx.hpp`, `runtime/rt/gfx.cpp`, `tests/gfx/test_gfx_runtime.cpp`, `tests/runtime/test_cli_gfx.py`
- Modify: `runtime/main.cpp` (windowed mode; `--screenshot`; `--headless` unchanged), `runtime/hle/vi.cpp` (present on retrace), `runtime/rt/config.cpp` / `config.hpp` (keys), `runtime/CMakeLists.txt` and `runtime/gen.cmake` (link aurora into `pikmin_rt` / `pikmin` when `PIKMIN_GRAPHICS`)

**Interfaces:**
- Produces (namespace `rt`):
  - `struct GfxOptions { bool headless_surface; uint32_t width = 1920, height = 1440; bool fullscreen = false; bool vsync = true; }`.
  - `bool gfx_init(const GfxOptions&)`: returns false, with one logged line, when aurora finds no adapter. On success it calls `aurora_begin_frame()`.
  - `bool gfx_active()`: true between a successful init and shutdown. Every GX shim returns early when this is false.
  - `void gx_close_open_draw()`: declared in `gfx.hpp`, with a weak no-op definition in `gfx.cpp` until Task 5 provides the real one.
  - `void gfx_present()`: closes any open draw (`gx_close_open_draw()`), then calls `aurora_end_frame()` and `aurora_update()`. It handles events: `AURORA_EXIT` calls `rt::exit_process(0)`; `AURORA_PAUSED`/`UNPAUSED` are logged only, so game time continues. Then it calls `aurora_begin_frame()`, retrying on false without blocking game time.
  - `void gfx_request_screenshot(uint32_t frame, std::string path)`: at the retrace whose count equals `frame`, captures the frame via the readback API and writes a binary PPM (P6) to `path`.
  - Config keys: `window_width` (int), `window_height` (int), `fullscreen` (bool), `vsync` (bool).
- Behaviour in `vi.cpp`: in each retrace, after the count increments and before waking waiters, call `rt::gfx_present()` if `gfx_active()`.
- CLI:
  - Without `--headless`, `pikmin` initialises graphics (the Plan 2 "Plan 3" error is removed). If init fails, it prints `pikmin: no usable GPU (…); try --headless` and exits 2.
  - `--screenshot FRAME:PATH` works in windowed mode, and also with `--offscreen`, a new flag meaning headless surface + real GPU, used by tests.
- [ ] **Step 1: Write failing tests**:
  - `tests/gfx/test_gfx_runtime.cpp` (exits 77 if init fails): `gfx_init({.headless_surface = true, .width = 64, .height = 64})`. Set a copy-clear colour through aurora, call `gfx_present()` 3 times, then request and take a screenshot. The PPM header is `P6\n64 64\n255\n` and its centre pixel matches the clear colour.
  - `tests/runtime/test_cli_gfx.py`: with `WAYLAND_DISPLAY`, `DISPLAY` and the GPU devices hidden (`env -i` plus `VK_ICD_FILENAMES=/nonexistent`), `pikmin --disc <junk-but-valid synthetic ISO>` without `--headless` exits 2, with one line containing `try --headless`.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_gfx_runtime` and `pytest tests/runtime/test_cli_gfx.py -q`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** both, plus the full suite. The Plan 2 boot gate must still pass. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): aurora window, present at retrace, screenshots`.

### Task 4: GX shims: state, colours, matrices, lighting, TEV

**Files:**
- Create: `runtime/hle/gx/marshal.hpp` (guest → aurora conversions), `runtime/hle/gx/gx_state.cpp`, `tests/gfx/test_gx_state.cpp`
- Delete: the matching entries from `runtime/hle/gx_headless.cpp`. That file shrinks as Tasks 4–5 land, and is deleted in Task 5.

**Interfaces:**
- Produces, in `marshal.hpp` (namespace `rt::gx`):
  - `GXColor color_at(ppc_ctx*, uint32_t ea)` (4 bytes R, G, B, A) and `GXColorS10 color_s10_at(ppc_ctx*, uint32_t ea)` (4 big-endian s16).
  - `void floats_at(ppc_ctx*, uint32_t ea, float* out, int n)`: big-endian f32 to host.
  - `void* host_ptr(ppc_ctx*, uint32_t ea)` (RAM; ea 0 maps to nullptr).
- Shims for these functions; every one starts with `HLE_ENTRY` and then `if (!rt::gfx_active()) return;`, preceded by `gx_close_open_draw()` from Task 5:
  - **Scalar passthrough (GPR args in order):** GXSetAlphaCompare, GXSetAlphaUpdate, GXSetBlendMode, GXSetChanCtrl, GXSetCoPlanar, GXSetColorUpdate, GXSetCullMode, GXSetCurrentMtx, GXSetDispCopyDst, GXSetDispCopySrc, GXSetDither, GXSetLineWidth, GXSetNumChans, GXSetNumTevStages, GXSetNumTexGens, GXSetPixelFmt, GXSetPointSize, GXSetScissor, GXSetScissorBoxOffset, GXSetTevAlphaIn, GXSetTevAlphaOp, GXSetTevColorIn, GXSetTevColorOp, GXSetTevKAlphaSel, GXSetTevKColorSel, GXSetTevOp, GXSetTevOrder, GXSetTevSwapMode, GXSetTevSwapModeTable, GXSetTexCoordGen2, GXSetTexCopyDst, GXSetTexCopySrc, GXSetZCompLoc, GXSetZMode, GXSetVtxAttrFmt, GXSetVtxDesc, GXClearVtxDesc, GXInvalidateVtxCache, GXInvalidateTexAll, GXPixModeSync, GXFlush, GXDrawDone.
  - **Floats:** `GXSetViewport` (f1–f6). `GXSetDispCopyYScale(f1)` returns aurora's u32 in r3. `GXInitLightAttn(obj, f1–f6)`; `GXInitLightDir`, `GXInitLightPos` and `GXInitSpecularDir` (obj, f1–f3).
  - **Colour by pointer:** `GXSetCopyClear(color*, z)`, `GXSetChanAmbColor(chan, color*)`, `GXSetChanMatColor(chan, color*)`, `GXSetTevColor(id, color*)`, `GXSetTevKColor(id, color*)`, `GXSetTevColorS10(id, s10*)`, `GXInitLightColor(obj, color*)`, `GXSetFog(type, f1–f4, color*)` (its colour pointer is r4).
  - **Matrices:** `GXLoadPosMtxImm(mtx, id)` (12), `GXLoadNrmMtxImm(mtx, id)` (9), `GXLoadTexMtxImm(mtx, id, type)` (12, or 8 when type is 1), `GXSetProjection(mtx, type)` (16). Each converts into a local `float[]` and passes that.
  - **Lists:** `GXSetVtxDescv(list)` and `GXSetVtxAttrFmtv(fmt, list)` convert into host arrays (Global Constraints layouts). `GXSetFogRangeAdj(enable, center, table)`: table 0 → nullptr, otherwise 10 u16 converted. `GXSetCopyFilter(aa, pattern*, vf, vfilter*)` passes byte arrays through `host_ptr`.
  - **Light objects:** `GXLoadLightObjImm(obj, light)`, with obj passed as `host_ptr`.
  - **Special:** `GXInit(base, size)` calls aurora `GXInit(host_ptr(base), size)` and still returns guest 0x817F0000. `GXSetCurrentGXThread()` returns the current guest thread's address (no aurora call).
- [ ] **Step 1: Write failing tests** in `test_gx_state.cpp`. Each drives the **shims** through a `ppc_ctx` holding guest data, after `gfx_init` offscreen, and checks captured pixels:
  - `test_copy_clear_color_by_pointer`: the colour is written big-endian at a guest address, and the cleared frame has that colour.
  - `test_projection_and_pos_mtx_big_endian`: an orthographic projection and a position matrix translating a quad to the right half, both as big-endian floats in guest RAM. The left-half pixel is the clear colour; the right-half pixel is the quad colour. The quad is drawn with aurora's direct-mode API in the test.
  - `test_tev_color_by_pointer`: a TEV stage outputs `GX_CC_C0`, set via `GXSetTevColor(GX_TEVREG0, &color)`; the pixel equals that colour.
  - `test_vtx_desc_list_conversion`: `GXSetVtxDescv` with a guest list {POS DIRECT, CLR0 DIRECT, NULL} renders like the equivalent `GXSetVtxDesc` calls.
  - `test_shims_inert_when_inactive`: without `gfx_init`, calling a shim neither crashes nor touches aurora.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_gx_state --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** the test and the full suite. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): GX state, colour, matrix and lighting shims over aurora`.

### Task 5: GX shims: geometry, inline FIFO, arrays, display lists, textures, copies

**Files:**
- Create: `runtime/hle/gx/gx_geometry.cpp`, `tests/gfx/test_gx_geometry.cpp`
- Modify: `runtime/rt/mmio.cpp` (WGPIPE writes go to aurora's FIFO when graphics are active)
- Delete: `runtime/hle/gx_headless.cpp` (every function now has a real shim)

**Interfaces:**
- Produces:
  - `void rt::gx_close_open_draw()`: if a `GXBegin` is open, calls aurora `GXEnd()`. It's called at the top of every GX shim and in `gfx_present`.
  - Shims:
    - `GXBegin(prim, fmt, nverts)`: closes any open draw, calls aurora `GXBegin`, and marks the draw open.
    - `GXSetArray(attr, data, stride)` → aurora `GXSetArray(attr, host_ptr(data), 0, stride, false)`, where size 0 means unknown (Task 2).
    - `GXCallDisplayList(list, n)` → aurora with `host_ptr(list)`; the data is already big-endian. `GXBeginDisplayList(buf, size)` / `GXEndDisplayList()` → aurora, with the buffer in guest RAM and the size returned in r3.
    - `GXInitTexObj(obj, image, w, h, fmt, wrapS, wrapT, mipmap)` and `GXInitTexObjLOD(obj, min, mag, f1 min_lod, f2 max_lod, f3 bias, bias_clamp, edge_lod, aniso)`: obj and image via `host_ptr`, initialised in place.
    - `GXLoadTexObj(obj, id)`.
    - `GXCopyDisp(dest, clear)` and `GXCopyTex(dest, clear)` → aurora with `host_ptr(dest)`. aurora keys copies by host pointer, so a later `GXInitTexObj` on the same guest address finds the copy.
  - MMIO: a write to physical `0x0C008000` while `gfx_active()` goes to aurora's FIFO as raw big-endian data of the access size (internal `aurora::gx::fifo::write_u8` / `write_u16` / `write_u32`; float stores arrive as their bit pattern through `mem_w32`). In headless mode it's counted and dropped as before.
- [ ] **Step 1: Write failing tests** in `test_gx_geometry.cpp` (offscreen; every call goes through the shims and guest RAM):
  - `test_inline_draw_closed_by_next_gx_call`: `GXBegin(GX_QUADS, fmt0, 4)` via the shim. Then 4 vertices of big-endian `f32` XYZ position + RGBA8 colour, written through `mem_w32` / `mem_w8` to `0xCC008000` (exactly what translated code does), then a `GXSetZMode` shim call and a present. The quad is visible in the capture.
  - `test_display_list_big_endian`: a display list assembled big-endian in guest RAM (draw command, vertex count, vertex data), called through `GXCallDisplayList`. The triangle is visible.
  - `test_indexed_arrays_unknown_size`: arrays via the `GXSetArray` shim (big-endian `s16` positions, RGBA8 colours), with `GX_INDEX8` indices written through WGPIPE. Correct pixels.
  - `test_invalidate_vtx_cache_reuploads`: draw, rewrite the colour array in guest RAM, call `GXInvalidateVtxCache`, draw again in the next frame. The new colour shows.
  - `test_texture_from_guest_memory`: a 4×4 RGBA8 GameCube-tiled texture (big-endian tile layout) in guest RAM, through `GXInitTexObj` / `GXLoadTexObj`, sampled on a full-screen quad. The centre pixel equals the texel colour.
  - `test_copy_tex_then_sample`: clear to green, `GXCopyTex` to a guest address, then sample that address as a texture in the next frame. Green.
  - Add to `tests/runtime/test_cli.py` `test_every_referenced_gx_function_has_a_shim`: every `hle_GX*` symbol that the generated code references is a strong `T` in `nm build/cmake/pikmin`, so none is left on its weak `ppc_hle_missing` stub.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_gx_geometry --output-on-failure` and `pytest tests/runtime/test_cli.py -q`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** the test and the full suite, including the Plan 2 headless gate. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): GX geometry, inline FIFO, arrays, display lists, textures and copies`.

### Task 6: Input: aurora PAD, default keyboard layout, scripted input for tests

**Files:**
- Create: `runtime/hle/pad_aurora.cpp`, `runtime/rt/input_script.hpp`, `runtime/rt/input_script.cpp`, `tests/gfx/test_pad.cpp`
- Modify: `runtime/hle/pad.cpp` (headless path stays), `runtime/main.cpp` (`--input-script PATH`)

**Interfaces:**
- Produces:
  - **With graphics active:**
    - `PADInit` → aurora `PADInit()`, returns its result.
    - `PADRead(status)`: aurora `PADRead(host[4])`, then each port is converted to the guest 12-byte layout (button u16 BE, stickX/Y, substickX/Y, triggerL/R, analogA/B, err), and aurora's return value goes back in r3.
    - `PADReset`, `PADRecalibrate`, `PADClamp(status)` (applied to the guest copy, converted in and out), `PADSetSpec` and `PADControlMotor` → aurora.
  - **Default keyboard layout** (port 0, via `aurora::pad::set_default_keyboard_bindings`):
    - Control stick: W/A/S/D. C-stick (Pikmin swarming): arrow keys.
    - A: J; B: K; X: L; Y: I; Z: U; L: Q; R: E; Start: Enter.
    - D-pad: T/G/F/H (up/down/left/right).
  - Controllers use aurora's defaults (SDL gamepad, GC adapter).
  - **Scripted input**, `--input-script PATH`. Lines have the form `FRAME BUTTONS [STICKX STICKY]`, where BUTTONS is `+`-joined names from {A, B, X, Y, Z, L, R, START, UP, DOWN, LEFT, RIGHT} or `-` for none. From that retrace on, port 0 reports exactly that state, overriding aurora (so tests are deterministic). Buttons use the SDK masks: LEFT 0x0001, RIGHT 0x0002, DOWN 0x0004, UP 0x0008, Z 0x0010, R 0x0020, L 0x0040, A 0x0100, B 0x0200, X 0x0400, Y 0x0800, START 0x1000.
- [ ] **Step 1: Write failing tests** in `test_pad.cpp`:
  - The script parser handles `10 A+START`, `20 - 0 -127` and comments, and rejects an unknown button with a message naming it.
  - With the script active at frame ≥ 10, `PADRead` writes guest port 0 with button 0x1100 (big-endian) and err 0.
  - The guest layout conversion of a synthetic host `PADStatus` (with `extButton` set) produces the 12-byte guest record exactly.
- [ ] **Step 2: Run** `ctest --test-dir build/cmake -R test_pad --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run** the test. Expected: PASS.
- [ ] **Step 5: Commit** `feat(runtime): aurora input with keyboard layout and scripted input`.

### Task 7: Rendering bring-up: the title screen (milestones 3 and 4)

**Files:**
- Create: `tests/runtime/test_title_screen.py`, `docs/superpowers/notes/graphics-bringup.md`
- Modify: whichever shim or runtime module the bring-up shows is wrong

**Interfaces:**
- **The gate**, on the user's disc and real GPU (offscreen surface, so it runs unattended):
  1. `pikmin --disc ISO --offscreen --frames 900 --screenshot 600:build/title.ppm` exits 0, with no `ppc_hle_missing`, `ppc_unimpl`, `ppc_bad_access`, `watchdog` or aurora `ERROR`/`FATAL` log lines.
  2. `build/title.ppm` isn't blank: at least 32 distinct colours, and at least 5% of pixels differ from the most common colour.
  3. An input script `700 START` / `701 -` makes the game respond: at least 3 more `dvd: open` lines after frame 700 than in the same run without the script, **or** a screenshot at frame 880 that differs from one taken without the script (over 5% of pixels).
  4. The Plan 2 headless gate still passes.
- [ ] **Step 1: Write the failing gate test** `tests/runtime/test_title_screen.py` (skips without the disc or the GPU), covering conditions 1–3. Copy the screenshots to `build/` for inspection.
- [ ] **Step 2: Run** `pytest tests/runtime/test_title_screen.py -v`. Expected: FAIL.
- [ ] **Step 3: Bring-up loop**, the same rules as Plan 2 Task 10:
  1. Take the first failure.
  2. Write a unit test reproducing it in the owning test file (Tasks 3–6). Watch it fail, fix it, run the full suite.
  3. Log it in `graphics-bringup.md` and commit `fix(gfx): …`.

  Look at the screenshots after each iteration. Rendering that is wrong but non-blank (colours, missing layers, upside down) counts as a failure only if it is clearly wrong against the original game, which the user can confirm in Step 5.
- [ ] **Step 4: Run** the gate and the full suite: `pytest -q && cmake --build build/cmake && ctest --test-dir build/cmake --output-on-failure`. Expected: all pass.
- [ ] **Step 5: Manual check with the user.** They run `build/cmake/pikmin --disc ~/Downloads/GPIP01/game.iso` in a window on the RTX 5070, look at the title screen, and press Enter. Record their observations in `graphics-bringup.md`.
- [ ] **Step 6: Commit** `test(gfx): title screen renders and responds to input`.
