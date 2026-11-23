# Crash diagnostics: non-obvious choices

- **One report per process.** `crash_report` sets an atomic flag first. The `ppc_*` fatal hooks
  report and then `abort()`; the SIGABRT handler then finds the flag set and only re-raises, so a
  fatal guest error never produces two reports.
- **`SA_RESETHAND` on every handler.** If the report path itself faults (a corrupt fiber stack,
  say), the second signal takes the default action and the process dies instead of looping.
  `--crash-test double_fault` covers this.
- **Alternate signal stack.** Guest threads are fibers with their own mmap'd stacks; a stack
  overflow leaves no room to run a handler there. The 64 KiB alt stack belongs to the game
  thread (all fibers run on it). Other host threads (audio, aurora workers) handle signals on
  their own stacks.
- **`backtrace()` is primed in `crash_install`.** glibc loads libgcc's unwinder lazily on the first
  call; doing that inside a signal handler means `dlopen` + `malloc`, which isn't safe.
- **The faulting PC replaces frame 2** of the handler's backtrace (frame 0 = `crash_report`, 1 =
  the handler, 2 = the signal trampoline), so the first interesting frame is where it crashed.
- **No clock reads in the event ring.** `Clock::ticks()` advances virtual time in `--fast` mode, so
  tracing would change the game's timeline. Entries carry the retrace count and a sequence number.
- **`ppc_poll` isn't traced.** It runs at every guest busy-wait iteration and would push everything
  else out of the ring.
- **Cost.** Recording every HLE entry measured within noise: headless `--fast`, 3000 retraces,
  1.29 s with the ring vs 1.27 s without (three runs each).
- **Release `-g1`.** The installed binary grows from 42 to 69 MB; `-rdynamic` alone already names
  `f_<addr>` frames, the line tables add file:line for host code in `addr2line`.
