// Guest -> aurora argument conversion for the GX shims.
#pragma once
#include <dolphin/gx.h>

#include <cstring>

#include "rt/gfx.hpp"
#include "rt/guest.hpp"

namespace rt::gx {

inline GXColor color_at(ppc_ctx* c, uint32_t ea) {  // GXColor arguments arrive as a pointer to a copy
    return GXColor{rd8(c, ea), rd8(c, ea + 1), rd8(c, ea + 2), rd8(c, ea + 3)};
}

inline GXColorS10 color_s10_at(ppc_ctx* c, uint32_t ea) {
    return GXColorS10{static_cast<s16>(rd16(c, ea)), static_cast<s16>(rd16(c, ea + 2)),
                      static_cast<s16>(rd16(c, ea + 4)), static_cast<s16>(rd16(c, ea + 6))};
}

inline void floats_at(ppc_ctx* c, uint32_t ea, float* out, int n) {
    for (int i = 0; i < n; ++i) {
        uint32_t u = rd32(c, ea + 4 * static_cast<uint32_t>(i));
        std::memcpy(&out[i], &u, 4);
    }
}

inline void* host_ptr(ppc_ctx* c, uint32_t ea) { return ea ? mem_host(c, ea) : nullptr; }

}  // namespace rt::gx

// Shim prologue: breadcrumb + safe point, nothing at all when graphics are inactive (headless), and
// close any draw the guest left open (GXEnd is an empty inline on the GameCube).
#define GX_SHIM(c, name)                         \
    HLE_ENTRY(c, name);                          \
    if (!::rt::gfx_active()) return;             \
    ::rt::gx_close_open_draw()
