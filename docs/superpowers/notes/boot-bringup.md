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
