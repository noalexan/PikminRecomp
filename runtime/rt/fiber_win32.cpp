#ifdef _WIN32
#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "rt/fiber.hpp"

namespace rt {

struct Fiber {
    void* handle = nullptr;
    void (*entry)() = nullptr;
    bool thread = false;
};

namespace {
VOID CALLBACK start(LPVOID p) { static_cast<Fiber*>(p)->entry(); }
}  // namespace

Fiber* fiber_current_thread() {
    static thread_local Fiber self;
    if (!self.handle) {
        self.thread = true;
        self.handle = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
        if (!self.handle) self.handle = GetCurrentFiber();  // already a fiber
    }
    return &self;
}

Fiber* fiber_create(size_t stack_size, void (*entry)()) {
    fiber_current_thread();  // switching needs the thread to be a fiber too
    auto* f = new Fiber;
    f->entry = entry;
    f->handle = CreateFiberEx(64 * 1024, stack_size, FIBER_FLAG_FLOAT_SWITCH, start, f);
    if (!f->handle) {
        std::fprintf(stderr, "CreateFiberEx failed (%lu)\n", GetLastError());
        std::abort();
    }
    return f;
}

void fiber_switch(Fiber*, Fiber* to) { SwitchToFiber(to->handle); }

void fiber_destroy(Fiber* f) {
    if (!f || f->thread) return;
    DeleteFiber(f->handle);
    delete f;
}

}  // namespace rt
#endif
