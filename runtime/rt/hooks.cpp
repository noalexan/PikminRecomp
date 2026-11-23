// Default host hooks required by generated code. Weak so unit tests can substitute their own.
#include "rt/weak.hpp"
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "rt/clock.hpp"
#include "rt/crash.hpp"
#include "rt/log.hpp"
#include "rt/guest.hpp"


extern "C" {

PIKMIN_WEAK void ppc_bad_access(uint32_t ea) {
    rt::CrashInfo info{"ppc_bad_access"};
    info.has_guest_addr = true;
    info.guest_addr = ea;
    rt::crash_report(info);
    std::abort();
}

PIKMIN_WEAK void ppc_unimpl(ppc_ctx*, uint32_t addr, uint32_t word) {
    rt::log("fatal", "ppc_unimpl: instruction 0x%08" PRIx32 " at 0x%08" PRIx32, word, addr);
    rt::CrashInfo info{"ppc_unimpl"};
    info.has_guest_addr = true;
    info.guest_addr = addr;
    rt::crash_report(info);
    std::abort();
}

PIKMIN_WEAK void ppc_hle_missing(const char* name, uint32_t addr) {
    rt::log("fatal", "ppc_hle_missing: %s (0x%08" PRIx32 ") has no host implementation", name, addr);
    rt::CrashInfo info{"ppc_hle_missing"};
    info.has_guest_addr = true;
    info.guest_addr = addr;
    rt::crash_report(info);
    std::abort();
}

PIKMIN_WEAK uint64_t ppc_timebase(void) { return rt::clock().ticks(); }

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
