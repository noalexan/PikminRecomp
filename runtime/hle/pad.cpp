// Headless PAD: no controllers connected.
#include "rt/guest.hpp"

using rt::arg;
using rt::ret;

extern "C" {

void hle_PADInit(ppc_ctx* c) {
    HLE_ENTRY(c, "PADInit");
    ret(c, 1);
}

// PADStatus is 12 bytes: buttons, sticks, triggers, analog A/B, err (+0x0A, s8).
void hle_PADRead(ppc_ctx* c) {
    HLE_ENTRY(c, "PADRead");
    uint32_t status = arg(c, 0);
    for (uint32_t p = 0; p < 4; ++p) {
        for (uint32_t off = 0; off < 12; off += 4) rt::wr32(c, status + p * 12 + off, 0);
        rt::wr8(c, status + p * 12 + 0x0A, static_cast<uint8_t>(-1));  // PAD_ERR_NO_CONTROLLER
    }
    ret(c, 0);
}

void hle_PADReset(ppc_ctx* c) {
    HLE_ENTRY(c, "PADReset");
    ret(c, 1);
}
void hle_PADRecalibrate(ppc_ctx* c) {
    HLE_ENTRY(c, "PADRecalibrate");
    ret(c, 1);
}
void hle_PADClamp(ppc_ctx* c) { HLE_ENTRY(c, "PADClamp"); }
void hle_PADSetSpec(ppc_ctx* c) { HLE_ENTRY(c, "PADSetSpec"); }
void hle_PADControlMotor(ppc_ctx* c) { HLE_ENTRY(c, "PADControlMotor"); }

}  // extern "C"
