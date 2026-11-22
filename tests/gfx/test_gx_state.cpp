// hle_GX* state shims: big-endian guest data (colours by pointer, matrices, lists) reaches aurora correctly.
#include "gx_shim_test_util.hpp"

DEFINE_GFX_TEST_LOOKUP

extern "C" {
void hle_GXInit(ppc_ctx*);
void hle_GXSetCopyClear(ppc_ctx*);
void hle_GXSetProjection(ppc_ctx*);
void hle_GXLoadPosMtxImm(ppc_ctx*);
void hle_GXSetCurrentMtx(ppc_ctx*);
void hle_GXSetViewport(ppc_ctx*);
void hle_GXSetTevColorIn(ppc_ctx*);
void hle_GXSetTevColor(ppc_ctx*);
void hle_GXSetVtxDescv(ppc_ctx*);
void hle_GXClearVtxDesc(ppc_ctx*);
void hle_GXSetBlendMode(ppc_ctx*);
}

static int failures = 0;
static void expect(const char* what, uint32_t got, uint32_t want) {
    if (got != want) {
        std::fprintf(stderr, "%s: got %08x want %08x\n", what, got, want);
        ++failures;
    }
}

// The runtime emulates the XFB (as the game expects): what is presented is the last GXCopyDisp.
static void cycle_frame() {
    GXCopyDisp(guest_ram().data() + 0x00600000, GX_TRUE);
    aurora_end_frame();
    aurora_update();
    aurora_begin_frame();
}

// Full-EFB quad (or a given rectangle) with direct positions and one colour, via aurora's own API.
static void draw_rect(float x0, float y0, float x1, float y1, uint32_t rgba) {
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    const float v[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (auto& p : v) {
        GXPosition3f32(p[0], p[1], -0.5f);
        GXColor4u8(rgba >> 24, rgba >> 16, rgba >> 8, rgba);
    }
    GXEnd();
}

int main() {
    ppc_ctx& c = make_guest_ctx();

    // test_shims_inert_when_inactive: before graphics start, shims do nothing (and do not crash)
    gputrgba(0x80300000, 0x11223344);
    gcall(hle_GXSetCopyClear, &c, {0x80300000, 0x00FFFFFF});
    gcall(hle_GXSetBlendMode, &c, {0, 1, 0, 0});

    if (!shim_test_init()) return 77;
    gcall(hle_GXInit, &c, {0x80500000, 0x10000});

    // test_copy_clear_color_by_pointer
    gputrgba(0x80300000, 0x204060FF);
    gcall(hle_GXSetCopyClear, &c, {0x80300000, 0x00FFFFFF});
    cycle_frame();
    cycle_frame();  // the first copy clears; the second frame shows the cleared EFB
    auto px = gfx_test_capture();
    expect("copy clear", pixel(px, 32, 32), 0x204060FF);

    // test_projection_and_pos_mtx_big_endian: quad drawn over the left half, translated to the right half
    gputrgba(0x80300000, 0x000000FF);
    gcall(hle_GXSetCopyClear, &c, {0x80300000, 0x00FFFFFF});
    cycle_frame();  // the new clear colour applies from the next frame
    gfx_test_setup_2d(kEfbW, kEfbH);
    const float proj[16] = {2.0f / kEfbW, 0, 0, -1, 0, -2.0f / kEfbH, 0, 1, 0, 0, -1, -1, 0, 0, 0, 1};
    gputmtx(0x80300100, proj, 16);
    gcall(hle_GXSetProjection, &c, {0x80300100, GX_ORTHOGRAPHIC});
    const float pos[12] = {1, 0, 0, kEfbW / 2, 0, 1, 0, 0, 0, 0, 1, 0};
    gputmtx(0x80300200, pos, 12);
    gcall(hle_GXLoadPosMtxImm, &c, {0x80300200, GX_PNMTX0});
    gcall(hle_GXSetCurrentMtx, &c, {GX_PNMTX0});
    gcall(hle_GXSetViewport, &c, {}, {0, 0, kEfbW, kEfbH, 0, 1});
    draw_rect(0, 0, kEfbW / 2, kEfbH, 0xFFFF00FF);
    px = gfx_test_capture();
    expect("left half (clear)", pixel(px, 12, 32), 0x000000FF);
    expect("right half (moved quad)", pixel(px, 52, 32), 0xFFFF00FF);

    // test_tev_color_by_pointer: TEV stage 0 outputs C0, set from a guest colour
    gfx_test_setup_2d(kEfbW, kEfbH);
    gcall(hle_GXSetTevColorIn, &c, {GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0});
    gputrgba(0x80300300, 0x8040C0FF);
    gcall(hle_GXSetTevColor, &c, {GX_TEVREG0, 0x80300300});
    draw_rect(0, 0, kEfbW, kEfbH, 0x000000FF);
    px = gfx_test_capture();
    expect("tev C0", pixel(px, 32, 32) & 0xFFFFFF00, 0x8040C000);

    // test_vtx_desc_list_conversion: {POS DIRECT, CLR0 DIRECT, NULL} from guest memory
    gfx_test_setup_2d(kEfbW, kEfbH);
    gput32(0x80300400, GX_VA_POS);
    gput32(0x80300404, GX_DIRECT);
    gput32(0x80300408, GX_VA_CLR0);
    gput32(0x8030040C, GX_DIRECT);
    gput32(0x80300410, GX_VA_NULL);
    gput32(0x80300414, GX_NONE);
    gcall(hle_GXClearVtxDesc, &c, {});
    gcall(hle_GXSetVtxDescv, &c, {0x80300400});
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    const float v[4][2] = {{0, 0}, {kEfbW, 0}, {kEfbW, kEfbH}, {0, kEfbH}};
    for (auto& p : v) {
        GXPosition3f32(p[0], p[1], -0.5f);
        GXColor4u8(0, 255, 255, 255);
    }
    GXEnd();
    px = gfx_test_capture();
    expect("desc list quad", pixel(px, 32, 32), 0x00FFFFFF);

    if (failures) gfx_test_exit(1);
    std::puts("ok");
    gfx_test_exit(0);
}
