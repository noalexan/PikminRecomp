// Graphics via aurora: window or offscreen surface, presenting at each VI retrace, screenshots.
#pragma once
#include <cstdint>
#include <string>

namespace rt {

struct GfxOptions {
    bool headless_surface = false;  // offscreen surface on the real GPU (tests, screenshots)
    uint32_t width = 1920, height = 1440;
    bool fullscreen = true;
    bool vsync = true;
    std::string state_dir;          // aurora settings/caches; required (never defaults into $HOME silently)
    const void* guest_ram_end = nullptr;  // bound for inferred vertex array extents
};

// Returns false (after logging one line) if no usable GPU adapter is found.
bool gfx_init(const GfxOptions& opt);
bool gfx_active();
// End the frame, handle window events, begin the next frame. Called at each retrace.
void gfx_present();
// Capture the frame presented as present number `frame` (1-based) and write a binary PPM to `path`.
void gfx_request_screenshot(uint32_t frame, std::string path);
// Closes a GXBegin left open by guest code (real implementation in the GX shims).
void gx_close_open_draw();
// Per-frame counters for PIKMIN_GFX_STATS=1 diagnostics.
struct GxCounters { uint32_t set_array = 0, invalidate_vtx = 0, begin = 0, call_dl = 0; };
GxCounters& gx_counters();

}  // namespace rt
