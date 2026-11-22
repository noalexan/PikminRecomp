#include <cstring>
#include <vector>

#include "check.hpp"
#include "rt/format.hpp"
#include "rt/guest.hpp"

extern "C" {
ppc_fn ppc_lookup(uint32_t) { return nullptr; }
uint32_t ppc_mmio_read(uint32_t, int) { return 0; }
void ppc_mmio_write(uint32_t, uint32_t, int) {}
void ppc_bad_access(uint32_t ea) {
    std::fprintf(stderr, "bad access %08x\n", ea);
    std::exit(2);
}
}

static std::vector<uint8_t> ram(PPC_RAM_SIZE);
static uint32_t put_str(uint32_t ea, const char* s) {
    std::memcpy(&ram[ea & 0x01FFFFFF], s, std::strlen(s) + 1);
    return ea;
}

static ppc_ctx fresh() {
    ppc_ctx c{};
    c.mem = ram.data();
    c.r[1] = 0x80400000u;
    return c;
}

int main() {
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "%d %s");
        c.r[4] = static_cast<uint32_t>(-5);
        c.r[5] = put_str(0x80100100, "pikmin");
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "-5 pikmin");
    }
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "%08X");
        c.r[4] = 0xBEEF;
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "0000BEEF");
    }
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "%.2f");
        c.f[1].ps0 = 3.14159;
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "3.14");
    }
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "%5.1f|%-4d|");
        c.f[1].ps0 = 2.25;
        c.r[4] = 7;
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "  2.2|7   |");
    }
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "%lld");
        c.r[4] = 0x12345678;  // skipped: long long starts at an odd register
        c.r[5] = 0xFFFFFFFF;
        c.r[6] = 0xFFFFFFFF;
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "-1");
    }
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "%d %d %d %d %d %d %d %d %d");
        for (int i = 0; i < 7; ++i) c.r[4 + i] = static_cast<uint32_t>(i + 1);  // r4..r10 = 1..7
        rt::wr32(&c, c.r[1] + 8, 8);   // overflow area
        rt::wr32(&c, c.r[1] + 12, 9);
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "1 2 3 4 5 6 7 8 9");
    }
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "100%%");
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "100%");
    }
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "[%s]");
        c.r[4] = 0;
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "[(null)]");
    }
    {
        ppc_ctx c = fresh();
        c.r[3] = put_str(0x80100000, "%*d|%c|%x|%u");
        c.r[4] = 4;
        c.r[5] = 42;
        c.r[6] = 'A';
        c.r[7] = 0xff;
        c.r[8] = 0xFFFFFFFF;
        CHECK(rt::guest_format(&c, c.r[3], 4, 1) == "  42|A|ff|4294967295");
    }
    return check_result();
}
