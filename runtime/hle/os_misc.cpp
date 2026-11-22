// OSInit, arena, allocator bootstrap, cache ops, system settings, reset/exit.
#include <cstdlib>
#include <cstring>

#include "rt/boot.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"

using rt::arg;
using rt::ret;

extern "C" {

void hle_OSInit(ppc_ctx* c) {
    HLE_ENTRY(c, "OSInit");  // the runtime already initialised arena, clock and threads
}

void hle_OSGetArenaLo(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetArenaLo");
    ret(c, rt::g_arena_lo);
}

void hle_OSGetArenaHi(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetArenaHi");
    ret(c, rt::g_arena_hi);
}

void hle_OSSetArenaLo(ppc_ctx* c) {
    HLE_ENTRY(c, "OSSetArenaLo");
    rt::g_arena_lo = arg(c, 0);
    rt::wr32(c, 0x80000030u, rt::g_arena_lo);
}

void hle_OSSetArenaHi(ppc_ctx* c) {
    HLE_ENTRY(c, "OSSetArenaHi");
    rt::g_arena_hi = arg(c, 0);
    rt::wr32(c, 0x80000034u, rt::g_arena_hi);
}

// Returns the arena start past the heap descriptor array (maxHeaps * 0x0C, 32-byte aligned).
void hle_OSInitAlloc(ppc_ctx* c) {
    HLE_ENTRY(c, "OSInitAlloc");
    uint32_t start = arg(c, 0), max_heaps = arg(c, 2);
    ret(c, (start + max_heaps * 0x0C + 31) & ~31u);
}

void hle_OSGetLanguage(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetLanguage");
    ret(c, rt::g_language);
}

void hle_OSGetSoundMode(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetSoundMode");
    ret(c, 1);  // stereo
}

void hle_OSSetSoundMode(ppc_ctx* c) { HLE_ENTRY(c, "OSSetSoundMode"); }
void hle_OSGetResetSwitchState(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetResetSwitchState");
    ret(c, 0);
}
void hle_OSSetProgressiveMode(ppc_ctx* c) { HLE_ENTRY(c, "OSSetProgressiveMode"); }

// Cache maintenance has no effect on the host.
void hle_DCFlushRange(ppc_ctx* c) { HLE_ENTRY(c, "DCFlushRange"); }
void hle_DCFlushRangeNoSync(ppc_ctx* c) { HLE_ENTRY(c, "DCFlushRangeNoSync"); }
void hle_DCInvalidateRange(ppc_ctx* c) { HLE_ENTRY(c, "DCInvalidateRange"); }
void hle_DCStoreRange(ppc_ctx* c) { HLE_ENTRY(c, "DCStoreRange"); }
void hle_DCStoreRangeNoSync(ppc_ctx* c) { HLE_ENTRY(c, "DCStoreRangeNoSync"); }
void hle_PPCSync(ppc_ctx* c) { HLE_ENTRY(c, "PPCSync"); }

void hle_DCZeroRange(ppc_ctx* c) {
    HLE_ENTRY(c, "DCZeroRange");
    uint32_t addr = arg(c, 0), len = arg(c, 1);
    std::memset(rt::host(c, addr, len), 0, len);
}

void hle_OSResetSystem(ppc_ctx* c) {
    HLE_ENTRY(c, "OSResetSystem");
    rt::log("os", "OSResetSystem: exiting");
    std::exit(0);
}

void hle__ExitProcess(ppc_ctx* c) {
    HLE_ENTRY(c, "_ExitProcess");
    rt::log("os", "_ExitProcess: exiting");
    std::exit(0);
}

}  // extern "C"
