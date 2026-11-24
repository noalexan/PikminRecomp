// Cheat menu (Dear ImGui, mouse / keyboard / gamepad): F1 or L3 + R3 opens and closes it, B or Esc
// closes it. While it is open the game sees a released controller. The cheats themselves are in
// enhance/cheats.cpp; docs/cheats.md is the player-facing sheet.
#pragma once

namespace ui {

bool cheat_menu_open();
void cheat_menu_toggle();
void cheat_menu_draw();  // inside the ImGui frame, before aurora_end_frame

}  // namespace ui
