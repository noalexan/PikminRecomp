// Frame-stepped effects at high frame rates.
//
// Particles (zen::particleManager, 3D effects and the 2D EffectMgr2D). particleManager::update
// scales particle life by the frame time, but each zen::particleGenerator update also adds the
// velocity (in units per original frame) to the position, applies drag and acceleration, emits
// (SetPtclsLife adds a per-frame rate) and drops trail children on whole-frame ages, all once per
// call. At 240 fps particles flew ~10x farther in their lifetime, emitters spawned ~10x as many and
// trails turned into dense dotted lines. The whole manager update now runs once per elapsed
// original (PAL) frame with the original frame time (enh::orig_frame_steps, shared with the
// other stepped systems), so effects match the original. Drawing extrapolates each particle by
// velocity x the elapsed fraction of the next original frame, so motion stays smooth.
//
// Menu cursor sparkle trail. zen::SpectrumCursorMgr::update moves the head by the frame time but
// shifts the trail (pane i takes pane i-1's position) on every call: at 240 fps the trail was
// squashed into overlapping balls. The shift now happens once per original frame (on other calls
// the trail length is set to 1 for the call, so only the head moves).
#include <cstring>

#include "enhance/enhance.hpp"
#include "rt/guest.hpp"

extern "C" {
__attribute__((weak)) void f_8019fa44_orig(ppc_ctx* c);  // zen::particleManager::update()
__attribute__((weak)) void f_8019b7f4_orig(ppc_ctx* c);  // zen::particleGenerator::draw(Graphics&)
__attribute__((weak)) void f_801d8f68_orig(ppc_ctx* c);  // zen::SpectrumCursorMgr::update()
}

namespace {
// zen::particleGenerator: +0x28 list of live particles (sentinel node; node +8 = next).
// zen::particleMdl: +0xc position, +0x34 velocity (per original frame), +0x58 rotation (s16),
// +0x5a rotation speed (s16, per original frame).
constexpr uint32_t kGenParticles = 0x28, kNext = 0x8;
constexpr uint32_t kPos = 0xc, kVel = 0x34, kRot = 0x58, kRotSpeed = 0x5a;

float rdf(ppc_ctx* c, uint32_t ea) {
    uint32_t u = rt::rd32(c, ea);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
void wrf(ppc_ctx* c, uint32_t ea, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    rt::wr32(c, ea, u);
}

// Moves every live particle of `gen` by `t` original frames of its current velocity and spin
// (t < 0 undoes it). Positions are restored exactly by the caller-saved copies, not by -t.
struct Saved {
    uint32_t p;
    uint32_t pos[3];
    uint16_t rot;
};
constexpr size_t kMaxSaved = 4096;
Saved g_saved[kMaxSaved];

size_t extrapolate(ppc_ctx* c, uint32_t gen, float t) {
    const uint32_t head = rt::rd32(c, gen + kGenParticles);
    if (!head) return 0;
    size_t n = 0;
    for (uint32_t p = rt::rd32(c, head + kNext); p && p != head && n < kMaxSaved; p = rt::rd32(c, p + kNext)) {
        Saved& s = g_saved[n++];
        s.p = p;
        for (uint32_t i = 0; i < 3; ++i) {
            s.pos[i] = rt::rd32(c, p + kPos + 4 * i);
            wrf(c, p + kPos + 4 * i, rdf(c, p + kPos + 4 * i) + t * rdf(c, p + kVel + 4 * i));
        }
        s.rot = rt::rd16(c, p + kRot);
        const auto speed = static_cast<int16_t>(rt::rd16(c, p + kRotSpeed));
        rt::wr16(c, p + kRot, static_cast<uint16_t>(s.rot + static_cast<int>(speed * t)));
    }
    return n;
}
}  // namespace

extern "C" {

void hle_update__Q23zen15particleManagerFv(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::particleManager::update");
    if (!f_8019fa44_orig) return;
    const uint32_t self = rt::arg(c, 0);
    if (enh::original_timing()) {
        f_8019fa44_orig(c);
        return;
    }
    const uint32_t gsys = rt::rd32(c, enh::kGsysPtr);
    const uint32_t ft = gsys + enh::kFrameTimeOff;
    const uint32_t saved = rt::rd32(c, ft);
    wrf(c, ft, enh::orig_frame_dt());
    for (int n = enh::orig_frame_steps(); n > 0; --n) {
        c->r[3] = self;
        f_8019fa44_orig(c);
    }
    rt::wr32(c, ft, saved);
}

void hle_draw__Q23zen17particleGeneratorFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::particleGenerator::draw");
    if (!f_8019b7f4_orig) return;
    float t = enh::orig_frame_phase();
    if (t > 0.999f) t = 0.999f;
    if (t <= 0.f) {
        f_8019b7f4_orig(c);
        return;
    }
    const uint32_t gen = rt::arg(c, 0);
    const size_t n = extrapolate(c, gen, t);
    f_8019b7f4_orig(c);
    for (size_t i = 0; i < n; ++i) {
        const Saved& s = g_saved[i];
        for (uint32_t k = 0; k < 3; ++k) rt::wr32(c, s.p + kPos + 4 * k, s.pos[k]);
        rt::wr16(c, s.p + kRot, s.rot);
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
