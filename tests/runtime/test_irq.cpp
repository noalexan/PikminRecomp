#include <chrono>
#include <string>

#include "check.hpp"
#include "hle_test_util.hpp"
#include "rt/clock.hpp"
#include "rt/irq.hpp"
#include "rt/sched.hpp"

DEFINE_TEST_HOOKS

extern "C" void hle_OSTicksToCalendarTime(ppc_ctx*);

static std::string g_log;
static ppc_ctx g_main;

static void setup(rt::Clock::Mode mode) {
    g_log.clear();
    g_main = make_ctx();
    rt::sched().init(&g_main, 0x817F0100u);
    rt::clock().init(mode);
    rt::irq().init(&g_main);
}

int main() {
    // deferred while disabled, delivered exactly once after restore
    setup(rt::Clock::Mode::Fast);
    bool prev = rt::irq().disable();
    CHECK(prev == true);
    rt::irq().post([](ppc_ctx*) { g_log += "C"; });
    rt::irq().deliver();
    CHECK(g_log.empty());
    rt::irq().restore(true);
    rt::irq().deliver();
    CHECK(g_log == "C");

    // a callback posting another callback does not nest
    setup(rt::Clock::Mode::Fast);
    rt::irq().post([](ppc_ctx*) {
        g_log += "1";
        rt::irq().post([](ppc_ctx*) { g_log += "2"; });
        rt::irq().deliver();  // must not run "2" inside "1"
        g_log += "1";
    });
    rt::irq().deliver();
    CHECK(g_log == "112");

    // callbacks run on the interrupt stack
    setup(rt::Clock::Mode::Fast);
    rt::irq().post([](ppc_ctx* c) { g_log += c->r[1] == 0x81800000u - 16 ? "S" : "x"; });
    rt::irq().deliver();
    CHECK(g_log == "S");

    // callbacks run with interrupts disabled and cannot re-enable them for the thread they interrupt
    setup(rt::Clock::Mode::Fast);
    rt::irq().disable();  // main enters a critical section and sleeps inside it
    rt::schedule_at(rt::clock().ticks() + 1000, [] {
        rt::irq().post([](ppc_ctx*) {
            g_log += rt::irq().enabled() ? "E" : "d";
            rt::irq().restore(true);  // callback re-enables (OSEnableInterrupts)
            rt::sched().wakeup(0x80300200u);
        });
    });
    rt::sched().sleep_on(0x80300200u);
    CHECK(g_log == "d");
    CHECK(!rt::irq().enabled());  // still inside main's critical section
    rt::irq().restore(true);

    // fast clock jumps straight to the next event when every thread sleeps
    setup(rt::Clock::Mode::Fast);
    uint64_t due = rt::clock().ticks() + rt::Clock::kTicksPerSec;
    rt::schedule_at(due, [] { rt::sched().wakeup(0x80300000u); });
    auto t0 = std::chrono::steady_clock::now();
    rt::sched().sleep_on(0x80300000u);
    auto wall = std::chrono::steady_clock::now() - t0;
    CHECK(rt::clock().ticks() >= due);
    CHECK(wall < std::chrono::milliseconds(100));

    // OS epoch: tick 0 is Saturday 2000-01-01 00:00:00
    setup(rt::Clock::Mode::Fast);
    uint32_t cal = 0x80300100u;
    g_main.r[3] = 0;
    g_main.r[4] = 0;
    g_main.r[5] = cal;
    hle_OSTicksToCalendarTime(&g_main);
    CHECK(rt::rd32(&g_main, cal + 0x14) == 2000);  // year
    CHECK(rt::rd32(&g_main, cal + 0x10) == 0);     // mon (0-based)
    CHECK(rt::rd32(&g_main, cal + 0x0C) == 1);     // mday
    CHECK(rt::rd32(&g_main, cal + 0x18) == 6);     // wday
    CHECK(rt::rd32(&g_main, cal + 0x08) == 0);     // hour
    // and one day + 1.5 s later
    uint64_t t = 86400ull * rt::Clock::kTicksPerSec + rt::Clock::kTicksPerSec * 3 / 2;
    g_main.r[3] = static_cast<uint32_t>(t >> 32);
    g_main.r[4] = static_cast<uint32_t>(t);
    g_main.r[5] = cal;
    hle_OSTicksToCalendarTime(&g_main);
    CHECK(rt::rd32(&g_main, cal + 0x0C) == 2 && rt::rd32(&g_main, cal + 0x00) == 1);
    CHECK(rt::rd32(&g_main, cal + 0x20) == 500 && rt::rd32(&g_main, cal + 0x1C) == 1);

    return check_result();
}
