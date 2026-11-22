#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "ops.h"
#include "ppc.h"
#include "ps.h"

static int failures;
#define CHECK(expr)                                                   \
    do {                                                              \
        if (!(expr)) {                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,    \
                    __LINE__, #expr);                                 \
            failures++;                                               \
        }                                                             \
    } while (0)

/* ---- extern hooks required by ppc.h ---- */
static uint32_t mmio_last_phys, mmio_last_value;
static int mmio_last_size;
void ppc_mmio_write(uint32_t phys, uint32_t value, int size) {
    mmio_last_phys = phys;
    mmio_last_value = value;
    mmio_last_size = size;
}
uint32_t ppc_mmio_read(uint32_t phys, int size) { (void)phys; (void)size; return 0; }
static jmp_buf bad_jmp;
static int expect_bad;
void ppc_bad_access(uint32_t ea) {
    if (expect_bad) longjmp(bad_jmp, 1);
    fprintf(stderr, "bad access %08x\n", ea);
    exit(2);
}
void ppc_unimpl(ppc_ctx *c, uint32_t addr, uint32_t word) { (void)c; (void)addr; (void)word; exit(2); }
uint64_t ppc_timebase(void) { return 0; }
ppc_fn ppc_lookup(uint32_t addr) { (void)addr; return NULL; }

int main(void) {
    static uint8_t ram[PPC_RAM_SIZE];
    static uint8_t lcbuf[PPC_LC_SIZE + 16]; /* guard bytes after the locked cache */
    uint8_t *lc = lcbuf;
    ppc_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ppc_ctx *c = &ctx;
    c->mem = ram;
    c->lc = lc;

    /* rotate / mask */
    CHECK(ppc_mask(0, 29) == 0xFFFFFFFCu);
    CHECK(ppc_mask(30, 1) == 0xC0000003u);
    CHECK(ppc_rotl(0x80000001u, 1) == 3u);

    /* compares */
    ppc_cmp_s(c, 0, -1, 1);
    CHECK(ppc_cr_get(c, 0) == 8);
    ppc_cmp_u(c, 1, 0xFFFFFFFFu, 1);
    CHECK(ppc_cr_get(c, 1) == 4);
    c->xer |= PPC_XER_SO;
    ppc_rc(c, 0);
    CHECK(ppc_cr_get(c, 0) == 3);
    c->xer = 0;

    /* memory: big-endian, mirrors */
    mem_w32(c, 0x80001000u, 0x11223344u);
    CHECK(ram[0x1000] == 0x11 && ram[0x1001] == 0x22 && ram[0x1002] == 0x33 && ram[0x1003] == 0x44);
    CHECK(mem_r32(c, 0xC0001000u) == 0x11223344u);

    /* MMIO routing */
    mem_w32(c, 0xCC008000u, 5);
    CHECK(mmio_last_phys == 0x0C008000u && mmio_last_value == 5 && mmio_last_size == 4);

    /* locked cache */
    mem_w32(c, 0xE0000010u, 7);
    CHECK(lc[0x13] == 7);
    CHECK(ram[0x13] == 0);

    /* access straddling the end of the locked cache: bad access, no overflow, no RAM alias */
    expect_bad = 1;
    int faulted = 0;
    if (setjmp(bad_jmp) == 0) {
        mem_w32(c, PPC_LC_BASE + PPC_LC_SIZE - 2, 0xAABBCCDDu);
    } else {
        faulted = 1;
    }
    expect_bad = 0;
    CHECK(faulted);
    CHECK(lcbuf[PPC_LC_SIZE] == 0 && lcbuf[PPC_LC_SIZE + 1] == 0);
    CHECK(ram[PPC_LC_SIZE - 2] == 0);

    /* paired-single quantization */
    c->gqr[0] = (4u << 16) | (0u << 24); /* load u8, scale 0 */
    ram[0x2000] = 200;
    ram[0x2001] = 10;
    double p0, p1;
    ppc_psq_load(c, 0x80002000u, 0, 0, &p0, &p1);
    CHECK(p0 == 200.0 && p1 == 10.0);

    c->gqr[1] = (7u << 16) | (8u << 24); /* load s16, scale 8 */
    ram[0x2010] = 0xFF;
    ram[0x2011] = 0x38;
    ppc_psq_load(c, 0x80002010u, 1, 1, &p0, &p1);
    CHECK(p0 == -0.78125 && p1 == 1.0);

    c->gqr[2] = 4u | (1u << 8); /* store u8, scale 1 */
    ppc_psq_store(c, 0x80002020u, 1.5, 300.0, 0, 2);
    CHECK(ram[0x2020] == 3);
    CHECK(ram[0x2021] == 255);
    ppc_psq_store(c, 0x80002030u, -5.0, 0.0, 1, 2);
    CHECK(ram[0x2030] == 0);

    /* float -> int conversion */
    CHECK(ppc_fctiw(3.9, 1) == 3u);
    CHECK(ppc_fctiw(-3.9, 1) == 0xFFFFFFFDu);
    CHECK(ppc_fctiw(1e10, 1) == 0x7FFFFFFFu);
    CHECK(ppc_fctiw(-1e10, 1) == 0x80000000u);
    CHECK(ppc_fctiw(NAN, 1) == 0x80000000u);

    CHECK(ppc_round_single(0.1) == (double)0.1f);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
