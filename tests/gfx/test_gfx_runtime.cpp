// rt::gfx: offscreen init, presents, and a PPM screenshot of a known clear colour.
#include <dolphin/gx.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "rt/gfx.hpp"

int main() {
    rt::GfxOptions opt;
    opt.headless_surface = true;
    opt.width = 64;
    opt.height = 64;
    opt.state_dir = PIKMIN_TEST_STATE_DIR;
    if (!rt::gfx_init(opt)) {
        std::fprintf(stderr, "skip: no GPU\n");
        return 77;
    }
    if (!rt::gfx_active()) {
        std::fprintf(stderr, "gfx_active false after init\n");
        std::_Exit(1);
    }
    const std::string path = std::string(PIKMIN_TEST_STATE_DIR) + "/shot.ppm";
    std::remove(path.c_str());
    rt::gfx_request_screenshot(2, path);
    for (int i = 0; i < 8; ++i) {
        GXSetCopyClear(GXColor{10, 200, 30, 255}, GX_MAX_Z24);
        GXCopyDisp(nullptr, GX_TRUE);
        rt::gfx_present();
    }
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string header = "P6\n64 64\n255\n";
    int rc = 0;
    if (data.size() != header.size() + 64 * 64 * 3 || std::string(data.begin(), data.begin() + header.size()) != header) {
        std::fprintf(stderr, "bad PPM (%zu bytes)\n", data.size());
        rc = 1;
    } else {
        const unsigned char* px = &data[header.size() + (32 * 64 + 32) * 3];
        std::printf("centre %u %u %u\n", px[0], px[1], px[2]);
        if (px[0] != 10 || px[1] != 200 || px[2] != 30) rc = 1;
    }
    if (rc == 0) std::puts("ok");
    std::fflush(stdout);
    std::_Exit(rc);
}
