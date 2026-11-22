// ARAM (16 MB auxiliary RAM) and the ARAM DMA queue.
#include <cstring>
#include <vector>

#include "rt/guest.hpp"
#include "rt/irq.hpp"

using rt::arg;
using rt::ret;

namespace {
constexpr uint32_t kAramSize = 0x01000000u, kAramBase = 0x4000u;

std::vector<uint8_t>& aram() {
    static std::vector<uint8_t> a(kAramSize);
    return a;
}

uint8_t* aram_ptr(uint32_t addr, uint32_t len) {
    if (addr >= kAramSize || len > kAramSize - addr) ppc_bad_access(addr);
    return aram().data() + addr;
}
}  // namespace

extern "C" {

void hle_ARInit(ppc_ctx* c) {
    HLE_ENTRY(c, "ARInit");
    ret(c, kAramBase);
}

void hle_ARGetBaseAddress(ppc_ctx* c) {
    HLE_ENTRY(c, "ARGetBaseAddress");
    ret(c, kAramBase);
}

void hle_ARQInit(ppc_ctx* c) { HLE_ENTRY(c, "ARQInit"); }

// ARQPostRequest(request, owner, type, priority, source, dest, length, callback)
void hle_ARQPostRequest(ppc_ctx* c) {
    HLE_ENTRY(c, "ARQPostRequest");
    uint32_t req = arg(c, 0), type = arg(c, 2), src = arg(c, 4), dst = arg(c, 5), len = arg(c, 6), cb = arg(c, 7);
    if (type == 0) {
        std::memcpy(aram_ptr(dst, len), rt::host(c, src, len), len);  // main RAM -> ARAM
    } else {
        std::memcpy(rt::host(c, dst, len), aram_ptr(src, len), len);  // ARAM -> main RAM
    }
    // The host copy is instant, so the completion interrupt fires now if interrupts allow it.
    // (Games busy-wait on the flag this callback sets without making any OS call.)
    if (cb) {
        rt::irq().post([=](ppc_ctx* ic) { rt::call_guest(ic, cb, req); });
        rt::irq().deliver();
    }
}

}  // extern "C"
