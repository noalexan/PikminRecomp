// F3 debug HUD: fps, 1% low, frame-time graph, time breakdown, CPU/GPU usage, the active GPU and the
// last frame drop with its likely cause. Drawn with Dear ImGui inside aurora's frame (graphics builds).
#pragma once
#include <string>

namespace ui {

void hud_init(const std::string& config_path, bool on);
void hud_set(bool on, bool save);  // saves debug_hud = true|false when `save`
bool hud_on();
void hud_toggle();  // F3
void hud_draw();    // inside the ImGui frame, before aurora_end_frame

}  // namespace ui
