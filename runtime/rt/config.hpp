// User configuration: $XDG_CONFIG_HOME/pikminrecomp/config.toml (a small TOML subset).
#pragma once
#include <string>
#include <vector>

namespace rt {

struct Config {
    std::string disc;
    int language = 0;
    bool headless = false;
    int window_width = 1920;
    int window_height = 1440;
    bool fullscreen = true;  // user preference: fullscreen by default
    bool vsync = true;
    bool audio = true;  // sound output (never with --headless or --fast)
    // Enhancements. aspect: 0 = follow the window (Hor+ widescreen), 4/3 = original (pillarboxed).
    float aspect = 0.f;
    // Game frames per second: 0 = display refresh rate, -1 = original (PAL VI pacing).
    int target_fps = 0;
    float hud_scale = 0.5f;  // gameplay HUD size relative to the original (anchored to screen edges)
    bool modern_camera = true;  // right stick orbits/tilts the camera; modifier + right stick swarms
    std::vector<std::string> warnings;
};

Config parse_config(const std::string& text);
// "auto" -> 0 (follow the window), "4:3" -> 4/3, invalid -> -1.
float parse_aspect(const std::string& v);
// "auto" -> 0, "original" -> -1, 20..1000 -> value, invalid -> -2.
int parse_fps(const std::string& v);
std::string default_config_path();
// Missing file -> defaults (no error).
Config load_config(const std::string& path);

}  // namespace rt
