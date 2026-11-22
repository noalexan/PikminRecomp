#include "rt/mmio.hpp"

#include <cstdio>
#include <unordered_map>

#include "rt/guest.hpp"
#include "rt/log.hpp"

namespace rt {
namespace {
std::unordered_map<uint32_t, uint32_t>& regs() {
    static std::unordered_map<uint32_t, uint32_t> r;
    return r;
}
uint64_t g_wgpipe_bytes = 0;

void note(uint32_t phys, const char* kind) {
    char key[32];
    std::snprintf(key, sizeof key, "mmio:%s:%08x", kind, phys);
    log_once(key, "mmio %s 0x%08x (from %s, lr %08x)", kind, phys, current_function.name, current_function.lr);
}
}  // namespace

uint64_t mmio_wgpipe_bytes() { return g_wgpipe_bytes; }

}  // namespace rt

extern "C" {

__attribute__((weak)) uint32_t ppc_mmio_read(uint32_t phys, int size) {
    rt::note(phys, "read");
    auto it = rt::regs().find(phys);
    uint32_t v = it == rt::regs().end() ? 0 : it->second;
    return size == 4 ? v : size == 2 ? (v & 0xFFFF) : (v & 0xFF);
}

__attribute__((weak)) void ppc_mmio_write(uint32_t phys, uint32_t value, int size) {
    if (phys == rt::kWgPipe) {
        rt::g_wgpipe_bytes += static_cast<uint64_t>(size);
        return;
    }
    rt::note(phys, "write");
    rt::regs()[phys] = value;
}

}  // extern "C"
