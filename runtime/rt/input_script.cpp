#include "rt/input_script.hpp"

#include <map>
#include <sstream>

namespace rt {
namespace {
std::vector<PadScriptEntry> g_script;

const std::map<std::string, uint16_t>& button_names() {
    static const std::map<std::string, uint16_t> m = {
        {"LEFT", 0x0001}, {"RIGHT", 0x0002}, {"DOWN", 0x0004}, {"UP", 0x0008}, {"Z", 0x0010}, {"R", 0x0020},
        {"L", 0x0040},    {"A", 0x0100},     {"B", 0x0200},    {"X", 0x0400},  {"Y", 0x0800}, {"START", 0x1000},
    };
    return m;
}
}  // namespace

PadScriptParse parse_input_script(const std::string& text) {
    PadScriptParse out;
    std::istringstream in(text);
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream ls(line);
        std::string frame_s, buttons_s;
        if (!(ls >> frame_s)) continue;
        PadScriptEntry e;
        try {
            e.frame = static_cast<uint32_t>(std::stoul(frame_s));
        } catch (...) {
            out.error = "input script line " + std::to_string(lineno) + ": bad frame '" + frame_s + "'";
            return out;
        }
        if (!(ls >> buttons_s)) {
            out.error = "input script line " + std::to_string(lineno) + ": missing buttons";
            return out;
        }
        if (buttons_s != "-") {
            std::istringstream bs(buttons_s);
            std::string name;
            while (std::getline(bs, name, '+')) {
                auto it = button_names().find(name);
                if (it == button_names().end()) {
                    out.error = "input script line " + std::to_string(lineno) + ": unknown button '" + name + "'";
                    return out;
                }
                e.buttons |= it->second;
            }
        }
        int sx = 0, sy = 0;
        if (ls >> sx >> sy) {
            e.stick_x = static_cast<int8_t>(sx);
            e.stick_y = static_cast<int8_t>(sy);
        }
        out.entries.push_back(e);
    }
    return out;
}

void set_input_script(std::vector<PadScriptEntry> entries) { g_script = std::move(entries); }

std::optional<PadScriptEntry> scripted_pad(uint32_t frame) {
    std::optional<PadScriptEntry> cur;
    for (const auto& e : g_script)
        if (e.frame <= frame) cur = e;
    return cur;
}

}  // namespace rt
