// hle_GX* geometry shims: inline FIFO writes, display lists, arrays of unknown size, textures, copies.
#include "gx_shim_test_util.hpp"

DEFINE_GFX_TEST_LOOKUP

extern "C" {
void hle_GXInit(ppc_ctx*);
void hle_GXBegin(ppc_ctx*);
void hle_GXSetZMode(ppc_ctx*);
void hle_GXCallDisplayList(ppc_ctx*);
void hle_GXSetArray(ppc_ctx*);
void hle_GXInvalidateVtxCache(ppc_ctx*);
void hle_GXInitTexObj(ppc_ctx*);
void hle_GXInitTexObjLOD(ppc_ctx*);
void hle_GXLoadTexObj(ppc_ctx*);
void hle_GXCopyTex(ppc_ctx*);
void hle_GXCopyDisp(ppc_ctx*);
}

static int failures = 0;
static void expect(const char* what, uint32_t got, uint32_t want) {
    if (got != want) {
        std::fprintf(stderr, "%s: got %08x want %08x\n", what, got, want);
        ++failures;
    }
}

static ppc_ctx* G;
constexpr uint32_t WGPIPE = 0xCC008000u;
static void wg_f32(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    mem_w32(G, WGPIPE, u);
}
static void wg_u8(uint8_t v) { mem_w8(G, WGPIPE, v); }

static void next_frame() {
    gcall(hle_GXCopyDisp, G, {0x80600000, 1});
    aurora_end_frame();
    aurora_update();
    aurora_begin_frame();
}

static void desc_pos_clr(GXAttrType type) {
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, type);
    GXSetVtxDesc(GX_VA_CLR0, type);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

static const float kQuad[4][2] = {{0, 0}, {kEfbW, 0}, {kEfbW, kEfbH}, {0, kEfbH}};

int main() {
    ppc_ctx& c = make_guest_ctx();
    G = &c;
    if (!shim_test_init()) return 77;
    gcall(hle_GXInit, &c, {0x80500000, 0x10000});
    GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
    next_frame();

    // test_inline_draw_closed_by_next_gx_call: vertices written to the write-gather pipe, never GXEnd
    gfx_test_setup_2d(kEfbW, kEfbH);
    desc_pos_clr(GX_DIRECT);
    gcall(hle_GXBegin, &c, {GX_QUADS, GX_VTXFMT0, 4});
    for (auto& v : kQuad) {
        wg_f32(v[0]);
        wg_f32(v[1]);
        wg_f32(-0.5f);
        wg_u8(255); wg_u8(128); wg_u8(0); wg_u8(255);
    }
    gcall(hle_GXSetZMode, &c, {0, GX_ALWAYS, 0});  // next GX call closes the draw
    auto px = gfx_test_capture();
    expect("inline quad", pixel(px, 32, 32), 0xFF8000FF);

    // test_display_list_big_endian
    gfx_test_setup_2d(kEfbW, kEfbH);
    desc_pos_clr(GX_DIRECT);
    {
        uint8_t* dl = &guest_ram()[0x310000];
        std::memset(dl, 0, 128);
        dl[0] = GX_DRAW_QUADS | GX_VTXFMT0;
        dl[1] = 0;
        dl[2] = 4;
        uint8_t* p = dl + 3;
        for (auto& v : kQuad) {
            put_bef(p, v[0]); put_bef(p + 4, v[1]); put_bef(p + 8, -0.5f);
            p[12] = 0; p[13] = 0; p[14] = 255; p[15] = 255;
            p += 16;
        }
        gcall(hle_GXCallDisplayList, &c, {0x80310000, 96});
    }
    px = gfx_test_capture();
    expect("display list quad", pixel(px, 32, 32), 0x0000FFFF);

    // test_indexed_arrays_unknown_size + test_invalidate_vtx_cache_reuploads
    for (int i = 0; i < 4; ++i) {
        gputf(0x80320000 + i * 12, kQuad[i][0]);
        gputf(0x80320000 + i * 12 + 4, kQuad[i][1]);
        gputf(0x80320000 + i * 12 + 8, -0.5f);
        gputrgba(0x80321000 + i * 4, 0xFF0000FF);
    }
    auto draw_indexed = [&] {
        gfx_test_setup_2d(kEfbW, kEfbH);
        desc_pos_clr(GX_INDEX8);
        gcall(hle_GXSetArray, &c, {GX_VA_POS, 0x80320000, 12});
        gcall(hle_GXSetArray, &c, {GX_VA_CLR0, 0x80321000, 4});
        gcall(hle_GXBegin, &c, {GX_QUADS, GX_VTXFMT0, 4});
        for (uint8_t i = 0; i < 4; ++i) {
            wg_u8(i);
            wg_u8(i);
        }
        gcall(hle_GXInvalidateVtxCache, &c, {});  // also closes the draw
    };
    draw_indexed();
    px = gfx_test_capture();
    expect("indexed quad", pixel(px, 32, 32), 0xFF0000FF);
    next_frame();
    for (int i = 0; i < 4; ++i) gputrgba(0x80321000 + i * 4, 0x00FF00FF);  // CPU rewrites the array
    gcall(hle_GXInvalidateVtxCache, &c, {});
    draw_indexed();
    px = gfx_test_capture();
    expect("re-uploaded array", pixel(px, 32, 32), 0x00FF00FF);

    // test_texture_from_guest_memory: 4x4 RGBA8 (one tile: 32 bytes AR pairs, then 32 bytes GB pairs)
    {
        uint8_t* t = &guest_ram()[0x330000];
        for (int i = 0; i < 16; ++i) {
            t[i * 2] = 255; t[i * 2 + 1] = 40;          // A, R
            t[32 + i * 2] = 160; t[32 + i * 2 + 1] = 220;  // G, B
        }
    }
    auto draw_textured = [&](uint32_t texobj) {
        gfx_test_setup_2d(kEfbW, kEfbH);
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GXSetNumTexGens(1);
        GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
        gcall(hle_GXLoadTexObj, &c, {texobj, GX_TEXMAP0});
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        const float st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int i = 0; i < 4; ++i) {
            GXPosition3f32(kQuad[i][0], kQuad[i][1], -0.5f);
            GXTexCoord2f32(st[i][0], st[i][1]);
        }
        GXEnd();
    };
    gcall(hle_GXInitTexObj, &c, {0x80340000, 0x80330000, 4, 4, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, 0});
    gcall(hle_GXInitTexObjLOD, &c, {0x80340000, GX_NEAR, GX_NEAR, 0, 0, GX_ANISO_1}, {0.0, 0.0, 0.0});
    draw_textured(0x80340000);
    px = gfx_test_capture();
    expect("guest texture", pixel(px, 32, 32) & 0xFFFFFF00, 0x28A0DC00);

    // test_copy_tex_then_sample: clear to green, copy to a guest address, sample it next frame
    next_frame();
    GXSetCopyClear(GXColor{0, 255, 0, 255}, GX_MAX_Z24);
    next_frame();  // this frame starts green
    GXSetTexCopySrc(0, 0, 64, 64);
    GXSetTexCopyDst(64, 64, GX_TF_RGBA8, GX_FALSE);
    gcall(hle_GXCopyTex, &c, {0x80350000, 0});
    GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
    next_frame();
    next_frame();  // black background now
    gcall(hle_GXInitTexObj, &c, {0x80360000, 0x80350000, 64, 64, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, 0});
    gcall(hle_GXInitTexObjLOD, &c, {0x80360000, GX_LINEAR, GX_LINEAR, 0, 0, GX_ANISO_1}, {0.0, 0.0, 0.0});
    draw_textured(0x80360000);
    px = gfx_test_capture();
    expect("sampled copy", pixel(px, 32, 32), 0x00FF00FF);

    if (failures) gfx_test_exit(1);
    std::puts("ok");
    gfx_test_exit(0);
}
