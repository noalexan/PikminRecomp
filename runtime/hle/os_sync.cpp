// Mutex, condition variable and message queue HLE (SDK layouts, waits keyed by guest address).
#include "rt/guest.hpp"
#include "rt/sched.hpp"

using rt::arg;
using rt::rd32;
using rt::ret;
using rt::sched;
using rt::wr32;

namespace {
// OSMutex: +0x00 queue, +0x08 owner thread, +0x0C count, +0x10 link
constexpr uint32_t kOwner = 0x08, kCount = 0x0C;
// OSMessageQueue: +0x00 queueSend, +0x08 queueReceive, +0x10 msgArray, +0x14 msgCount, +0x18 first, +0x1C used
constexpr uint32_t kSendQ = 0x00, kRecvQ = 0x08, kArray = 0x10, kMsgCount = 0x14, kFirst = 0x18, kUsed = 0x1C;

uint32_t self() { return sched().current()->ea; }

void lock(ppc_ctx* c, uint32_t m, uint32_t count) {
    for (;;) {
        uint32_t owner = rd32(c, m + kOwner);
        if (owner == 0) {
            wr32(c, m + kOwner, self());
            wr32(c, m + kCount, count);
            return;
        }
        if (owner == self()) {
            wr32(c, m + kCount, rd32(c, m + kCount) + count);
            return;
        }
        sched().sleep_on(m);
    }
}
}  // namespace

extern "C" {

void hle_OSInitMutex(ppc_ctx* c) {
    HLE_ENTRY(c, "OSInitMutex");
    for (uint32_t off = 0; off < 0x18; off += 4) wr32(c, arg(c, 0) + off, 0);
}

void hle_OSLockMutex(ppc_ctx* c) {
    HLE_ENTRY(c, "OSLockMutex");
    lock(c, arg(c, 0), 1);
}

void hle_OSUnlockMutex(ppc_ctx* c) {
    HLE_ENTRY(c, "OSUnlockMutex");
    uint32_t m = arg(c, 0);
    if (rd32(c, m + kOwner) != self()) return;
    uint32_t n = rd32(c, m + kCount) - 1;
    wr32(c, m + kCount, n);
    if (n == 0) {
        wr32(c, m + kOwner, 0);
        sched().wakeup(m);
    }
}

void hle_OSInitCond(ppc_ctx* c) {
    HLE_ENTRY(c, "OSInitCond");
    wr32(c, arg(c, 0), 0);
    wr32(c, arg(c, 0) + 4, 0);
}

void hle_OSWaitCond(ppc_ctx* c) {
    HLE_ENTRY(c, "OSWaitCond");
    uint32_t cond = arg(c, 0), m = arg(c, 1);
    if (rd32(c, m + kOwner) != self()) return;
    uint32_t count = rd32(c, m + kCount);
    // Sleep on the condition *before* releasing the mutex, so a mutex waiter that signals
    // right away cannot be missed (the SDK avoids this via priority inheritance).
    sched().begin_sleep(cond);
    wr32(c, m + kOwner, 0);
    wr32(c, m + kCount, 0);
    ++sched().defer_depth;
    sched().wakeup(m);
    --sched().defer_depth;
    sched().reschedule();
    lock(c, m, count);
}

void hle_OSSignalCond(ppc_ctx* c) {
    HLE_ENTRY(c, "OSSignalCond");
    sched().wakeup(arg(c, 0));
}

void hle_OSInitMessageQueue(ppc_ctx* c) {
    HLE_ENTRY(c, "OSInitMessageQueue");
    uint32_t mq = arg(c, 0);
    for (uint32_t off = 0; off < 0x10; off += 4) wr32(c, mq + off, 0);
    wr32(c, mq + kArray, arg(c, 1));
    wr32(c, mq + kMsgCount, arg(c, 2));
    wr32(c, mq + kFirst, 0);
    wr32(c, mq + kUsed, 0);
}

void hle_OSSendMessage(ppc_ctx* c) {
    HLE_ENTRY(c, "OSSendMessage");
    uint32_t mq = arg(c, 0), msg = arg(c, 1), block = arg(c, 2) & 1;
    while (rd32(c, mq + kUsed) >= rd32(c, mq + kMsgCount)) {
        if (!block) {
            ret(c, 0);
            return;
        }
        sched().sleep_on(mq + kSendQ);
    }
    uint32_t count = rd32(c, mq + kMsgCount), used = rd32(c, mq + kUsed);
    uint32_t slot = (rd32(c, mq + kFirst) + used) % count;
    wr32(c, rd32(c, mq + kArray) + 4 * slot, msg);
    wr32(c, mq + kUsed, used + 1);
    sched().wakeup(mq + kRecvQ);
    ret(c, 1);
}

void hle_OSReceiveMessage(ppc_ctx* c) {
    HLE_ENTRY(c, "OSReceiveMessage");
    uint32_t mq = arg(c, 0), out = arg(c, 1), block = arg(c, 2) & 1;
    while (rd32(c, mq + kUsed) == 0) {
        if (!block) {
            ret(c, 0);
            return;
        }
        sched().sleep_on(mq + kRecvQ);
    }
    uint32_t first = rd32(c, mq + kFirst);
    if (out) wr32(c, out, rd32(c, rd32(c, mq + kArray) + 4 * first));
    wr32(c, mq + kFirst, (first + 1) % rd32(c, mq + kMsgCount));
    wr32(c, mq + kUsed, rd32(c, mq + kUsed) - 1);
    sched().wakeup(mq + kSendQ);
    ret(c, 1);
}

}  // extern "C"
