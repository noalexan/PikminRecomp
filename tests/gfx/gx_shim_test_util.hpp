// Helpers for driving the runtime's hle_GX* shims with guest data, then rendering offscreen.
#pragma once
#include <cstring>
#include <initializer_list>
#include <map>
#include <vector>

#include "gfx_test_util.hpp"
#include "rt/clock.hpp"
#include "rt/gfx.hpp"
#include "rt/guest.hpp"
#include "rt/irq.hpp"
#include "rt/sched.hpp"

inline std::vector<uint8_t>& guest_ram() {
    static std::vector<uint8_t> ram(PPC_RAM_SIZE);
    return ram;
}

// Guest context plus the runtime pieces every HLE call relies on (HLE_ENTRY is a safe point).
inline ppc_ctx& make_guest_ctx() {
    static ppc_ctx c{};
    c.mem = guest_ram().data();
    c.r[1] = 0x80400000u;
    rt::sched().init(&c, 0x817F0100u);
    rt::clock().init(rt::Clock::Mode::Fast);
    rt::irq().init(&c);
    return c;
}

// Calls a shim with integer args in r3.. and float args in f1..
inline uint32_t gcall(void (*hle)(ppc_ctx*), ppc_ctx* c, std::initializer_list<uint32_t> ints,
                      std::initializer_list<double> floats = {}) {
    int i = 3;
    for (uint32_t a : ints) c->r[i++] = a;
    int f = 1;
    for (double d : floats) c->f[f++].ps0 = d;
    hle(c);
    return c->r[3];
}

inline void gput32(uint32_t ea, uint32_t v) { put_be32(&guest_ram()[ea & 0x01FFFFFF], v); }
inline void gputf(uint32_t ea, float f) { put_bef(&guest_ram()[ea & 0x01FFFFFF], f); }
inline void gputrgba(uint32_t ea, uint32_t rgba) { gput32(ea, rgba); }

// Row-major big-endian floats into guest RAM.
inline void gputmtx(uint32_t ea, const float* m, int n) {
    for (int i = 0; i < n; ++i) gputf(ea + 4 * i, m[i]);
}

inline bool shim_test_init() {
    rt::GfxOptions opt;
    opt.headless_surface = true;
    opt.width = 64;
    opt.height = 64;
    opt.state_dir = PIKMIN_TEST_STATE_DIR;
    opt.guest_ram_end = guest_ram().data() + guest_ram().size();
    if (!rt::gfx_init(opt)) return false;
    AuroraSetDrawsBlocking(true);
    return true;
}

#define DEFINE_GFX_TEST_LOOKUP                                       \
    extern "C" ppc_fn ppc_lookup(uint32_t addr) {                    \
        std::fprintf(stderr, "unexpected guest call %08x\n", addr);  \
        std::_Exit(2);                                               \
    }
