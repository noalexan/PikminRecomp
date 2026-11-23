// In-game overlay (Dear ImGui through aurora): Resume / Settings / Quit Game.
#pragma once

namespace ui {

enum class Request { None, Settings, Quit };
// From the native menu entries (enhance/menu_entries.cpp): open the overlay on that page.
void request(Request r);

}  // namespace ui
