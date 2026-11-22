// Enhancements: widescreen (Hor+), anchored/scaled HUD, high framerate, right-stick camera.
// Game functions are patched through "hook" entries in recomp/overrides.toml: the host hle_<name>
// runs instead of the guest function and can call the original translation f_<addr>_orig.
#pragma once
#include <cstdint>

extern "C" {
#include "ppc.h"
}

namespace enh {

struct Settings {
    float aspect = 0.f;         // 0 = follow the window (Hor+), 4/3 = original (pillarboxed by aurora)
    int target_fps = 0;         // 0 = display refresh, -1 = original PAL VI pacing, else a cap
    float hud_scale = 0.5f;     // gameplay HUD size, anchored to the screen edges
    bool modern_camera = true;  // right stick = camera; swarm = modifier + right stick
    bool window_vsync = false;  // presenting to a vsync'd window (vsync paces frames)
    int display_hz = 60;        // refresh rate of the display (or 60 offscreen)
};
Settings& settings();

// Effective aspect ratio of the 3D view (window aspect, or 4/3).
float view_aspect();
// Game frames per second in the unlocked mode (target_fps or the display refresh).
int effective_fps();
inline bool original_timing() { return settings().target_fps < 0; }

// Hooks from the GX shims (graphics builds only).
void on_set_projection(float m[16], uint32_t type);  // adjusts 2D projections in place
bool on_set_scissor(uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h);
void on_set_viewport(float x, float y, float w, float h);

// PAD: splits the right stick between the camera and (with the modifier held) the guest C-stick.
// `status` is the guest PADStatus of port 0.
void route_right_stick(ppc_ctx* c, uint32_t status, bool swarm_modifier);

// Guest globals used by the patches (PAL GPIP01 addresses, from build.map).
constexpr uint32_t kGsysPtr = 0x803ec9ccu;  // System* gsys
constexpr uint32_t kFrameTimeOff = 0x290;  // float System::mFrameTime

}  // namespace enh
