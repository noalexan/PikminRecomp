# Headless boot bring-up log

Each entry: symptom, cause, fix, test.

1. **strcpy ran off the end of RAM in `DVDT_SetRootPath`.** Cause: `load_dol` zeroed the DOL header's BSS range *after* copying sections; that range spans `.bss`..`.sbss2` and covers the initialised `.sdata`/`.sdata2`, wiping constants (e.g. strcpy's `0xFEFEFEFF`/`0x80808080` at r13+0x2a40). Fix: zero first, then copy sections. Test: `test_boot` (BSS range overlapping a data section keeps the data). Also added a translated-`strcpy` alignment sweep to `test_real_functions` while ruling out a recompiler bug.
2. **`OSAllocFromHeap` reached from JAudio's `OSAlloc2`.** Cause: with `StartAudioThread` stubbed, JAudio falls back to the SDK heap the game never creates. Fix: override `Jac_Start` (JAudio's whole start-up). Test: `test_jaudio_entry_points_overridden`.
3. **Hang in `Jac_SceneSetup`** (spins until `first_load`, set by a JAudio DVD-load callback). Cause: game-side audio glue depends on the JAudio engine running. Fix: stub audio at the game/JAudio boundary — all 43 JAudio functions the game calls directly are silent no-ops returning 0 (`sqrtf__3stdFf` stays translated). Test: `test_game_never_calls_translated_jaudio`.
4. **Hang in `System::copyWaitUntilDone`** (busy-waits on a flag set by the ARQ completion callback, no OS call in the loop). Fix: ARAM DMA is instant on the host, so `ARQPostRequest` delivers its completion callback before returning when interrupts are enabled (deferred otherwise). Test: `test_ar_mmio`.
Tooling added during bring-up: fatal errors print a host backtrace (`-rdynamic`, frames named `f_<guest addr>`), and the watchdog signals the game thread to print where it is stuck.
5. **Segfault (exit 139) after reaching the frame limit.** Cause: `std::exit` from inside a guest fiber ran static destructors, which unmapped the fiber stack still in use. Fix: `rt::exit_process()` flushes stdio and `_Exit`s; used by the frame limit, `OSResetSystem`, `_ExitProcess` and the deadlock handler. Test: the boot gate's exit-code check.
6. **Gate condition corrected:** the retail build makes no `OSReport`/`WriteUARTN`/`HIOWrite` calls in the first 600 frames (checked with breakpoints), so the gate checks that the title-screen assets load instead.

Result: 600 frames headless (fast clock) — threads `dvdFunc` and `loadFunc` created, 17 disc files opened including the opening cinematic and title logo assets, clean exit.

## Audio bring-up

JAudio now runs translated; only `dsp.a`/`ai.a` below it are HLE (`hle/dsp.cpp`, `hle/ai.cpp`, Zelda ucode ported from Dolphin in `hle/dsp/`, output in `rt/audio_out.cpp`).

7. **Hang in `Jac_InitEventSystem` (and later `Jac_HVQM_Init`, `JV_LoadFile`).** JAudio busy-waits on state its audio/DVD threads produce, with no OS call in the loop; on hardware the AI/DSP interrupt preempts the loop. Fix: overrides mode `"poll"` — the function is translated, but its entry and every loop head call `ppc_poll` (a safe point). The list in `overrides.toml` comes from grepping the decomp for empty loops. Test: `test_poll_marks_entry_and_loop_heads`, `test_jaudio_busy_waits_poll`.
8. **Title BGM never started (only the boot "Pikmin!" voice played).** `Jac_SceneSetup` queues `DVDT_CheckPass(bgm, __Loaded)` to JAudio's DVD thread (higher priority) *before* it records `current_bgm`; `__Loaded` only starts the BGM if `current_bgm` matches. On hardware the DVD thread blocks in its preceding `DVDReadPrio`, so the game thread gets there first. Our reads were instant. Fix: `DVDReadPrio` blocks its thread ~0.5 ms of guest time after the copy.
9. DSP->CPU mails queue in a mailbox; a mail sent with an interrupt raises it when it reaches the head. JAudio's own `__DSPHandler` (0x80008920, checked by `test_dsp_handler_address_matches_runtime`) is called on the interrupt context. AI DMA blocks are clock events (32029 Hz), samples big-endian R,L.
Verification: `PIKMIN_AUDIO_DUMP=build/x.wav` dumps the mixed output in any mode; `PIKMIN_AUDIO_DEBUG=1` logs DSP frame/voice counts and the SDL queue; `PIKMIN_POLL_TRACE=1` logs `ppc_poll` sites.
