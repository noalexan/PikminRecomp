#include "ui/settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "enhance/enhance.hpp"
#include "rt/config.hpp"
#include "rt/gfx.hpp"
#include "rt/log.hpp"

namespace ui {
namespace {
std::string g_path;
bool g_fullscreen = true, g_vsync = true, g_window = false;
std::set<Setting> g_dirty;

const char* key(Setting s) {
    switch (s) {
        case Setting::Window: return "fullscreen";
        case Setting::Aspect: return "aspect";
        case Setting::Vsync: return "vsync";
        case Setting::Fps: return "target_fps";
        case Setting::Camera: return "camera";
        case Setting::Hud: return "hud_scale";
    }
    return "?";
}

// The config.toml value of the current state.
std::string toml(Setting s) {
    const auto& e = enh::settings();
    switch (s) {
        case Setting::Window: return g_fullscreen ? "true" : "false";
        case Setting::Aspect: return e.aspect > 0.f ? "\"4:3\"" : "\"auto\"";
        case Setting::Vsync: return g_vsync ? "true" : "false";
        case Setting::Fps: return e.target_fps < 0 ? "\"original\"" : e.target_fps == 0 ? "\"auto\"" : std::to_string(e.target_fps);
        case Setting::Camera: return e.modern_camera ? "\"modern\"" : "\"original\"";
        case Setting::Hud: {
            char b[16];
            std::snprintf(b, sizeof b, "%.1f", e.hud_scale);
            return b;
        }
    }
    return {};
}

void fullscreen_toggled(bool on) {  // F11
    g_fullscreen = on;
    g_dirty.insert(Setting::Window);
    save();
}
}  // namespace

int choices(Setting s) {
    switch (s) {
        case Setting::Fps: return 3;
        case Setting::Hud: return 11;
        default: return 2;
    }
}

int min_value(Setting s) { return s == Setting::Hud ? 2 : 0; }

int value(Setting s) {
    const auto& e = enh::settings();
    switch (s) {
        case Setting::Window: return g_fullscreen ? 0 : 1;
        case Setting::Aspect: return e.aspect > 0.f ? 1 : 0;
        case Setting::Vsync: return g_vsync ? 0 : 1;
        case Setting::Fps: return e.target_fps == 0 ? 2 : e.target_fps < 0 || e.target_fps <= 40 ? 0 : 1;
        case Setting::Camera: return e.modern_camera ? 0 : 1;
        case Setting::Hud: return std::clamp(static_cast<int>(std::lround(e.hud_scale * 10.f)), 2, 10);
    }
    return 0;
}

void set(Setting s, int v) {
    v = std::clamp(v, min_value(s), choices(s) - 1);
    auto& e = enh::settings();
    switch (s) {
        case Setting::Window:
            g_fullscreen = v == 0;
            rt::gfx_set_fullscreen(g_fullscreen);
            break;
        case Setting::Aspect:
            e.aspect = v == 1 ? 4.f / 3.f : 0.f;
            rt::gfx_set_aspect_4_3(v == 1);
            break;
        case Setting::Vsync:
            g_vsync = v == 0;
            rt::gfx_set_vsync(g_vsync);
            e.window_vsync = g_window && g_vsync && rt::gfx_display_refresh() > 0;
            break;
        case Setting::Fps: e.target_fps = v == 0 ? -1 : v == 1 ? 60 : 0; break;
        case Setting::Camera: e.modern_camera = v == 0; break;
        case Setting::Hud: e.hud_scale = static_cast<float>(v) / 10.f; break;
    }
    g_dirty.insert(s);
    rt::log("settings", "%s = %s", key(s), toml(s).c_str());
}

void save() {
    if (g_dirty.empty() || g_path.empty()) return;
    for (Setting s : g_dirty)
        if (!rt::save_config_value(g_path, key(s), toml(s))) rt::log("settings", "cannot write %s", g_path.c_str());
    g_dirty.clear();
}

void init(const std::string& config_path, bool fullscreen, bool vsync, bool window) {
    g_path = config_path;
    g_fullscreen = fullscreen;
    g_vsync = vsync;
    g_window = window;
    rt::gfx_on_fullscreen_toggled(fullscreen_toggled);
}

}  // namespace ui
