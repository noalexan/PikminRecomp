#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.hpp"
#include "rt/guest.hpp"
#include "rt/sched.hpp"

extern "C" void hle_OSIsThreadTerminated(ppc_ctx* c);

static std::vector<uint8_t> ram(PPC_RAM_SIZE);
static std::string g_log;
static std::map<uint32_t, ppc_fn> g_funcs;
static uint32_t g_queue = 0x80300000;
static uint32_t g_stack_top = 0;

extern "C" {
ppc_fn ppc_lookup(uint32_t addr) { return g_funcs.at(addr); }
uint32_t ppc_mmio_read(uint32_t, int) { return 0; }
void ppc_mmio_write(uint32_t, uint32_t, int) {}
void ppc_bad_access(uint32_t ea) {
    std::fprintf(stderr, "bad access %08x\n", ea);
    std::exit(2);
}
}

using rt::sched;

static void t_log_T(ppc_ctx*) { g_log += "T"; }
static int g_runs = 0;
static void t_count(ppc_ctx* c) { c->r[3] = static_cast<uint32_t>(++g_runs); }
static void t_wakeup(ppc_ctx*) {
    g_log += "T";
    sched().wakeup(g_queue);
    g_log += "t";  // runs only after main sleeps again
}
static void t_yield_A(ppc_ctx*) { g_log += "A1"; sched().yield(); g_log += "A2"; }
static void t_yield_B(ppc_ctx*) { g_log += "B1"; sched().yield(); g_log += "B2"; }
static void t_return7(ppc_ctx* c) { c->r[3] = 7; }
static void t_check_regs(ppc_ctx* c) {
    CHECK(c->r[1] == ((g_stack_top - 8) & ~7u));
    CHECK(c->r[3] == 0x1234);
    CHECK(c->r[13] == 0x803e9c60u);
    g_log += "R";
}
static int recurse(int n) {
    volatile char frame[64];  // real stack usage per level
    frame[0] = 1;
    return n == 0 ? 0 : recurse(n - 1) + frame[0];
}
static void t_deep(ppc_ctx*) { g_log += recurse(50000) == 50000 ? "D" : "x"; }

static ppc_ctx g_main{};

static void reset() {
    g_log.clear();
    g_main = ppc_ctx{};
    g_main.mem = ram.data();
    g_main.r[2] = 0x803f5120u;
    g_main.r[13] = 0x803e9c60u;
    sched().init(&g_main, 0x817F0100u);
}

static rt::GuestThread* spawn(uint32_t ea, ppc_fn fn, int prio, uint32_t param = 0, uint32_t stack_top = 0x80380000) {
    uint32_t entry = 0x80010000 + static_cast<uint32_t>(g_funcs.size()) * 4;
    g_funcs[entry] = fn;
    return sched().create(ea, entry, param, stack_top, prio, false);
}

int main() {
    // higher priority thread runs as soon as it is resumed
    reset();
    sched().resume(spawn(0x80200000, t_log_T, 10));
    g_log += "M";
    CHECK(g_log == "TM");

    // lower priority thread only runs when main blocks; wakeup preempts back to main
    reset();
    sched().resume(spawn(0x80200400, t_wakeup, 20));
    g_log += "M";
    sched().sleep_on(g_queue);
    g_log += "M";
    CHECK(g_log == "MTM");

    // equal priority round robin via yield
    reset();
    sched().resume(spawn(0x80200800, t_yield_A, 16));
    sched().resume(spawn(0x80200C00, t_yield_B, 16));
    sched().yield();
    sched().yield();
    CHECK(g_log == "A1B1A2B2");

    // join returns the exit value; terminated flag set
    reset();
    rt::GuestThread* t = spawn(0x80201000, t_return7, 20);
    sched().resume(t);
    uint32_t value = 0;
    CHECK(sched().join(t, &value));
    CHECK(value == 7);
    g_main.r[3] = 0x80201000;
    hle_OSIsThreadTerminated(&g_main);
    CHECK(g_main.r[3] == 1);

    // guest registers of a new thread
    reset();
    g_stack_top = 0x80390004;
    sched().resume(spawn(0x80201400, t_check_regs, 10, 0x1234, g_stack_top));
    CHECK(g_log == "R");

    // deep host recursion on a fiber stack
    reset();
    sched().resume(spawn(0x80201800, t_deep, 10));
    CHECK(g_log == "D");

    // the same OSThread struct reused for successive threads (System::startLoading does this)
    reset();
    g_runs = 0;
    for (uint32_t i = 1; i <= 3; ++i) {
        rt::GuestThread* lt = spawn(0x80202000, t_count, 20);
        sched().resume(lt);
        uint32_t v = 0;
        CHECK(sched().join(lt, &v));
        CHECK(v == i);
        CHECK(sched().by_ea(0x80202000) == lt);
    }
    CHECK(g_runs == 3);
    CHECK(sched().active_count() == 1);  // only main remains active

    // deadlock is reported instead of hanging
    reset();
    sched().set_deadlock_handler([] { throw std::runtime_error("deadlock"); });
    bool caught = false;
    try {
        sched().sleep_on(0x80300100);
    } catch (const std::runtime_error&) {
        caught = true;
    }
    CHECK(caught);

    return check_result();
}
