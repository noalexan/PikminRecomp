# Freeze after ~7.5 minutes: guest clock overflow

- Symptom: in real-time runs the game froze after about 7 min 35 s of play, wherever it happened
  to be. The watchdog reported "no retrace for 5 s" at retrace ~22707 (454 s at 50 Hz). One report
  was in particle drawing (`zen::particleGenerator::drawPtclBillboard`), another at day end in
  `Jaf_StopSeq` (spinning until `audioproc` freed sequence 3, state 3 = stop requested).
- Cause: `Clock::ticks()` / `coarse_ticks()` computed `ns * kTicksPerSec / 1e9` in `uint64_t`.
  `ns * 40.5e6` passes 2^64 at ns ≈ 4.55e11 (455 s). The guest clock then wrapped back near its
  start, so no clock event was ever due again: no retrace, no AI DMA block (`g_blocks` frozen), no
  `audioproc` wakeups. `--fast` runs never hit it (virtual clock), which is why tests missed it.
- Fix: `Clock::ns_to_ticks` / `ticks_to_ns` split whole seconds from the remainder (exact, no
  overflow); `advance_to` uses `ticks_to_ns`. `tests/runtime/test_clock.cpp` covers 456 s and a month.
- How it was found: `--watchdog 0`, then gdb on the live process. Guest RAM is `ppc_ctx::mem`
  (phys = ea & 0x1FFFFFFF, big-endian); the guest threads are `rt::sched()::s.threads_`; the AI
  state is `'ai.cpp'::g_blocks`. Two samples showing `g_blocks` unchanged gave it away.
