// Offscreen aurora setup and frame capture for rendering tests. Exit 77 = no GPU (skip).
#pragma once
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

inline uint32_t g_test_w = 0, g_test_h = 0;

inline void gfx_test_log(AuroraLogLevel level, const char* module, const char* msg, unsigned int) {
    static const bool verbose = std::getenv("PIKMIN_TEST_LOG") != nullptr;
    if (level >= LOG_WARNING || verbose) std::fprintf(stderr, "[aurora %d %s] %s\n", level, module, msg);
    if (level == LOG_FATAL) std::abort();
}

inline bool gfx_test_init(uint32_t w, uint32_t h) {
    g_test_w = w;
    g_test_h = h;
    AuroraConfig cfg{};
    cfg.appName = "pikmin-gfx-test";
    cfg.desiredBackend = BACKEND_VULKAN;
    cfg.logCallback = gfx_test_log;
    cfg.emulateXfb = false;
    cfg.mem1Size = 0;
    cfg.mem2Size = 0;
    cfg.windowWidth = w;
    cfg.windowHeight = h;
    cfg.surface.type = AURORA_SURFACE_HEADLESS;
    cfg.surface.width = w;
    cfg.surface.height = h;
    cfg.surface.scale = 1.0f;
    AuroraInfo info = aurora_initialize(0, nullptr, &cfg);
    if (info.backend == BACKEND_NULL) return false;
    aurora_update();
    return aurora_begin_frame();
}

// Captures the frame drawn so far, presents it and returns its RGBA8 pixels (row-major).
inline std::vector<uint32_t> gfx_test_capture() {
    static AuroraReadbackStream stream = AuroraReadbackCreateStream();
    static uint64_t key = 1;
    uint32_t w = 0, h = 0;
    ++key;
    if (!AuroraReadbackCaptureColor(stream, key, &w, &h)) {
        std::fprintf(stderr, "readback capture rejected\n");
        std::exit(1);
    }
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    AuroraReadbackDest dest{key, px.data(), static_cast<u32>(px.size() * 4), GX_FALSE};
    for (int frame = 0; frame < 16 && !dest.found; ++frame) {
        aurora_end_frame();
        aurora_update();
        aurora_begin_frame();
        AuroraGXSync();
        AuroraReadbackGet(stream, &dest, 1);
    }
    if (!dest.found) {
        std::fprintf(stderr, "readback result never arrived\n");
        std::exit(1);
    }
    g_test_w = w;
    g_test_h = h;
    return px;
}

// RGBA as 0xRRGGBBAA, independent of host byte order.
inline uint32_t pixel(const std::vector<uint32_t>& px, uint32_t x, uint32_t y) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&px[static_cast<size_t>(y) * g_test_w + x]);
    return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
