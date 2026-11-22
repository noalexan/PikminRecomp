// Bringing the guest up: DOL loading, BootInfo, registers, static constructors, main().
#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>

#include "rt/guest.hpp"

namespace rt {

struct BootError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr uint32_t kArenaLo = 0x80403720u;  // _db_stack_addr rounded up to 32
constexpr uint32_t kArenaHi = 0x817F0000u;  // runtime-reserved RAM starts here
constexpr uint32_t kMainThreadEa = 0x817F0100u;
constexpr uint32_t kMainEntry = 0x800055C0u;  // main

// Arena bounds tracked by OSGet/SetArena* (initialised by write_boot_info).
extern uint32_t g_arena_lo, g_arena_hi;
// OSGetLanguage result (0 English, 1 German, 2 French, 3 Spanish, 4 Italian, 5 Dutch).
extern uint32_t g_language;

// Copies DOL sections into guest RAM, zeroes BSS, returns the entry point.
uint32_t load_dol(ppc_ctx* c, std::span<const uint8_t> dol);
// Writes the BootInfo block at 0x80000000; header32 is the first 0x20 bytes of the disc header.
void write_boot_info(ppc_ctx* c, const uint8_t* header32);
// r1 = _stack_addr, r2/r13 = small-data bases.
void init_registers(ppc_ctx* c);
// Calls each non-zero word of [start, end) until the first zero.
void run_constructors(ppc_ctx* c, uint32_t start, uint32_t end);
// write_boot_info + init_registers + constructors from .ctors + main(0, 0).
void boot_guest(ppc_ctx* c, const uint8_t* header32);

}  // namespace rt
