// Default input layouts registered with aurora: keyboard (port 0) and SDL gamepads (DualShock 4 etc.).
#pragma once
#ifdef PIKMIN_GRAPHICS
#include <aurora/binding.hpp>

#include <vector>

namespace rt {
std::vector<aurora::binding::Binding> default_keyboard_bindings();
std::vector<aurora::binding::Binding> default_controller_bindings();
// Registers both with aurora (call after aurora's PADInit, which registers the pad controls).
void install_default_layouts();
}  // namespace rt
#endif
