#include <cstring>
#include <string>
#include <vector>

#include "check.hpp"
#include "hle_test_util.hpp"
#include "rt/boot.hpp"
#include "rt/clock.hpp"
#include "rt/irq.hpp"
#include "rt/sched.hpp"

DEFINE_TEST_HOOKS

extern "C" {
void hle_OSInitAlloc(ppc_ctx*);
void hle_DCZeroRange(ppc_ctx*);
void hle_OSGetArenaLo(ppc_ctx*);
void hle_OSGetArenaHi(ppc_ctx*);
void hle_OSSetArenaLo(ppc_ctx*);
}

static void put32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = x >> 24; v[off + 1] = x >> 16; v[off + 2] = x >> 8; v[off + 3] = x;
}

static std::vector<uint8_t> make_dol() {
    std::vector<uint8_t> dol(0x100 + 4 + 8);
    put32(dol, 0x00, 0x100);        // T0 offset
    put32(dol, 0x48, 0x80003100);   // T0 address
    put32(dol, 0x90, 4);            // T0 size
    put32(dol, 0x1C, 0x104);        // D0 offset
    put32(dol, 0x64, 0x80300000);   // D0 address
    put32(dol, 0xAC, 8);            // D0 size
    put32(dol, 0xD8, 0x80310000);   // BSS address
    put32(dol, 0xDC, 0x100);        // BSS size
    put32(dol, 0xE0, 0x80003100);   // entry
    put32(dol, 0x100, 0x4E800020);  // blr
    put32(dol, 0x104, 0x11223344);
    put32(dol, 0x108, 0x55667788);
    return dol;
}

static std::string g_log;
static void ctor_a(ppc_ctx*) { g_log += "a"; }
static void ctor_b(ppc_ctx*) { g_log += "b"; }

int main() {
    ppc_ctx c = make_ctx();
    auto& ram = test_ram();

    // DOL loading
    std::memset(&ram[0x310000], 0xAA, 0x200);
    auto dol = make_dol();
    CHECK(rt::load_dol(&c, dol) == 0x80003100u);
    CHECK(rt::rd32(&c, 0x80003100u) == 0x4E800020u);
    CHECK(rt::rd32(&c, 0x80300004u) == 0x55667788u);
    CHECK(ram[0x310000] == 0 && ram[0x3100FF] == 0 && ram[0x310100] == 0xAA);

    // The DOL header's BSS range spans .bss..sbss2 and overlaps initialised .sdata: data wins.
    auto overlap = make_dol();
    put32(overlap, 0xD8, 0x80300000);  // BSS range starts at the data section...
    put32(overlap, 0xDC, 0x20);        // ...and covers it
    std::memset(&ram[0x300000], 0xAA, 0x20);
    rt::load_dol(&c, overlap);
    CHECK(rt::rd32(&c, 0x80300000u) == 0x11223344u && rt::rd32(&c, 0x80300004u) == 0x55667788u);
    CHECK(ram[0x300008] == 0 && ram[0x30001F] == 0);

    bool threw = false;
    try {
        dol.resize(0x104);  // data section now past the end
        rt::load_dol(&c, dol);
    } catch (const rt::BootError&) {
        threw = true;
    }
    CHECK(threw);

    // BootInfo
    uint8_t header[0x20] = {'G', 'P', 'I', 'P', '0', '1'};
    rt::write_boot_info(&c, header);
    CHECK(std::memcmp(&ram[0], "GPIP01", 6) == 0);
    CHECK(rt::rd32(&c, 0x80000020u) == 0x0D15EA5Eu);
    CHECK(rt::rd32(&c, 0x80000028u) == 0x01800000u);
    CHECK(rt::rd32(&c, 0x8000002Cu) == 3u);
    CHECK(rt::rd32(&c, 0x80000030u) == rt::kArenaLo && rt::rd32(&c, 0x80000034u) == rt::kArenaHi);
    CHECK(rt::rd32(&c, 0x800000F8u) == 162000000u && rt::rd32(&c, 0x800000FCu) == 486000000u);

    // registers
    rt::init_registers(&c);
    CHECK(c.r[1] == 0x80401710u && c.r[2] == 0x803f5120u && c.r[13] == 0x803e9c60u);

    // static constructors: stop at the first zero word
    rt::sched().init(&c, 0x817F0100u);
    rt::clock().init(rt::Clock::Mode::Fast);
    rt::irq().init(&c);
    rt::wr32(&c, 0x80320000u, fake_fn(ctor_a));
    rt::wr32(&c, 0x80320004u, fake_fn(ctor_b));
    rt::wr32(&c, 0x80320008u, 0);
    rt::wr32(&c, 0x8032000Cu, 0xDEADBEEFu);  // after terminator: never called
    rt::run_constructors(&c, 0x80320000u, 0x80320010u);
    CHECK(g_log == "ab");

    // arena and heap descriptors
    CHECK(call(hle_OSGetArenaLo, &c, {}) == rt::kArenaLo);
    CHECK(call(hle_OSGetArenaHi, &c, {}) == rt::kArenaHi);
    call(hle_OSSetArenaLo, &c, {0x80500000u});
    CHECK(call(hle_OSGetArenaLo, &c, {}) == 0x80500000u);
    CHECK(rt::rd32(&c, 0x80000030u) == 0x80500000u);
    CHECK(call(hle_OSInitAlloc, &c, {0x80500000u, 0x81000000u, 4}) == 0x80500040u);

    // DCZeroRange zeroes exactly the range
    std::memset(&ram[0x400000], 0xFF, 0x40);
    call(hle_DCZeroRange, &c, {0x80400010u, 0x20});
    CHECK(ram[0x40000F] == 0xFF && ram[0x400010] == 0 && ram[0x40002F] == 0 && ram[0x400030] == 0xFF);

    return check_result();
}
