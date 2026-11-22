// Input: scripted input, guest PADStatus conversion, default keyboard/controller layouts.
#include <aurora/binding.hpp>
#include <dolphin/pad.h>

#include <algorithm>
#include <string>

#include "gx_shim_test_util.hpp"
#include "rt/input_script.hpp"
#include "rt/pad_layout.hpp"

DEFINE_GFX_TEST_LOOKUP

extern "C" {
void hle_PADInit(ppc_ctx*);
void hle_PADRead(ppc_ctx*);
void hle_VIInit(ppc_ctx*);
void hle_VIWaitForRetrace(ppc_ctx*);
}
namespace rt {
void pad_to_guest(ppc_ctx* c, uint32_t ea, const PADStatus& s);
}

static int failures = 0;
#define CHECK(e)                                                            \
    do {                                                                    \
        if (!(e)) {                                                         \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #e); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

// The bindings in `set` whose target control is named `control`, as "kind:code" strings.
static bool has_binding(const std::vector<aurora::binding::Binding>& set, const char* control,
                        SDL_GamepadButton button) {
    auto id = aurora::binding::find_control(control);
    return std::ranges::any_of(set, [&](const auto& b) {
        auto* gb = b.input.control.template get_if<aurora::binding::PhysicalInput::GamepadButton>();
        return b.target == id && gb && gb->button == button;
    });
}
static bool has_key(const std::vector<aurora::binding::Binding>& set, const char* control, SDL_Scancode key,
                    float scale) {
    auto id = aurora::binding::find_control(control);
    return std::ranges::any_of(set, [&](const auto& b) {
        auto* k = b.input.control.template get_if<aurora::binding::PhysicalInput::Key>();
        return b.target == id && k && k->scancode == key && b.scale == scale;
    });
}

int main() {
    // script parser
    {
        auto r = rt::parse_input_script("# start the game\n10 A+START\n20 - 0 -127\n");
        CHECK(r.error.empty());
        CHECK(r.entries.size() == 2);
        CHECK(r.entries[0].frame == 10 && r.entries[0].buttons == 0x1100);
        CHECK(r.entries[1].frame == 20 && r.entries[1].buttons == 0 && r.entries[1].stick_x == 0 &&
              r.entries[1].stick_y == -127);
        auto bad = rt::parse_input_script("5 FOO\n");
        CHECK(bad.error.find("FOO") != std::string::npos);
    }

    ppc_ctx& c = make_guest_ctx();

    // scripted PADRead (headless path): from retrace 10 on, port 0 reports A+START
    rt::set_input_script(rt::parse_input_script("10 A+START\n").entries);
    gcall(hle_VIInit, &c, {});
    for (int i = 0; i < 10; ++i) gcall(hle_VIWaitForRetrace, &c, {});
    std::memset(&guest_ram()[0x300000], 0x55, 48);
    gcall(hle_PADRead, &c, {0x80300000});
    CHECK(guest_ram()[0x300000] == 0x11 && guest_ram()[0x300001] == 0x00);
    CHECK(static_cast<int8_t>(guest_ram()[0x30000A]) == 0);
    rt::set_input_script({});

    // host PADStatus (PC layout, extra extButton) -> 12-byte guest record
    {
        PADStatus s{};
        s.button = PAD_BUTTON_A | PAD_BUTTON_START;
        s.stickX = 100;
        s.stickY = -50;
        s.substickX = -1;
        s.substickY = 2;
        s.triggerLeft = 30;
        s.triggerRight = 200;
        s.analogA = 7;
        s.analogB = 8;
        s.err = 0;
        s.extButton = 0xFFFFFFFF;
        rt::pad_to_guest(&c, 0x80300100, s);
        const uint8_t want[12] = {0x11, 0x00, 100, static_cast<uint8_t>(-50), 0xFF, 2, 30, 200, 7, 8, 0, 0};
        CHECK(std::memcmp(&guest_ram()[0x300100], want, 11) == 0);
    }

    // default layouts (need aurora's input system: offscreen graphics)
    if (!shim_test_init()) {
        std::fprintf(stderr, "skip layout checks: no GPU\n");
        gfx_test_exit(failures ? 1 : 77);
    }
    gcall(hle_PADInit, &c, {});
    auto ctl = rt::default_controller_bindings();
    CHECK(has_binding(ctl, "aurora.pad.a", SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(has_binding(ctl, "aurora.pad.b", SDL_GAMEPAD_BUTTON_WEST));
    CHECK(has_binding(ctl, "aurora.pad.x", SDL_GAMEPAD_BUTTON_EAST));
    CHECK(has_binding(ctl, "aurora.pad.y", SDL_GAMEPAD_BUTTON_NORTH));
    CHECK(has_binding(ctl, "aurora.pad.z", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    CHECK(has_binding(ctl, "aurora.pad.start", SDL_GAMEPAD_BUTTON_START));
    auto kb = rt::default_keyboard_bindings();
    CHECK(has_key(kb, "aurora.pad.left_y", SDL_SCANCODE_W, 1.f));
    CHECK(has_key(kb, "aurora.pad.left_y", SDL_SCANCODE_S, -1.f));
    CHECK(has_key(kb, "aurora.pad.right_x", SDL_SCANCODE_RIGHT, 1.f));
    CHECK(has_key(kb, "aurora.pad.a", SDL_SCANCODE_J, 1.f));
    CHECK(has_key(kb, "aurora.pad.start", SDL_SCANCODE_RETURN, 1.f));

    if (failures) gfx_test_exit(1);
    std::puts("ok");
    gfx_test_exit(0);
}
