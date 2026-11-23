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

}  // namespace rt::blo
