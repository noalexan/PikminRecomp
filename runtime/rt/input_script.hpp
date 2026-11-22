// Scripted controller input (--input-script): deterministic port-0 state per retrace, for tests.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rt {

struct PadScriptEntry {
    uint32_t frame = 0;   // applies from this retrace on
    uint16_t buttons = 0; // SDK PAD_BUTTON_* mask
    int8_t stick_x = 0, stick_y = 0;
};

struct PadScriptParse {
    std::vector<PadScriptEntry> entries;
    std::string error;  // empty on success
};

// Lines: "FRAME BUTTONS [STICKX STICKY]"; BUTTONS = '+'-joined names or '-'; '#' starts a comment.
PadScriptParse parse_input_script(const std::string& text);
void set_input_script(std::vector<PadScriptEntry> entries);
// The scripted port-0 state at `frame`, or nothing if no script is active yet.
std::optional<PadScriptEntry> scripted_pad(uint32_t frame);

}  // namespace rt
