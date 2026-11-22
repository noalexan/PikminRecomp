// GXSetArray with size 0 ("unknown", as the GameCube SDK never passes a size): aurora must infer the
// extent from the indices each draw uses.
#include "gfx_test_util.hpp"

alignas(32) static uint8_t g_pos[4 * 12];   // 4 vertices, big-endian f32 XYZ
alignas(32) static uint8_t g_col[4 * 4];    // 4 colours, RGBA8

static void set_square() {
    const float xy[4][2] = {{0, 0}, {kEfbW, 0}, {kEfbW, kEfbH}, {0, kEfbH}};
    for (int i = 0; i < 4; ++i) {
        put_bef(&g_pos[i * 12 + 0], xy[i][0]);
        put_bef(&g_pos[i * 12 + 4], xy[i][1]);
        put_bef(&g_pos[i * 12 + 8], -0.5f);
    }
}

static void set_colours(uint32_t rgba) {
    for (int i = 0; i < 4; ++i) put_be32(&g_col[i * 4], rgba);
}

static void setup_indexed() {
    gfx_test_setup_2d(kEfbW, kEfbH);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxDesc(GX_VA_CLR0, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetArray(GX_VA_POS, g_pos, 0, 12, false);  // size unknown
    GXSetArray(GX_VA_CLR0, g_col, 0, 4, false);
    GXInvalidateVtxCache();
}

static void draw(GXPrimitive prim, std::initializer_list<uint8_t> idx) {
    GXBegin(prim, GX_VTXFMT0, static_cast<u16>(idx.size()));
    for (uint8_t i : idx) {
        GXPosition1x8(i);
        GXColor1x8(i);
    }
    GXEnd();
}

static int failures = 0;
static void expect(const char* what, uint32_t got, uint32_t want) {
    if (got != want) {
        std::fprintf(stderr, "%s: got %08x want %08x\n", what, got, want);
        ++failures;
    }
}

int main() {
    if (!gfx_test_init(64, 64)) return 77;
    GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);

    // baseline: direct vertex data with the same pipeline (no arrays involved)
    gfx_test_setup_2d(kEfbW, kEfbH);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    const float sq[4][2] = {{0, 0}, {kEfbW, 0}, {kEfbW, kEfbH}, {0, kEfbH}};
    for (auto& v : sq) {
        GXPosition3f32(v[0], v[1], -0.5f);
        GXColor4u8(0, 0, 255, 255);
    }
    GXEnd();
    auto px0 = gfx_test_capture();
    expect("direct quad centre", pixel(px0, 32, 32), 0x0000FFFF);

    // test_array_size_inferred_from_indices
    set_square();
    set_colours(0xFF0000FF);
    setup_indexed();
    draw(GX_QUADS, {0, 1, 2, 3});
    auto px = gfx_test_capture();
    expect("quad centre", pixel(px, 32, 32), 0xFF0000FF);

    // test_unknown_size_extent_grows: the second draw needs index 3, beyond the first draw's extent
    set_colours(0x00FF00FF);
    setup_indexed();
    draw(GX_TRIANGLES, {0, 1, 2});  // upper-right half
    draw(GX_TRIANGLES, {0, 2, 3});  // lower-left half
    px = gfx_test_capture();
    expect("upper-right triangle", pixel(px, 48, 12), 0x00FF00FF);
    expect("lower-left triangle", pixel(px, 12, 48), 0x00FF00FF);

    if (failures) gfx_test_exit(1);
    std::puts("ok");
    gfx_test_exit(0);
}
