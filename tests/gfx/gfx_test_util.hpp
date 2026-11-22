// Offscreen aurora setup and frame capture for rendering tests. Exit 77 = no GPU (skip).
#pragma once
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include <cstdio>
#include <cstring>
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
    cfg.userPath = PIKMIN_TEST_STATE_DIR;   // keep aurora's caches/settings in the build tree, not $HOME
    cfg.cachePath = PIKMIN_TEST_STATE_DIR;
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
    if (!aurora_begin_frame()) return false;
    static uint8_t fifo[0x10000];
    GXInit(fifo, sizeof fifo);     // GameCube default GX state, as the game sets it up
    AuroraSetDrawsBlocking(true);  // deterministic: draws wait for their pipeline to compile
    return true;
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

// Ends a test without running static destructors (Dawn's pipeline workers may still be running).
[[noreturn]] inline void gfx_test_exit(int code) {
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(code);
}

// RGBA as 0xRRGGBBAA, independent of host byte order.
inline uint32_t pixel(const std::vector<uint32_t>& px, uint32_t x, uint32_t y) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&px[static_cast<size_t>(y) * g_test_w + x]);
    return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

// The GX logical framebuffer is the game's 640x480 (scaled by aurora to the window/surface size).
constexpr float kEfbW = 640.0f, kEfbH = 480.0f;

// Minimal pipeline for flat-colour 2D drawing: ortho projection in EFB pixel coordinates (origin top-left), identity position matrix, vertex colour passed straight through.
inline void gfx_test_setup_2d(float w, float h) {
    float proj[4][4] = {};
    proj[0][0] = 2.0f / w;
    proj[0][3] = -1.0f;
    proj[1][1] = -2.0f / h;
    proj[1][3] = 1.0f;
    proj[2][2] = -1.0f;
    proj[2][3] = -1.0f;
    proj[3][3] = 1.0f;
    GXSetProjection(proj, GX_ORTHOGRAPHIC);
    float ident[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    GXLoadPosMtxImm(ident, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetViewport(0, 0, w, h, 0, 1);
    GXSetScissor(0, 0, static_cast<u32>(w), static_cast<u32>(h));
    GXSetCullMode(GX_CULL_NONE);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

// Big-endian helpers for building GameCube-format data in host buffers.
inline void put_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8); p[3] = static_cast<uint8_t>(v);
}
inline void put_bef(uint8_t* p, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put_be32(p, u);
}
