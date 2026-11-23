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
// Menu items as DrawMenu counts them: he00, he01, ... up to the first missing one. 0 if unparseable.
int item_count(const std::vector<uint8_t>& blo);
// Appends one item per label after the last item of the hm00 group: hmNN and heNN text boxes cloned
// from that item, placed below the lowest existing item with the group's spacing. nullopt if the file doesn't parse or has
// fewer than two items in the group.
std::optional<std::vector<uint8_t>> add_menu_items(const std::vector<uint8_t>& blo,
                                                   const std::vector<std::string>& labels);

}  // namespace rt::blo
