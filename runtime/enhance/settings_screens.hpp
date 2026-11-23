// Settings screens built in memory from the game's own screen files (rt::blo edits).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace enh::screens {

// A settings row: a label and 2-3 choices (dot + text each), or a 10-dot bar (no choices).
struct Row {
    std::string label;
    std::vector<std::string> choices;  // empty = the screen's original volume bar (on11..on20)
};

// From the English s_select.blo (Options > Sound; other languages lay it out differently): title + three rows. Choice k of row r gets a dot picture
// named "o<r><k>n" (coloured on/off at run time) and a text box "o<r><k>t".
std::optional<std::vector<uint8_t>> make_rows_screen(const std::vector<uint8_t>& sound, const std::string& title,
                                                     const std::vector<Row>& rows);
// From option.blo: the pause Options list (Sound, Rumble, Display, Game).
std::optional<std::vector<uint8_t>> make_pause_options(const std::vector<uint8_t>& option);
// From pause_ok.blo: "Quit Pikmin?" with Yes (item 0) / No (item 1).
std::optional<std::vector<uint8_t>> make_quit_screen(const std::vector<uint8_t>& pause_ok);

}  // namespace enh::screens
