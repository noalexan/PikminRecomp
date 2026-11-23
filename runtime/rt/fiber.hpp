// Host fibers for guest threads: ucontext on POSIX (fiber_posix.cpp), Win32 fibers on Windows
// (fiber_win32.cpp). All fibers run on one host thread.
#pragma once
#include <cstddef>

namespace rt {

struct Fiber;

// The calling host thread as a fiber (made on first use; never destroyed).
Fiber* fiber_current_thread();
// A new fiber with its own stack (with a guard page) that starts in `entry`, which must not return.
Fiber* fiber_create(size_t stack_size, void (*entry)());
// Saves the running fiber `from` and resumes `to`.
void fiber_switch(Fiber* from, Fiber* to);
// Frees a fiber that is not running; the thread fiber is ignored.
void fiber_destroy(Fiber* f);

}  // namespace rt
