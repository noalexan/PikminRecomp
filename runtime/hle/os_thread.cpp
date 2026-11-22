// OS thread HLE over rt::Scheduler.
#include "rt/guest.hpp"
#include "rt/log.hpp"
#include "rt/sched.hpp"

using rt::arg;
using rt::ret;
using rt::sched;

extern "C" {

void hle_OSCreateThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSCreateThread");
    uint32_t thread = arg(c, 0), func = arg(c, 1), param = arg(c, 2), stack = arg(c, 3);
    int prio = static_cast<int32_t>(arg(c, 5));
    uint32_t attr = arg(c, 6);
    if (prio < 0 || prio > 31) {
        ret(c, 0);
        return;
    }
    sched().create(thread, func, param, stack, prio, attr & 1);
    rt::log("os", "OSCreateThread %08x entry %08x prio %d", thread, func, prio);
    ret(c, 1);
}

void hle_OSResumeThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSResumeThread");
    rt::GuestThread* t = sched().by_ea(arg(c, 0));
    ret(c, t ? static_cast<uint32_t>(sched().resume(t)) : 0);
}

void hle_OSSuspendThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSSuspendThread");
    rt::GuestThread* t = sched().by_ea(arg(c, 0));
    int prev = t ? t->suspend : 0;
    if (t) sched().suspend(t);
    ret(c, static_cast<uint32_t>(prev));
}

void hle_OSSleepThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSSleepThread");
    sched().sleep_on(arg(c, 0));
}

void hle_OSWakeupThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSWakeupThread");
    sched().wakeup(arg(c, 0));
}

void hle_OSInitThreadQueue(ppc_ctx* c) {
    HLE_ENTRY(c, "OSInitThreadQueue");
    rt::wr32(c, arg(c, 0), 0);
    rt::wr32(c, arg(c, 0) + 4, 0);
}

void hle_OSYieldThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSYieldThread");
    sched().yield();
}

void hle_OSGetCurrentThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetCurrentThread");
    ret(c, sched().current()->ea);
}

void hle_OSExitThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSExitThread");
    sched().exit_current(arg(c, 0));
}

void hle_OSCancelThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSCancelThread");
    if (rt::GuestThread* t = sched().by_ea(arg(c, 0))) sched().cancel(t);
}

void hle_OSJoinThread(ppc_ctx* c) {
    HLE_ENTRY(c, "OSJoinThread");
    uint32_t value = 0, value_ptr = arg(c, 1);
    bool ok = sched().join(sched().by_ea(arg(c, 0)), &value);
    if (ok && value_ptr) rt::wr32(c, value_ptr, value);
    ret(c, ok);
}

void hle_OSIsThreadTerminated(ppc_ctx* c) {
    HLE_ENTRY(c, "OSIsThreadTerminated");
    rt::GuestThread* t = sched().by_ea(arg(c, 0));
    ret(c, !t || t->state == rt::GuestThread::Moribund);
}

void hle_OSGetThreadPriority(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetThreadPriority");
    rt::GuestThread* t = sched().by_ea(arg(c, 0));
    ret(c, static_cast<uint32_t>(t ? t->prio : 0));
}

void hle_OSCheckActiveThreads(ppc_ctx* c) {
    HLE_ENTRY(c, "OSCheckActiveThreads");
    ret(c, static_cast<uint32_t>(sched().active_count()));
}

}  // extern "C"
