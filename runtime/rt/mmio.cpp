#include "rt/mmio.hpp"

#include <cstdio>
#include <unordered_map>

#include "rt/gfx.hpp"
#include "rt/guest.hpp"

#ifdef PIKMIN_GRAPHICS
#include "gx/fifo.hpp"  // aurora internal: raw big-endian FIFO writes
#endif
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
#ifdef PIKMIN_GRAPHICS
        // Inline vertex data/commands (GXPosition3f32 etc. compile to these stores): straight into
        // aurora's FIFO, byte-exact. Float stores arrive as their bit pattern.
        if (rt::gfx_active()) {
            namespace fifo = aurora::gx::fifo;
            if (size == 1) fifo::write_u8(static_cast<uint8_t>(value));
            else if (size == 2) fifo::write_u16(static_cast<uint16_t>(value));
            else fifo::write_u32(value);
        }
#endif
        return;
    }
    rt::note(phys, "write");
    rt::regs()[phys] = value;
}

}  // extern "C"
