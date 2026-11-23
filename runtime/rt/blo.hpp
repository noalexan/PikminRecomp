// P2D screen files (.blo): read the menu items and add new ones. Format: docs/superpowers/notes/blo-format.md.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rt::blo {

// A text-box pane named hmNN (item text) or heNN (its highlighted copy), as zen::DrawMenu finds them.
struct Item {
    std::string name;
    std::string text;
    int depth = 0;  // nesting level in the pane hierarchy
    int16_t x = 0, y = 0, w = 0, h = 0;
    uint16_t tag = 0;     // 0x10 pane, 0x12 picture, 0x13 text box (set by all())
    std::string texture;  // pictures: texture resource name
    bool visible = true;
};

// Every hmNN/heNN text box, in file order; empty if the file doesn't parse.
std::vector<Item> menu_items(const std::vector<uint8_t>& blo);
// Every plain pane (groups such as yoko, iall, pall), in file order.
std::vector<Item> panes(const std::vector<uint8_t>& blo);
// Every picture pane (frame pieces pNNN, side icons iNNl/iNNr, ...), in file order.
std::vector<Item> pictures(const std::vector<uint8_t>& blo);
// Menu items as DrawMenu counts them: he00, he01, ... up to the first missing one. 0 if unparseable.
int item_count(const std::vector<uint8_t>& blo);
// Appends one item per label: hmNN/heNN text boxes (and iNNl/iNNr side icons) cloned from the last
// item of the hm00 group, below the lowest existing item. Menus of 4+ items are re-spaced to at
// most 36 px (m_selec2.blo tightens to 39); the glass capsule (unnamed pictures in the item group) grows
// to fit and the menu moves up by at most 55 px. nullopt if the file doesn't parse or the group has fewer than two items.
std::optional<std::vector<uint8_t>> add_menu_items(const std::vector<uint8_t>& blo,
                                                   const std::vector<std::string>& labels);

// Every pane record in file order (tag, texture and visibility filled in); the position in this
// list is the index the editors below take. Empty if the file doesn't parse.
std::vector<Item> all(const std::vector<uint8_t>& blo);
// In-place edits of pane `index`; false if it doesn't exist (or isn't a text box, for set_text).
bool set_text(std::vector<uint8_t>& blo, size_t index, const std::string& text);
bool set_visible(std::vector<uint8_t>& blo, size_t index, bool visible);
bool set_pos(std::vector<uint8_t>& blo, size_t index, int x, int y);
// Inserts a copy of pane `index` (a picture or text box without children) right after it, renamed
// (4 characters) and moved to x, y in the same parent.
bool insert_clone(std::vector<uint8_t>& blo, size_t index, const std::string& name, int x, int y);

}  // namespace rt::blo
