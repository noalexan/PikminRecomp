// aurora builds, finds a GPU and renders: a cleared frame reads back with the clear colour.
#include "gfx_test_util.hpp"

int main() {
    if (!gfx_test_init(64, 64)) {
        std::fprintf(stderr, "skip: no GPU adapter\n");
        return 77;
    }
    std::printf("backend %d\n", static_cast<int>(aurora_get_backend()));
    GXSetCopyClear(GXColor{255, 0, 128, 255}, GX_MAX_Z24);
    GXCopyDisp(nullptr, GX_TRUE);  // the copy clears the EFB with the copy-clear colour
    aurora_end_frame();
    aurora_update();
    aurora_begin_frame();
    auto px = gfx_test_capture();
    uint32_t c = pixel(px, g_test_w / 2, g_test_h / 2);
    std::printf("centre pixel %08x (%ux%u)\n", c, g_test_w, g_test_h);
    if (c != 0xFF0080FFu) {
        std::fprintf(stderr, "expected ff0080ff\n");
        return 1;
    }
    std::puts("ok");
    std::fflush(stdout);
    std::_Exit(0);  // skip aurora_shutdown: it can block on the headless surface
}
