#include "rt/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "rt/platform.hpp"

namespace rt {
namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
}  // namespace

float parse_aspect(const std::string& v) {
    if (v == "auto") return 0.f;
    if (v == "4:3") return 4.f / 3.f;
    return -1.f;
}

int parse_fps(const std::string& v) {
    if (v == "auto") return 0;
    if (v == "original") return -1;
    char* end = nullptr;
    long f = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || *end || f < 20 || f > 1000) return -2;
    return static_cast<int>(f);
}

Config parse_config(const std::string& text) {
    Config cfg;
    std::istringstream in(text);
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        // strip comments outside quotes
        bool quoted = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"') quoted = !quoted;
            if (line[i] == '#' && !quoted) {
                line.resize(i);
                break;
            }
        }
        line = trim(line);
        if (line.empty()) continue;
        size_t eq = line.find('=');
        auto warn = [&](const std::string& msg) {
            cfg.warnings.push_back("config line " + std::to_string(lineno) + ": " + msg);
        };
        if (eq == std::string::npos) {
            warn("expected key = value");
            continue;
        }
        std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        bool is_str = value.size() >= 2 && value.front() == '"' && value.back() == '"';
        if (key == "disc") {
            if (is_str) cfg.disc = value.substr(1, value.size() - 2);
            else warn("disc must be a quoted string");
        } else if (key == "language") {
            char* end = nullptr;
            long v = std::strtol(value.c_str(), &end, 10);
            if (!is_str && end && *end == '\0' && v >= 0 && v <= 5) cfg.language = static_cast<int>(v);
            else warn("language must be an integer 0-5");
        } else if (key == "headless" || key == "fullscreen" || key == "vsync" || key == "audio") {
            bool* dst = key == "headless"     ? &cfg.headless
                        : key == "fullscreen" ? &cfg.fullscreen
                        : key == "audio"      ? &cfg.audio
                                              : &cfg.vsync;
            if (value == "true" || value == "false") *dst = value == "true";
            else warn(key + " must be true or false");
        } else if (key == "window_width" || key == "window_height") {
            char* end = nullptr;
            long v = std::strtol(value.c_str(), &end, 10);
            if (!is_str && end && *end == '\0' && v >= 320 && v <= 16384) {
                (key == "window_width" ? cfg.window_width : cfg.window_height) = static_cast<int>(v);
            } else {
                warn(key + " must be an integer 320-16384");
            }
        } else if (key == "aspect") {
            std::string v = is_str ? value.substr(1, value.size() - 2) : value;
            float a = parse_aspect(v);
            if (a >= 0.f) cfg.aspect = a;
            else warn("aspect must be \"auto\" or \"4:3\"");
        } else if (key == "target_fps") {
            std::string v = is_str ? value.substr(1, value.size() - 2) : value;
            int f = parse_fps(v);
            if (f >= -1) cfg.target_fps = f;
            else warn("target_fps must be \"auto\", \"original\" or an integer 20-1000");
        } else if (key == "hud_scale") {
            char* end = nullptr;
            float v = std::strtof(value.c_str(), &end);
            if (!is_str && end && *end == '\0' && v >= 0.2f && v <= 1.5f) cfg.hud_scale = v;
            else warn("hud_scale must be a number 0.2-1.5");
        } else if (key == "debug_hud") {
            if (value == "true" || value == "false") cfg.debug_hud = value == "true";
            else warn("debug_hud must be true or false");
        } else if (key == "camera") {
            std::string v = is_str ? value.substr(1, value.size() - 2) : value;
            if (v == "modern" || v == "original") cfg.modern_camera = v == "modern";
            else warn("camera must be \"modern\" or \"original\"");
        } else if (key == "cutscenes") {
            std::string v = is_str ? value.substr(1, value.size() - 2) : value;
            if (v == "fill" || v == "wide") cfg.cutscene_fill = v == "fill";
            else warn("cutscenes must be \"fill\" or \"wide\"");
        } else {
            warn("unknown key '" + key + "' ignored");
        }
    }
    return cfg;
}

std::string default_config_path() {
    std::string base = plat::user_dir(plat::UserDir::Config);
    if (base.empty()) base = ".";
    return base + "/pikminrecomp/config.toml";
}

Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f) return Config{};
    std::stringstream ss;
    ss << f.rdbuf();
    return parse_config(ss.str());
}

std::string write_config_value(const std::string& text, const std::string& key, const std::string& value) {
    std::string out;
    bool done = false;
    size_t i = 0;
    while (i < text.size()) {
        size_t nl = text.find('\n', i);
        std::string line = text.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? text.size() : nl + 1;
        const size_t eq = line.find('=');
        if (!done && eq != std::string::npos && line.find('#') > eq && trim(line.substr(0, eq)) == key) {
            // Keep "key = " as written and anything from the comment on (with the spaces before it).
            size_t v = eq + 1;
            while (v < line.size() && line[v] == ' ') ++v;
            size_t tail = line.size();
            bool in_str = false;
            for (size_t k = v; k < line.size(); ++k) {
                if (line[k] == '"') in_str = !in_str;
                if (line[k] == '#' && !in_str) {
                    tail = k;
                    while (tail > v && line[tail - 1] == ' ') --tail;
                    break;
                }
            }
            line = line.substr(0, v) + value + line.substr(tail);
            done = true;
        }
        out += line + "\n";
    }
    if (!done) out += key + " = " + value + "\n";
    return out;
}

bool save_config_value(const std::string& path, const std::string& key, const std::string& value) {
    std::string text;
    if (std::ifstream f(path); f) {
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!(f << write_config_value(text, key, value))) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

}  // namespace rt
