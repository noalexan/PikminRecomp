// Frame-stepped 2D effects at high frame rates.
//
// Particle emission. zen::particleGenerator::update moves particles by the frame
// time, but SetPtclsLife (emission) adds a per-frame rate to an accumulator and spawns its whole
// part on every call: at 240 fps emitters (title-screen sparkles, in-game effects) spawned ~10x the
// particles. The original now runs once per elapsed original (PAL) frame (enh::orig_frame_steps),
// so the rate and its pattern match the original.
//
// Menu cursor sparkle trail. zen::SpectrumCursorMgr::update moves the head by the frame time but
// shifts the trail (pane i takes pane i-1's position) on every call: at 240 fps the trail was
// squashed into overlapping balls. The shift now happens once per original frame (on other calls
// the trail length is set to 1 for the call, so only the head moves).
#include "enhance/enhance.hpp"
#include "rt/guest.hpp"

extern "C" {
__attribute__((weak)) void f_8019c4c4_orig(ppc_ctx* c);  // zen::particleGenerator::SetPtclsLife()
__attribute__((weak)) void f_801d8f68_orig(ppc_ctx* c);  // zen::SpectrumCursorMgr::update()

void hle_SetPtclsLife__Q23zen17particleGeneratorFv(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::particleGenerator::SetPtclsLife");
    if (!f_8019c4c4_orig) return;
    const uint32_t self = rt::arg(c, 0);
    for (int n = enh::orig_frame_steps(); n > 0; --n) {
        c->r[3] = self;
        f_8019c4c4_orig(c);
    }
}

void hle_update__Q23zen17SpectrumCursorMgrFv(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::SpectrumCursorMgr::update");
    if (!f_801d8f68_orig) return;
    const uint32_t self = rt::arg(c, 0);
    const bool shift = enh::orig_frame_steps() > 0;
    const uint32_t count = rt::rd32(c, self);  // +0: number of panes in the trail (head first)
    if (!shift) rt::wr32(c, self, 1);
    f_801d8f68_orig(c);
    if (!shift) rt::wr32(c, self, count);
}

}  // extern "C"
