// The settings shown on the Display and Game screens: current value as a choice index, live apply,
// and saving the changed keys to config.toml.
#pragma once
#include <string>

namespace ui {

enum class Setting { Window, Aspect, Vsync, Fps, Camera, Hud };
// Window: Full / Window. Aspect: Wide / 4:3. Vsync: On / Off. Fps: Original / 60 / Max.
// Camera: Modern / Original. Hud: 2..10 dots (hud_scale = dots / 10).
int choices(Setting s);
int value(Setting s);
int min_value(Setting s);
// Applies now (logged as "[settings] <key> = <value>") and marks the key for saving.
void set(Setting s, int v);
// Writes the changed keys to the config file; clears the marks.
void save();
// From main, after the enhancement settings are final.
void init(const std::string& config_path, bool fullscreen, bool vsync, bool window);

}  // namespace ui
