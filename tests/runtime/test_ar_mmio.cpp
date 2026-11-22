#include <csetjmp>
#include <cstring>

#include "check.hpp"
#include "hle_test_util.hpp"
#include "rt/clock.hpp"
#include "rt/irq.hpp"
#include "rt/mmio.hpp"
#include "rt/sched.hpp"

static std::jmp_buf g_jmp;
static bool g_expect_bad = false;

extern "C" {
ppc_fn ppc_lookup(uint32_t addr) { return test_funcs().at(addr); }
void ppc_bad_access(uint32_t ea) {
    if (g_expect_bad) std::longjmp(g_jmp, 1);
    std::fprintf(stderr, "bad access %08x\n", ea);
    std::exit(2);
}
void hle_ARInit(ppc_ctx*);
void hle_ARGetBaseAddress(ppc_ctx*);
void hle_ARQPostRequest(ppc_ctx*);
}

static int g_cb = 0;
static uint32_t g_cb_req = 0;
static void arq_cb(ppc_ctx* c) {
    ++g_cb;
    g_cb_req = c->r[3];
}

int main() {
    ppc_ctx c = make_ctx();
    rt::sched().init(&c, 0x817F0100u);
    rt::clock().init(rt::Clock::Mode::Fast);
    rt::irq().init(&c);

    CHECK(call(hle_ARInit, &c, {0, 0}) == 0x4000u);
    CHECK(call(hle_ARGetBaseAddress, &c, {}) == 0x4000u);

    // RAM -> ARAM -> RAM round trip; callbacks deferred to a safe point
    for (int i = 0; i < 64; ++i) test_ram()[0x300000 + i] = static_cast<uint8_t>(i * 3);
    uint32_t cb = fake_fn(arq_cb);
    const uint32_t REQ = 0x80310000u;
    // DMA completes instantly on the host: the completion "interrupt" fires before
    // ARQPostRequest returns (games busy-wait on the flag the callback sets)...
    call(hle_ARQPostRequest, &c, {REQ, 0, 0, 1, 0x80300000u, 0x4000u, 64, cb});
    CHECK(g_cb == 1 && g_cb_req == REQ);
    // ...unless interrupts are disabled, in which case it waits until they are re-enabled.
    bool was = rt::irq().disable();
    call(hle_ARQPostRequest, &c, {REQ + 0x20, 0, 1, 1, 0x4000u, 0x80300100u, 64, cb});
    CHECK(g_cb == 1);
    rt::irq().restore(was);
    CHECK(g_cb == 2 && g_cb_req == REQ + 0x20);
    CHECK(std::memcmp(&test_ram()[0x300000], &test_ram()[0x300100], 64) == 0);

    // ARAM address out of range faults
    g_expect_bad = true;
    bool faulted = false;
    if (setjmp(g_jmp) == 0) {
        call(hle_ARQPostRequest, &c, {REQ, 0, 0, 1, 0x80300000u, 0x00FFFFF0u, 64, 0});
    } else {
        faulted = true;
    }
    g_expect_bad = false;
    CHECK(faulted);

    // MMIO register file and write-gather pipe accounting
    mem_w16(&c, 0xCC00500Au, 0x1234);
    CHECK(mem_r16(&c, 0xCC00500Au) == 0x1234);
    uint64_t before = rt::mmio_wgpipe_bytes();
    mem_w32(&c, 0xCC008000u, 0xAABBCCDDu);
    mem_w8(&c, 0xCC008000u, 0x90);
    CHECK(rt::mmio_wgpipe_bytes() == before + 5);

    return check_result();
}
