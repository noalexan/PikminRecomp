// Default host hooks required by generated code. Weak so unit tests can substitute their own.
#include <execinfo.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "rt/clock.hpp"
#include "rt/guest.hpp"

namespace {
// Host backtrace: translated frames appear as f_<guest address> (binary linked with -rdynamic).
void print_backtrace() {
    void* frames[64];
    int n = backtrace(frames, 64);
    char** names = backtrace_symbols(frames, n);
    std::fprintf(stderr, "backtrace:\n");
    for (int i = 1; i < n && names; ++i) std::fprintf(stderr, "  %s\n", names[i]);
}
}  // namespace

extern "C" {

__attribute__((weak)) void ppc_bad_access(uint32_t ea) {
    std::fprintf(stderr, "ppc_bad_access: 0x%08" PRIx32 " (last HLE: %s, lr %08" PRIx32 ")\n", ea,
                 rt::current_function.name, rt::current_function.lr);
    print_backtrace();
    std::abort();
}

__attribute__((weak)) void ppc_unimpl(ppc_ctx*, uint32_t addr, uint32_t word) {
    std::fprintf(stderr, "ppc_unimpl: instruction 0x%08" PRIx32 " at 0x%08" PRIx32 "\n", word, addr);
    std::abort();
}

__attribute__((weak)) void ppc_hle_missing(const char* name, uint32_t addr) {
    std::fprintf(stderr, "ppc_hle_missing: %s (0x%08" PRIx32 ") has no host implementation (last HLE: %s)\n", name,
                 addr, rt::current_function.name);
    print_backtrace();
    std::abort();
}

__attribute__((weak)) uint64_t ppc_timebase(void) { return rt::clock().ticks(); }

// A guest busy-wait iteration: deliver due events/interrupts and let other threads run.
void ppc_poll(ppc_ctx* c, uint32_t addr) {
    static const bool trace = std::getenv("PIKMIN_POLL_TRACE") != nullptr;
    if (trace) {
        static uint32_t last = 0;
        if (addr != last) std::fprintf(stderr, "poll %08x lr %08x r3 %08x r4 %08x\n", addr, c->lr, c->r[3], c->r[4]);
        last = addr;
    }
    rt::current_function.name = "ppc_poll";
    rt::safe_point();
}

}  // extern "C"
