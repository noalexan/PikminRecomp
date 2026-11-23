#ifndef _WIN32
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "rt/fiber.hpp"

namespace rt {

struct Fiber {
    ucontext_t uc{};
    void* stack = nullptr;  // mmap'd, guard page first; null for the thread fiber
    size_t size = 0;
};

Fiber* fiber_current_thread() {
    static thread_local Fiber self;
    return &self;
}

Fiber* fiber_create(size_t stack_size, void (*entry)()) {
    auto* f = new Fiber;
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    f->size = stack_size + page;
    f->stack = mmap(nullptr, f->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (f->stack == MAP_FAILED) {
        std::perror("mmap fiber stack");
        std::abort();
    }
    mprotect(f->stack, page, PROT_NONE);  // guard page below the stack
    getcontext(&f->uc);
    f->uc.uc_stack.ss_sp = f->stack;
    f->uc.uc_stack.ss_size = f->size;
    f->uc.uc_link = nullptr;
    makecontext(&f->uc, entry, 0);
    return f;
}

void fiber_switch(Fiber* from, Fiber* to) { swapcontext(&from->uc, &to->uc); }

void fiber_destroy(Fiber* f) {
    if (!f || f == fiber_current_thread()) return;
    if (f->stack) munmap(f->stack, f->size);
    delete f;
}

}  // namespace rt
#endif
