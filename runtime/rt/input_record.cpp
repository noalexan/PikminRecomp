#include "rt/input_record.hpp"

#include <cstdio>

#include "rt/input_script.hpp"

namespace rt {
namespace {
std::FILE* g_file = nullptr;
std::string g_path;
bool g_have_last = false;
uint16_t g_buttons = 0;
int8_t g_sx = 0, g_sy = 0, g_cx = 0, g_cy = 0;
bool g_swarm = false;
}  // namespace

bool input_record_open(const std::string& path, const std::string& header) {
    g_file = std::fopen(path.c_str(), "w");
    if (!g_file) return false;
    g_path = path;
    std::fprintf(g_file, "# %s\n", header.c_str());
    std::fflush(g_file);
    return true;
}

void input_record(uint32_t retrace, uint16_t buttons, int8_t sx, int8_t sy, int8_t cx, int8_t cy, bool swarm) {
    if (!g_file) return;
    if (g_have_last && buttons == g_buttons && sx == g_sx && sy == g_sy && cx == g_cx && cy == g_cy &&
        swarm == g_swarm) {
        return;
    }
    g_have_last = true;
    g_buttons = buttons;
    g_sx = sx, g_sy = sy, g_cx = cx, g_cy = cy;
    g_swarm = swarm;
    std::fprintf(g_file, "%u %s %d %d %d %d\n", retrace, format_buttons(buttons, swarm).c_str(), sx, sy, cx, cy);
    std::fflush(g_file);
}

const std::string& input_record_path() { return g_path; }

}  // namespace rt
