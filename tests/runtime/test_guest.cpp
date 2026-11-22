#include <csetjmp>
#include <cstring>
#include <vector>

#include "check.hpp"
#include "rt/guest.hpp"

static std::jmp_buf g_jmp;
static bool g_expect_bad = false;

extern "C" {
ppc_fn ppc_lookup(uint32_t) { return nullptr; }
uint32_t ppc_mmio_read(uint32_t, int) { return 0; }
void ppc_mmio_write(uint32_t, uint32_t, int) {}
void ppc_bad_access(uint32_t ea) {
    if (g_expect_bad) std::longjmp(g_jmp, 1);
    std::fprintf(stderr, "bad access %08x\n", ea);
    std::exit(2);
}
}

int main() {
    std::vector<uint8_t> ram(PPC_RAM_SIZE);
    ppc_ctx ctx{};
    ppc_ctx* c = &ctx;
    c->mem = ram.data();

    c->r[3] = 11;
    c->r[4] = 22;
    CHECK(rt::arg(c, 0) == 11 && rt::arg(c, 1) == 22);

    rt::ret64(c, 0x0000000100000002ull);
    CHECK(c->r[3] == 1 && c->r[4] == 2);

    rt::wr32(c, 0x80001000u, 0xDEADBEEFu);
    CHECK(ram[0x1000] == 0xDE && ram[0x1003] == 0xEF);
    CHECK(rt::rd32(c, 0x80001000u) == 0xDEADBEEFu);

    std::memcpy(&ram[0x2000], "pikmin\0junk", 11);
    CHECK(rt::rdstr(c, 0x80002000u) == "pikmin");

    g_expect_bad = true;
    bool faulted = false;
    if (setjmp(g_jmp) == 0) {
        rt::host(c, 0x817FFFF0u, 32);
    } else {
        faulted = true;
    }
    g_expect_bad = false;
    CHECK(faulted);
    CHECK(rt::host(c, 0x80001000u, 4) == &ram[0x1000]);

    return check_result();
}
