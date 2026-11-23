// Boot logo: the red Nintendo logo is drawn by the loading screen (GameLoadIdler, on the loading
// thread, once per retrace) while GameFlow::hardReset and the title section load their data,
// about 5.6 MB in some 35 files. From a GameCube disc (2-3 MB/s plus seeks) that took about three
// seconds; read from the host it finishes within a retrace and the logo only flashes. In real-time
// runs the boot loads therefore end no sooner than kHoldSeconds after the first one started. --fast
// runs keep the original timeline, so input scripts and screenshots stay frame-exact.
#include "rt/clock.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"

namespace rt {
uint32_t vi_retrace_count();
uint32_t vi_hz();
void vi_wait_until(uint32_t target);
}  // namespace rt

extern "C" {
__attribute__((weak)) void f_80046128_orig(ppc_ctx* c);  // System::startLoading(LoadIdler*, bool, u32)
__attribute__((weak)) void f_800461f0_orig(ppc_ctx* c);  // System::endLoading()
}

namespace {
constexpr uint32_t kGameFlow = 0x803a2638u;         // GameFlow gameflow
constexpr uint32_t kBootLogoFlag = kGameFlow + 0x2d4;  // set by hardReset: loading screen shows the boot logo
constexpr uint32_t kGameLoadIdler = kGameFlow + 0x31c;
constexpr uint32_t kHoldSeconds = 3;

bool g_started = false;  // first boot-logo load seen
bool g_held = false;
uint32_t g_start = 0;

bool boot_logo_shown(ppc_ctx* c) { return rt::rd32(c, kBootLogoFlag) != 0; }
}  // namespace

extern "C" {

void hle_startLoading__6SystemFP9LoadIdlerbUl(ppc_ctx* c) {
    HLE_ENTRY(c, "System::startLoading");
    if (!g_started && rt::arg(c, 1) == kGameLoadIdler && boot_logo_shown(c)) {
        g_started = true;
        g_start = rt::vi_retrace_count();
    }
    if (f_80046128_orig) f_80046128_orig(c);
}

void hle_endLoading__6SystemFv(ppc_ctx* c) {
    HLE_ENTRY(c, "System::endLoading");
    if (g_started && !g_held && boot_logo_shown(c) && rt::clock().mode() == rt::Clock::Mode::Real) {
        g_held = true;
        const uint32_t until = g_start + kHoldSeconds * rt::vi_hz();
        rt::vi_wait_until(until);  // the loading thread keeps drawing the logo meanwhile
        rt::log("loading", "boot logo held from retrace %u to %u", g_start, rt::vi_retrace_count());
    }
    if (f_800461f0_orig) f_800461f0_orig(c);
}

}  // extern "C"
