#include <stdexcept>
#include <string>

#include "check.hpp"
#include "hle_test_util.hpp"
#include "rt/clock.hpp"
#include "rt/irq.hpp"
#include "rt/sched.hpp"

DEFINE_TEST_HOOKS

extern "C" {
void hle_OSInitMutex(ppc_ctx*);
void hle_OSLockMutex(ppc_ctx*);
void hle_OSUnlockMutex(ppc_ctx*);
void hle_OSInitCond(ppc_ctx*);
void hle_OSWaitCond(ppc_ctx*);
void hle_OSSignalCond(ppc_ctx*);
void hle_OSInitMessageQueue(ppc_ctx*);
void hle_OSSendMessage(ppc_ctx*);
void hle_OSReceiveMessage(ppc_ctx*);
void hle_OSCreateThread(ppc_ctx*);
void hle_OSResumeThread(ppc_ctx*);
}

static std::string g_log;
static ppc_ctx g_main;
constexpr uint32_t M = 0x80300000u, COND = 0x80300100u, MQ = 0x80300200u, MSGS = 0x80300300u, OUT = 0x80300400u;

static void setup() {
    g_log.clear();
    g_main = make_ctx();
    rt::sched().init(&g_main, 0x817F0100u);
    rt::clock().init(rt::Clock::Mode::Fast);
    rt::irq().init(&g_main);
}

static uint32_t spawn(ppc_fn fn, int prio, uint32_t ea) {
    call(hle_OSCreateThread, &g_main, {ea, fake_fn(fn), 0, 0x80380000u - (ea & 0xFFFF) * 16, 0x1000,
                                       static_cast<uint32_t>(prio), 0});
    call(hle_OSResumeThread, &g_main, {ea});
    return ea;
}

static void t_lock(ppc_ctx* c) {
    call(hle_OSLockMutex, c, {M});
    g_log += "T";
    call(hle_OSUnlockMutex, c, {M});
}

static void t_signal(ppc_ctx* c) {
    call(hle_OSLockMutex, c, {M});
    g_log += "S";
    call(hle_OSSignalCond, c, {COND});
    call(hle_OSUnlockMutex, c, {M});
}

static uint32_t g_flag = 0;
static void t_set_flag(ppc_ctx* c) {
    call(hle_OSLockMutex, c, {M});  // blocks: main holds M
    g_flag = 1;
    call(hle_OSSignalCond, c, {COND});
    call(hle_OSUnlockMutex, c, {M});
}

static void t_receive(ppc_ctx* c) {
    for (int i = 0; i < 2; ++i) {
        call(hle_OSReceiveMessage, c, {MQ, OUT, 1});
        g_log += "R" + std::to_string(rt::rd32(c, OUT));
    }
}

int main() {
    // mutex blocks a second thread until unlock; recursive locking by the owner
    setup();
    call(hle_OSInitMutex, &g_main, {M});
    call(hle_OSLockMutex, &g_main, {M});
    call(hle_OSLockMutex, &g_main, {M});  // recursive: must not block
    CHECK(rt::rd32(&g_main, M + 0x0C) == 2);
    spawn(t_lock, 10, 0x80200000u);       // higher prio, blocks on the mutex
    g_log += "M";
    call(hle_OSUnlockMutex, &g_main, {M});
    CHECK(g_log == "M");                  // still held once
    call(hle_OSUnlockMutex, &g_main, {M});  // released: T preempts
    CHECK(g_log == "MT");

    // condition variable handoff
    setup();
    call(hle_OSInitMutex, &g_main, {M});
    call(hle_OSInitCond, &g_main, {COND});
    call(hle_OSLockMutex, &g_main, {M});
    spawn(t_signal, 20, 0x80200400u);     // lower prio: runs once main waits
    call(hle_OSWaitCond, &g_main, {COND, M});
    g_log += "W";
    CHECK(g_log == "SW");
    CHECK(rt::rd32(&g_main, M + 0x08) == 0x817F0100u);  // main owns the mutex again

    // OSWaitCond must not lose a signal sent by a higher-priority thread waiting on the mutex
    setup();
    g_flag = 0;
    call(hle_OSInitMutex, &g_main, {M});
    call(hle_OSInitCond, &g_main, {COND});
    call(hle_OSLockMutex, &g_main, {M});
    spawn(t_set_flag, 10, 0x80200600u);  // runs now and blocks on M
    rt::sched().set_deadlock_handler([] { throw std::runtime_error("deadlock"); });
    bool deadlocked = false;
    try {
        while (!g_flag) call(hle_OSWaitCond, &g_main, {COND, M});
    } catch (const std::runtime_error&) {
        deadlocked = true;
    }
    rt::sched().set_deadlock_handler(nullptr);
    CHECK(!deadlocked && g_flag == 1);
    CHECK(rt::rd32(&g_main, M + 0x08) == 0x817F0100u);

    // message queue: capacity, blocking receive preemption, FIFO order
    setup();
    call(hle_OSInitMessageQueue, &g_main, {MQ, MSGS, 2});
    CHECK(call(hle_OSSendMessage, &g_main, {MQ, 1, 0}) == 1);
    CHECK(call(hle_OSSendMessage, &g_main, {MQ, 2, 0}) == 1);
    CHECK(call(hle_OSSendMessage, &g_main, {MQ, 3, 0}) == 0);  // full, non-blocking
    spawn(t_receive, 10, 0x80200800u);    // drains 1, 2 immediately
    CHECK(g_log == "R1R2");
    call(hle_OSSendMessage, &g_main, {MQ, 5, 1});
    CHECK(g_log == "R1R2");               // receiver finished after two messages

    return check_result();
}
