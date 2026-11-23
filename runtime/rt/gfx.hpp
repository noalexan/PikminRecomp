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
    bool aspect_4_3 = false;        // pillarbox the 640x480 image instead of filling the window (Hor+)
    std::string state_dir;          // aurora settings/caches; required (never defaults into $HOME silently)
    const void* guest_ram_end = nullptr;  // bound for inferred vertex array extents
};

// Returns false (after logging one line) if no usable GPU adapter is found.
bool gfx_init(const GfxOptions& opt);
bool gfx_active();
// Size of the presented image in pixels (window framebuffer or offscreen surface); false if inactive.
bool gfx_output_size(uint32_t& w, uint32_t& h);
// Refresh rate of the display showing the window (0 if unknown or offscreen).
int gfx_display_refresh();
// End the frame, handle window events, begin the next frame. Called at each retrace.
void gfx_present();
// Handle window events between presents (called at each retrace).
void gfx_pump_events();
// Capture the frame presented as present number `frame` (1-based) and write a binary PPM to `path`.
void gfx_request_screenshot(uint32_t frame, std::string path);
// Live settings (no-ops without a window). F11 toggles fullscreen; the callback hears about it.
void gfx_set_fullscreen(bool on);
bool gfx_fullscreen();
void gfx_set_vsync(bool on);
void gfx_set_aspect_4_3(bool on);
void gfx_on_fullscreen_toggled(void (*cb)(bool fullscreen));
// Closes a GXBegin left open by guest code (real implementation in the GX shims).
void gx_close_open_draw();
// Per-frame counters for PIKMIN_GFX_STATS=1 diagnostics.
struct GxCounters { uint32_t set_array = 0, invalidate_vtx = 0, begin = 0, call_dl = 0; };
GxCounters& gx_counters();

}  // namespace rt
