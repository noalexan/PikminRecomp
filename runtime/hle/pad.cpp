// PAD: aurora's SDL input when graphics are active (keyboard + gamepads), "no controller" when headless.
// A test input script (--input-script) overrides port 0 in both modes.
#include "enhance/enhance.hpp"
#include "ui/cheat_menu.hpp"
#include "rt/gfx.hpp"
#include "rt/guest.hpp"
#include "rt/input_record.hpp"
#include "rt/input_script.hpp"

#ifdef PIKMIN_GRAPHICS
#include <dolphin/pad.h>

#include "rt/pad_layout.hpp"
#endif

using rt::arg;
using rt::ret;

namespace rt {
uint32_t vi_retrace_count();

#ifdef PIKMIN_GRAPHICS
// aurora's PC PADStatus (extra extButton) -> the 12-byte guest record.
void pad_to_guest(ppc_ctx* c, uint32_t ea, const PADStatus& s) {
    wr16(c, ea + 0x00, s.button);
    wr8(c, ea + 0x02, static_cast<uint8_t>(s.stickX));
    wr8(c, ea + 0x03, static_cast<uint8_t>(s.stickY));
    wr8(c, ea + 0x04, static_cast<uint8_t>(s.substickX));
    wr8(c, ea + 0x05, static_cast<uint8_t>(s.substickY));
    wr8(c, ea + 0x06, s.triggerLeft);
    wr8(c, ea + 0x07, s.triggerRight);
    wr8(c, ea + 0x08, s.analogA);
    wr8(c, ea + 0x09, s.analogB);
    wr8(c, ea + 0x0A, static_cast<uint8_t>(s.err));
    wr8(c, ea + 0x0B, 0);
}

PADStatus pad_from_guest(ppc_ctx* c, uint32_t ea) {
    PADStatus s{};
    s.button = rd16(c, ea);
    s.stickX = static_cast<s8>(rd8(c, ea + 2));
    s.stickY = static_cast<s8>(rd8(c, ea + 3));
    s.substickX = static_cast<s8>(rd8(c, ea + 4));
    s.substickY = static_cast<s8>(rd8(c, ea + 5));
    s.triggerLeft = rd8(c, ea + 6);
    s.triggerRight = rd8(c, ea + 7);
    s.analogA = rd8(c, ea + 8);
    s.analogB = rd8(c, ea + 9);
    s.err = static_cast<s8>(rd8(c, ea + 10));
    return s;
}
#endif
}  // namespace rt

namespace {
void write_no_controller(ppc_ctx* c, uint32_t status) {
    for (uint32_t p = 0; p < 4; ++p) {
        for (uint32_t off = 0; off < 12; off += 4) rt::wr32(c, status + p * 12 + off, 0);
        rt::wr8(c, status + p * 12 + 0x0A, static_cast<uint8_t>(-1));  // PAD_ERR_NO_CONTROLLER
    }
}

// Returns whether the script holds the swarm modifier (false without a script).
bool apply_script(ppc_ctx* c, uint32_t status) {
    if (auto s = rt::scripted_pad(rt::vi_retrace_count())) {
        for (uint32_t off = 0; off < 12; off += 4) rt::wr32(c, status + off, 0);
        rt::wr16(c, status, s->buttons);
        rt::wr8(c, status + 2, static_cast<uint8_t>(s->stick_x));
        rt::wr8(c, status + 3, static_cast<uint8_t>(s->stick_y));
        rt::wr8(c, status + 4, static_cast<uint8_t>(s->substick_x));
        rt::wr8(c, status + 5, static_cast<uint8_t>(s->substick_y));
        rt::wr8(c, status + 0x0A, 0);  // connected
        return s->swarm;
    }
    return false;
}
}  // namespace

extern "C" {

void hle_PADInit(ppc_ctx* c) {
    HLE_ENTRY(c, "PADInit");
#ifdef PIKMIN_GRAPHICS
    if (rt::gfx_active()) {
        BOOL ok = PADInit();
        rt::install_default_layouts();
        ret(c, ok);
        return;
    }
#endif
    ret(c, 1);
}

void hle_PADRead(ppc_ctx* c) {
    HLE_ENTRY(c, "PADRead");
    uint32_t status = arg(c, 0);
    uint32_t result = 0;
    bool swarm = false;
#ifdef PIKMIN_GRAPHICS
    if (rt::gfx_active()) {
        PADStatus host[PAD_CHANMAX] = {};
        result = PADRead(host);
        for (uint32_t p = 0; p < PAD_CHANMAX; ++p) rt::pad_to_guest(c, status + p * 12, host[p]);
        swarm = (host[0].extButton & PAD_BUTTON_MISC1) != 0;  // LB / Left Shift (rt/pad_layout.cpp)
    } else
#endif
    {
        write_no_controller(c, status);
    }
    swarm = apply_script(c, status) || swarm;
    enh::cheats_filter_pad(c, status, ui::cheat_menu_open());  // menu open: released pad; fly: D-pad
    enh::cutscene_filter_pad(c, status);  // Start during a movie: skip it (enhance/cutscene_skip.cpp)
    rt::input_record(rt::vi_retrace_count(), rt::rd16(c, status), static_cast<int8_t>(rt::rd8(c, status + 2)),
                     static_cast<int8_t>(rt::rd8(c, status + 3)), static_cast<int8_t>(rt::rd8(c, status + 4)),
                     static_cast<int8_t>(rt::rd8(c, status + 5)), swarm);
    enh::route_right_stick(c, status, swarm);  // modern camera: right stick = camera unless swarming
    ret(c, result);
}

void hle_PADReset(ppc_ctx* c) {
    HLE_ENTRY(c, "PADReset");
#ifdef PIKMIN_GRAPHICS
    if (rt::gfx_active()) {
        ret(c, PADReset(arg(c, 0)));
        return;
    }
#endif
    ret(c, 1);
}

void hle_PADRecalibrate(ppc_ctx* c) {
    HLE_ENTRY(c, "PADRecalibrate");
#ifdef PIKMIN_GRAPHICS
    if (rt::gfx_active()) {
        ret(c, PADRecalibrate(arg(c, 0)));
        return;
    }
#endif
    ret(c, 1);
}

void hle_PADClamp(ppc_ctx* c) {
    HLE_ENTRY(c, "PADClamp");
#ifdef PIKMIN_GRAPHICS
    if (rt::gfx_active()) {
        uint32_t status = arg(c, 0);
        PADStatus host[PAD_CHANMAX];
        for (uint32_t p = 0; p < PAD_CHANMAX; ++p) host[p] = rt::pad_from_guest(c, status + p * 12);
        PADClamp(host);
        for (uint32_t p = 0; p < PAD_CHANMAX; ++p) rt::pad_to_guest(c, status + p * 12, host[p]);
    }
#endif
}

void hle_PADSetSpec(ppc_ctx* c) {
    HLE_ENTRY(c, "PADSetSpec");
#ifdef PIKMIN_GRAPHICS
    if (rt::gfx_active()) PADSetSpec(arg(c, 0));
#endif
}

void hle_PADControlMotor(ppc_ctx* c) {
    HLE_ENTRY(c, "PADControlMotor");
#ifdef PIKMIN_GRAPHICS
    if (rt::gfx_active()) PADControlMotor(arg(c, 0), arg(c, 1));
#endif
}

}  // extern "C"
