// Headless VI: retraces are clock events at 50 Hz (PAL) or 60 Hz; VIWaitForRetrace blocks the caller.
#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "rt/clock.hpp"
#include "rt/gfx.hpp"
#include "rt/log.hpp"
#include "rt/guest.hpp"
#include "rt/irq.hpp"
#include "rt/sched.hpp"

using rt::arg;
using rt::ret;

namespace {
constexpr uint32_t kRetraceQueue = 0x817F0400u;  // wait key for VIWaitForRetrace
std::atomic<uint32_t> g_count{0};  // read by the watchdog thread
uint32_t g_frame_limit = 0;
uint32_t g_callback = 0;
uint32_t g_hz = 50;
bool g_running = false;

void retrace() {
    ++g_count;
    rt::g_trace_retrace = g_count.load(std::memory_order_relaxed);
    if (g_frame_limit && g_count >= g_frame_limit) {
        rt::log("vi", "frame limit %u reached", g_frame_limit);
        rt::exit_process(0);
    }
    rt::gfx_pump_events();  // presenting happens at GXCopyDisp; keep the window responsive
    rt::sched().wakeup(kRetraceQueue);
    if (uint32_t cb = g_callback) {
        uint32_t n = g_count.load();
        rt::irq().post([=](ppc_ctx* ic) { rt::call_guest(ic, cb, n); });
    }
    rt::schedule_at(rt::clock().ticks() + rt::Clock::kTicksPerSec / g_hz, retrace);
}

void start() {
    if (g_running) return;
    g_running = true;
    rt::schedule_at(rt::clock().ticks() + rt::Clock::kTicksPerSec / g_hz, retrace);
}
}  // namespace

namespace rt {
uint32_t vi_retrace_count() { return g_count.load(); }
void vi_set_frame_limit(uint32_t n) { g_frame_limit = n; }
}  // namespace rt

extern "C" {

void hle_VIInit(ppc_ctx* c) {
    HLE_ENTRY(c, "VIInit");
    g_running = false;
    start();
}

void hle_VIConfigure(ppc_ctx* c) {
    HLE_ENTRY(c, "VIConfigure");
    uint32_t tvmode = rt::rd32(c, arg(c, 0));
    g_hz = (tvmode >> 2) == 1 ? 50 : 60;
}

void hle_VIWaitForRetrace(ppc_ctx* c) {
    HLE_ENTRY(c, "VIWaitForRetrace");
    start();
    uint32_t target = g_count.load() + 1;
    while (g_count < target) rt::sched().sleep_on(kRetraceQueue);
}

void hle_VIGetRetraceCount(ppc_ctx* c) {
    HLE_ENTRY(c, "VIGetRetraceCount");
    ret(c, g_count.load());
}

void hle_VISetPostRetraceCallback(ppc_ctx* c) {
    HLE_ENTRY(c, "VISetPostRetraceCallback");
    uint32_t prev = g_callback;
    g_callback = arg(c, 0);
    ret(c, prev);
}

void hle_VISetNextFrameBuffer(ppc_ctx* c) { HLE_ENTRY(c, "VISetNextFrameBuffer"); }
void hle_VIFlush(ppc_ctx* c) { HLE_ENTRY(c, "VIFlush"); }
void hle_VISetBlack(ppc_ctx* c) { HLE_ENTRY(c, "VISetBlack"); }

}  // extern "C"
