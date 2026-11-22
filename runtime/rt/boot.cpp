#include "rt/boot.hpp"

#include <cstring>
#include <string>

#include "rt/log.hpp"

namespace rt {

// .ctors table bounds: _ctors .. .ctors section end (checked by tests/runtime/test_ctors_bounds.py).
constexpr uint32_t kCtorsStart = 0x80223d00;
constexpr uint32_t kCtorsEnd = 0x80223d48;

uint32_t g_arena_lo = kArenaLo, g_arena_hi = kArenaHi;
uint32_t g_language = 0;

namespace {
uint32_t be32(std::span<const uint8_t> d, size_t off) {
    return static_cast<uint32_t>(d[off]) << 24 | d[off + 1] << 16 | d[off + 2] << 8 | d[off + 3];
}
}  // namespace

uint32_t load_dol(ppc_ctx* c, std::span<const uint8_t> dol) {
    if (dol.size() < 0x100) throw BootError("DOL is smaller than its 0x100-byte header");
    for (int i = 0; i < 18; ++i) {
        uint32_t off = be32(dol, i * 4), addr = be32(dol, 0x48 + i * 4), size = be32(dol, 0x90 + i * 4);
        if (!size) continue;
        if (static_cast<uint64_t>(off) + size > dol.size()) {
            throw BootError("DOL section " + std::to_string(i) + " extends past the end of the file");
        }
        std::memcpy(host(c, addr, size), dol.data() + off, size);
    }
    uint32_t bss = be32(dol, 0xD8), bss_size = be32(dol, 0xDC);
    if (bss_size) std::memset(host(c, bss, bss_size), 0, bss_size);
    return be32(dol, 0xE0);
}

void write_boot_info(ppc_ctx* c, const uint8_t* header32) {
    std::memcpy(host(c, 0x80000000u, 0x20), header32, 0x20);
    wr32(c, 0x80000020u, 0x0D15EA5Eu);
    wr32(c, 0x80000024u, 1);
    wr32(c, 0x80000028u, PPC_RAM_SIZE);
    wr32(c, 0x8000002Cu, 3);
    g_arena_lo = kArenaLo;
    g_arena_hi = kArenaHi;
    wr32(c, 0x80000030u, g_arena_lo);
    wr32(c, 0x80000034u, g_arena_hi);
    wr32(c, 0x800000F8u, 162000000u);
    wr32(c, 0x800000FCu, 486000000u);
}

void init_registers(ppc_ctx* c) {
    c->r[1] = 0x80401710u;
    c->r[2] = 0x803f5120u;
    c->r[13] = 0x803e9c60u;
    wr32(c, c->r[1], 0);  // back chain terminator
}

void run_constructors(ppc_ctx* c, uint32_t start, uint32_t end) {
    for (uint32_t p = start; p < end; p += 4) {
        uint32_t fn = rd32(c, p);
        if (!fn) break;
        c->lr = 0;
        ppc_lookup(fn)(c);
    }
}

void boot_guest(ppc_ctx* c, const uint8_t* header32) {
    write_boot_info(c, header32);
    init_registers(c);
    log("boot", "running static constructors");
    run_constructors(c, kCtorsStart, kCtorsEnd);
    log("boot", "entering main");
    c->r[3] = 0;
    c->r[4] = 0;
    c->lr = 0;
    ppc_lookup(kMainEntry)(c);
    log("boot", "main returned (r3=%u)", c->r[3]);
}

}  // namespace rt
