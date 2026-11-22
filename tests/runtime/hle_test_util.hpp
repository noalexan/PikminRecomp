// Shared helpers for runtime tests: guest RAM, default hooks, calling HLE functions.
#pragma once
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <vector>

#include "rt/guest.hpp"

inline std::vector<uint8_t>& test_ram() {
    static std::vector<uint8_t> ram(PPC_RAM_SIZE);
    return ram;
}

inline std::map<uint32_t, ppc_fn>& test_funcs() {
    static std::map<uint32_t, ppc_fn> f;
    return f;
}

// Registers a host function as a fake guest function and returns its address.
inline uint32_t fake_fn(ppc_fn fn) {
    uint32_t addr = 0x80010000u + static_cast<uint32_t>(test_funcs().size()) * 4;
    test_funcs()[addr] = fn;
    return addr;
}

inline ppc_ctx make_ctx() {
    ppc_ctx c{};
    c.mem = test_ram().data();
    c.r[1] = 0x80400000u;
    c.r[2] = 0x803f5120u;
    c.r[13] = 0x803e9c60u;
    return c;
}

// Calls an HLE function with integer arguments in r3.. and returns r3.
inline uint32_t call(void (*hle)(ppc_ctx*), ppc_ctx* c, std::initializer_list<uint32_t> args) {
    int i = 3;
    for (uint32_t a : args) c->r[i++] = a;
    hle(c);
    return c->r[3];
}

// Test hooks: function lookup through test_funcs() and a fatal bad-access handler.
#define DEFINE_TEST_LOOKUP                                                  \
    extern "C" {                                                            \
    ppc_fn ppc_lookup(uint32_t addr) { return test_funcs().at(addr); }      \
    void ppc_bad_access(uint32_t ea) {                                      \
        std::fprintf(stderr, "bad access %08x\n", ea);                      \
        std::exit(2);                                                       \
    }                                                                       \
    }

// Lookup + inert MMIO (tests that don't exercise the runtime's MMIO layer).
#define DEFINE_TEST_HOOKS                                                   \
    DEFINE_TEST_LOOKUP                                                      \
    extern "C" {                                                            \
    uint32_t ppc_mmio_read(uint32_t, int) { return 0; }                     \
    void ppc_mmio_write(uint32_t, uint32_t, int) {}                         \
    }
