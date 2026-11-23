#include "rt/pad_layout.hpp"

#ifdef PIKMIN_GRAPHICS
#include <aurora/pad.hpp>

namespace rt {
namespace {
using aurora::binding::Binding;
using aurora::binding::PhysicalInput;
using Dir = PhysicalInput::GamepadAxis::Direction;

Binding key(SDL_Scancode sc, const char* control, float scale = 1.f) {
    Binding b;
    b.input.control = PhysicalInput::Key{sc};
    b.target = aurora::binding::find_control(control);
    b.scale = scale;
    return b;
}

Binding button(SDL_GamepadButton btn, const char* control) {
    Binding b;
    b.input.control = PhysicalInput::GamepadButton{btn};
    b.target = aurora::binding::find_control(control);
    return b;
}

Binding axis(SDL_GamepadAxis ax, Dir dir, const char* control, float scale = 1.f) {
    Binding b;
    b.input.control = PhysicalInput::GamepadAxis{ax, dir};
    b.target = aurora::binding::find_control(control);
    b.scale = scale;
    return b;
}
}  // namespace

// WASD stick, arrow keys = right stick (camera; with Left Shift held: C-stick swarming),
// J/K/L/I = A/B/X/Y, U = Z, Q/E = L/R, Enter / Esc = Start, T/G/F/H = D-pad.
std::vector<Binding> default_keyboard_bindings() {
    return {
        key(SDL_SCANCODE_W, "aurora.pad.left_y", 1.f),     key(SDL_SCANCODE_S, "aurora.pad.left_y", -1.f),
        key(SDL_SCANCODE_D, "aurora.pad.left_x", 1.f),     key(SDL_SCANCODE_A, "aurora.pad.left_x", -1.f),
        key(SDL_SCANCODE_UP, "aurora.pad.right_y", 1.f),   key(SDL_SCANCODE_DOWN, "aurora.pad.right_y", -1.f),
        key(SDL_SCANCODE_RIGHT, "aurora.pad.right_x", 1.f), key(SDL_SCANCODE_LEFT, "aurora.pad.right_x", -1.f),
        key(SDL_SCANCODE_J, "aurora.pad.a"),               key(SDL_SCANCODE_K, "aurora.pad.b"),
        key(SDL_SCANCODE_L, "aurora.pad.x"),               key(SDL_SCANCODE_I, "aurora.pad.y"),
        key(SDL_SCANCODE_U, "aurora.pad.z"),
        key(SDL_SCANCODE_Q, "aurora.pad.l"),               key(SDL_SCANCODE_Q, "aurora.pad.trigger_l"),
        key(SDL_SCANCODE_E, "aurora.pad.r"),               key(SDL_SCANCODE_E, "aurora.pad.trigger_r"),
        key(SDL_SCANCODE_RETURN, "aurora.pad.start"),     key(SDL_SCANCODE_ESCAPE, "aurora.pad.start"),
        key(SDL_SCANCODE_LSHIFT, "aurora.pad.ext.misc1"),  // swarm modifier (see enhance/camera.cpp)
        key(SDL_SCANCODE_T, "aurora.pad.up"),              key(SDL_SCANCODE_G, "aurora.pad.down"),
        key(SDL_SCANCODE_F, "aurora.pad.left"),            key(SDL_SCANCODE_H, "aurora.pad.right"),
    };
}

// Cross/South = A, Circle/East = B (back, as players expect on a PlayStation pad), Square/West = X,
// Triangle/North = Y, R1 = Z, L2/R2 = analog L/R, Options / touchpad = Start.
// Right stick = camera (modern camera); L1 + right stick = C-stick (swarming).
// SDL's Y axes point down; the GameCube's point up.
std::vector<Binding> default_controller_bindings() {
    std::vector<Binding> v = {
        button(SDL_GAMEPAD_BUTTON_SOUTH, "aurora.pad.a"),
        button(SDL_GAMEPAD_BUTTON_EAST, "aurora.pad.b"),
        button(SDL_GAMEPAD_BUTTON_WEST, "aurora.pad.x"),
        button(SDL_GAMEPAD_BUTTON_NORTH, "aurora.pad.y"),
        button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "aurora.pad.z"),
        button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "aurora.pad.ext.misc1"),  // swarm modifier
        button(SDL_GAMEPAD_BUTTON_START, "aurora.pad.start"),
        button(SDL_GAMEPAD_BUTTON_TOUCHPAD, "aurora.pad.start"),
        button(SDL_GAMEPAD_BUTTON_DPAD_UP, "aurora.pad.up"),
        button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, "aurora.pad.down"),
        button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, "aurora.pad.left"),
        button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "aurora.pad.right"),
        axis(SDL_GAMEPAD_AXIS_LEFTX, Dir::Full, "aurora.pad.left_x"),
        axis(SDL_GAMEPAD_AXIS_LEFTY, Dir::Negative, "aurora.pad.left_y", 1.f),
        axis(SDL_GAMEPAD_AXIS_LEFTY, Dir::Positive, "aurora.pad.left_y", -1.f),
        axis(SDL_GAMEPAD_AXIS_RIGHTX, Dir::Full, "aurora.pad.right_x"),
        axis(SDL_GAMEPAD_AXIS_RIGHTY, Dir::Negative, "aurora.pad.right_y", 1.f),
        axis(SDL_GAMEPAD_AXIS_RIGHTY, Dir::Positive, "aurora.pad.right_y", -1.f),
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, Dir::Positive, "aurora.pad.trigger_l"),
        axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Dir::Positive, "aurora.pad.trigger_r"),
    };
    Binding lclick = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, Dir::Positive, "aurora.pad.l");
    lclick.threshold = 0.85f;  // digital L/R click near full press, like the GameCube triggers
    Binding rclick = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Dir::Positive, "aurora.pad.r");
    rclick.threshold = 0.85f;
    v.push_back(lclick);
    v.push_back(rclick);
    return v;
}

void install_default_layouts() {
    aurora::pad::set_default_keyboard_bindings(0, default_keyboard_bindings());
    aurora::pad::set_default_controller_bindings(default_controller_bindings());
}

}  // namespace rt
#endif
