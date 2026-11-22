// Default host hooks required by generated code. Weak so unit tests can substitute their own.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "rt/clock.hpp"
#include "rt/guest.hpp"

extern "C" {

__attribute__((weak)) void ppc_bad_access(uint32_t ea) {
    std::fprintf(stderr, "ppc_bad_access: 0x%08" PRIx32 " (last HLE: %s, lr %08" PRIx32 ")\n", ea,
                 rt::current_function.name, rt::current_function.lr);
    std::abort();
}

__attribute__((weak)) void ppc_unimpl(ppc_ctx*, uint32_t addr, uint32_t word) {
    std::fprintf(stderr, "ppc_unimpl: instruction 0x%08" PRIx32 " at 0x%08" PRIx32 "\n", word, addr);
    std::abort();
}

__attribute__((weak)) void ppc_hle_missing(const char* name, uint32_t addr) {
    std::fprintf(stderr, "ppc_hle_missing: %s (0x%08" PRIx32 ") has no host implementation (last HLE: %s)\n", name,
                 addr, rt::current_function.name);
    std::abort();
}

__attribute__((weak)) uint64_t ppc_timebase(void) { return rt::clock().ticks(); }

}  // extern "C"
