#include <unistd.h>

#include <cstring>
#include <string>

#include "check.hpp"
#include "hle_test_util.hpp"
#include "rt/clock.hpp"
#include "rt/irq.hpp"
#include "rt/sched.hpp"

DEFINE_TEST_HOOKS

extern "C" {
void hle_VIInit(ppc_ctx*);
void hle_VIWaitForRetrace(ppc_ctx*);
void hle_VIGetRetraceCount(ppc_ctx*);
void hle_VISetPostRetraceCallback(ppc_ctx*);
void hle_PADRead(ppc_ctx*);
void hle_CARDMount(ppc_ctx*);
void hle_CARDProbe(ppc_ctx*);
void hle_GXInit(ppc_ctx*);
void hle_GXSetBlendMode(ppc_ctx*);
void hle_WriteUARTN(ppc_ctx*);
void hle_DSPCheckMailFromDSP(ppc_ctx*);
}

static int g_retrace_cbs = 0;
static uint32_t g_last_count = 0;
static bool g_increasing = true;
static void retrace_cb(ppc_ctx* c) {
    if (c->r[3] <= g_last_count) g_increasing = false;
    g_last_count = c->r[3];
    ++g_retrace_cbs;
}

int main() {
    ppc_ctx c = make_ctx();
    rt::sched().init(&c, 0x817F0100u);
    rt::clock().init(rt::Clock::Mode::Fast);
    rt::irq().init(&c);

    // VI: retraces advance in fast mode; post-retrace callback runs once per retrace
    call(hle_VIInit, &c, {});
    CHECK(call(hle_VISetPostRetraceCallback, &c, {fake_fn(retrace_cb)}) == 0);
    uint32_t start = call(hle_VIGetRetraceCount, &c, {});
    for (int i = 0; i < 10; ++i) call(hle_VIWaitForRetrace, &c, {});
    rt::safe_point();
    CHECK(call(hle_VIGetRetraceCount, &c, {}) == start + 10);
    CHECK(g_retrace_cbs == 10 && g_increasing);

    // PAD: no controllers
    std::memset(&test_ram()[0x300000], 0x55, 48);
    CHECK(call(hle_PADRead, &c, {0x80300000u}) == 0);
    for (int p = 0; p < 4; ++p) {
        CHECK(static_cast<int8_t>(test_ram()[0x300000 + p * 12 + 0x0A]) == -1);
        CHECK(test_ram()[0x300000 + p * 12] == 0);
    }

    // CARD: no card inserted
    CHECK(call(hle_CARDProbe, &c, {0}) == 0);
    CHECK(static_cast<int32_t>(call(hle_CARDMount, &c, {0, 0x80300100u, 0})) == -3);

    // GX: GXInit returns the reserved FIFO object; others are inert
    CHECK(call(hle_GXInit, &c, {0x80500000u, 0x10000}) == 0x817F0000u);
    call(hle_GXSetBlendMode, &c, {1, 4, 5, 0});

    // audio stubs
    CHECK(call(hle_DSPCheckMailFromDSP, &c, {}) == 0);

    // WriteUARTN echoes guest bytes to stderr
    int fds[2];
    CHECK(pipe(fds) == 0);
    int saved = dup(2);
    dup2(fds[1], 2);
    std::memcpy(&test_ram()[0x300200], "uart!", 5);
    CHECK(call(hle_WriteUARTN, &c, {0x80300200u, 5}) == 0);
    std::fflush(stderr);
    dup2(saved, 2);
    close(fds[1]);
    char buf[16] = {};
    ssize_t n = read(fds[0], buf, sizeof buf - 1);
    CHECK(n >= 5 && std::string(buf).find("uart!") != std::string::npos);

    return check_result();
}
