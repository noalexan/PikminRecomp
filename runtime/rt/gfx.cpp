#include "rt/gfx.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "rt/log.hpp"

#ifdef PIKMIN_GRAPHICS
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#endif

namespace rt {

__attribute__((weak)) void gx_close_open_draw() {}

GxCounters& gx_counters() {
    static GxCounters c;
    return c;
}

#ifndef PIKMIN_GRAPHICS
bool gfx_init(const GfxOptions&) {
    log("gfx", "built without graphics (PIKMIN_GRAPHICS=OFF)");
    return false;
}
bool gfx_active() { return false; }
void gfx_present() {}
void gfx_pump_events() {}
void gfx_request_screenshot(uint32_t, std::string) {}
#else

namespace {
bool g_active = false;
uint32_t g_presents = 0;
bool g_init_failed = false;
bool g_in_frame = false;
uint32_t g_begin_failures = 0;

struct Screenshot {
    uint32_t frame = 0;
    std::string path;
    bool captured = false;
    uint64_t key = 0;
    uint32_t w = 0, h = 0;
    AuroraReadbackStream stream = 0;
} g_shot;

void aurora_log(AuroraLogLevel level, const char* module, const char* msg, unsigned int) {
    if (level >= LOG_WARNING) log("aurora", "%s %s: %s", level >= LOG_ERROR ? "ERROR" : "warning", module, msg);
    if (level == LOG_FATAL) {
        if (!g_active) {
            // Fatal during initialisation: report it as "no usable GPU" instead of aborting.
            std::fprintf(stderr, "pikmin: no usable GPU (%s); try --headless\n", msg);
            exit_process(2);
        }
        exit_process(134);
    }
}

void write_ppm(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        log("gfx", "cannot write screenshot %s", path.c_str());
        return;
    }
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) std::fwrite(&rgba[i * 4], 1, 3, f);
    std::fclose(f);
    log("gfx", "screenshot %s (%ux%u)", path.c_str(), w, h);
}

void poll_screenshot() {
    if (!g_shot.captured || g_shot.path.empty()) return;
    std::vector<uint8_t> px(static_cast<size_t>(g_shot.w) * g_shot.h * 4);
    AuroraReadbackDest dest{g_shot.key, px.data(), static_cast<u32>(px.size()), GX_FALSE};
    AuroraReadbackGet(g_shot.stream, &dest, 1);
    if (dest.found) {
        write_ppm(g_shot.path, px, g_shot.w, g_shot.h);
        g_shot = Screenshot{0, "", false, 0, 0, 0, g_shot.stream};
    }
}
}  // namespace

bool gfx_init(const GfxOptions& opt) {
    if (opt.state_dir.empty()) {
        log("gfx", "internal error: GfxOptions.state_dir not set");
        return false;
    }
    AuroraConfig cfg{};
    cfg.appName = "Pikmin Recomp";
    cfg.userPath = opt.state_dir.c_str();
    cfg.cachePath = opt.state_dir.c_str();
    cfg.desiredBackend = BACKEND_VULKAN;
    cfg.logCallback = aurora_log;
    cfg.vsync = opt.vsync;
    cfg.startFullscreen = opt.fullscreen && !opt.headless_surface;
    // aurora's XFB emulation presents black on a real window surface (offscreen it is never used);
    // presenting the EFB at each retrace works. PIKMIN_XFB=1 re-enables it for investigation.
    cfg.emulateXfb = std::getenv("PIKMIN_XFB") != nullptr;
    cfg.mem1Size = 0;
    cfg.mem2Size = 0;
    cfg.windowWidth = opt.width;
    cfg.windowHeight = opt.height;
    if (opt.headless_surface) {
        cfg.surface.type = AURORA_SURFACE_HEADLESS;
        cfg.surface.width = opt.width;
        cfg.surface.height = opt.height;
        cfg.surface.scale = 1.0f;
    }
    AuroraInfo info = aurora_initialize(0, nullptr, &cfg);
    if (info.backend == BACKEND_NULL) {
        g_init_failed = true;
        std::fprintf(stderr, "pikmin: no usable GPU (no Vulkan adapter); try --headless\n");
        return false;
    }
    if (opt.guest_ram_end) AuroraSetUnknownArrayLimit(opt.guest_ram_end);
    g_active = true;
    aurora_update();
    g_in_frame = aurora_begin_frame();
    log("gfx", "graphics ready (%ux%u%s)", opt.width, opt.height, opt.headless_surface ? ", offscreen" : "");
    return true;
}

bool gfx_active() { return g_active; }

void gfx_pump_events() {
    if (!g_active) return;
    static uint32_t retraces_since_present = 0;
    static uint32_t last_presents = 0;
    if (g_presents != last_presents) {
        last_presents = g_presents;
        retraces_since_present = 0;
        return;
    }
    // No frame finished for a while (loading, stalls): present anyway so the window stays alive.
    if (++retraces_since_present >= 30) {
        retraces_since_present = 0;
        gfx_present();
        last_presents = g_presents;
    }
}

void gfx_request_screenshot(uint32_t frame, std::string path) {
    g_shot.frame = frame;
    g_shot.path = std::move(path);
}

void gfx_present() {
    if (!g_active) return;
    gx_close_open_draw();
    ++g_presents;
    if (!g_shot.path.empty() && !g_shot.captured && g_presents == g_shot.frame) {
        if (!g_shot.stream) g_shot.stream = AuroraReadbackCreateStream();
        g_shot.key = g_presents;
        g_shot.captured = AuroraReadbackCaptureColor(g_shot.stream, g_shot.key, &g_shot.w, &g_shot.h);
        if (!g_shot.captured) log("gfx", "screenshot capture rejected");
    }
    if (g_in_frame) aurora_end_frame();
    static const bool stats = std::getenv("PIKMIN_GFX_STATS") != nullptr;
    if (stats) {
        const AuroraStats* st = aurora_get_stats();
        auto& k = gx_counters();
        log("gfxstats", "frame %u: storage %u B, draws %u (merged %u), setArray %u, invalVtx %u, begin %u, callDL %u",
            g_presents, st->lastStorageSize, st->drawCallCount, st->mergedDrawCallCount, k.set_array,
            k.invalidate_vtx, k.begin, k.call_dl);
        k = GxCounters{};
    }
    for (const AuroraEvent* ev = aurora_update(); ev && ev->type != AURORA_NONE; ++ev) {
        if (ev->type == AURORA_EXIT) {
            log("gfx", "window closed");
            exit_process(0);
        }
    }
    g_in_frame = aurora_begin_frame();  // false while minimised/pending: keep running, game time must not stall
    if (!g_in_frame && (++g_begin_failures == 1 || g_begin_failures % 500 == 0)) {
        log("gfx", "aurora_begin_frame failed (%u times so far)", g_begin_failures);
    }
    poll_screenshot();
}

#endif

}  // namespace rt
