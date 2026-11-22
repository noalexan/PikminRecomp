/* Runs real translated game functions from main.dol and checks results. Exit 77 = skip. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"

static int failures;
#define CHECK(expr)                                                                 \
    do {                                                                            \
        if (!(expr)) {                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                             \
        }                                                                           \
    } while (0)

static uint8_t ram[PPC_RAM_SIZE];
static uint8_t lc[PPC_LC_SIZE];

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* Test-only DOL loader: copy the 18 sections into guest RAM. */
static int load_dol(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return 0; }
    fclose(f);
    for (int i = 0; i < 18; i++) {
        uint32_t off = be32(buf + i * 4), addr = be32(buf + 0x48 + i * 4), size = be32(buf + 0x90 + i * 4);
        if (size) memcpy(ram + (addr & 0x01FFFFFF), buf + off, size);
    }
    free(buf);
    return 1;
}

static void reset(ppc_ctx *c) {
    memset(c, 0, sizeof *c);
    c->mem = ram;
    c->lc = lc;
    c->r[1] = 0x80400000u;
    c->r[2] = 0x803f5120u;
    c->r[13] = 0x803e9c60u;
}

static void put_floats(uint32_t addr, const float *v, int n) {
    for (int i = 0; i < n; i++) {
        uint32_t u;
        memcpy(&u, &v[i], 4);
        u = __builtin_bswap32(u);
        memcpy(ram + (addr & 0x01FFFFFF) + 4 * i, &u, 4);
    }
}

static float get_float(uint32_t addr) {
    uint32_t u;
    memcpy(&u, ram + (addr & 0x01FFFFFF), 4);
    u = __builtin_bswap32(u);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int main(int argc, char **argv) {
    if (argc < 2 || !load_dol(argv[1])) {
        fprintf(stderr, "skip: main.dol not available\n");
        return 77;
    }
    ppc_ctx ctx;
    ppc_ctx *c = &ctx;

    /* strlen("pikmin") */
    reset(c);
    memcpy(ram + 0x500000, "pikmin", 7);
    c->r[3] = 0x80500000u;
    f_8021b194(c);
    CHECK(c->r[3] == 6);

    /* strcpy (MSL, word-at-a-time) for several alignments and lengths */
    for (int salign = 0; salign < 4; ++salign) {
        for (int dalign = 0; dalign < 4; ++dalign) {
            for (int len = 0; len < 12; ++len) {
                reset(c);
                memset(ram + 0x500000, 'x', 64);
                memset(ram + 0x500100, 0x5A, 64);
                for (int k = 0; k < len; ++k) ram[0x500000 + salign + k] = (uint8_t)('a' + k);
                ram[0x500000 + salign + len] = 0;
                c->r[3] = 0x80500100u + (uint32_t)dalign;
                c->r[4] = 0x80500000u + (uint32_t)salign;
                f_8021b0e0(c);
                int ok = c->r[3] == 0x80500100u + (uint32_t)dalign &&
                         memcmp(ram + 0x500100 + dalign, ram + 0x500000 + salign, (size_t)len + 1) == 0 &&
                         ram[0x500100 + dalign + len + 1] == 0x5A;
                if (!ok) fprintf(stderr, "strcpy salign=%d dalign=%d len=%d\n", salign, dalign, len);
                CHECK(ok);
            }
        }
    }

    /* memset(dst, 0xAB, 16) */
    reset(c);
    memset(ram + 0x500100, 0, 32);
    c->r[3] = 0x80500100u;
    c->r[4] = 0xAB;
    c->r[5] = 16;
    f_80003384(c);
    int all_ab = 1;
    for (int i = 0; i < 16; i++) all_ab &= ram[0x500100 + i] == 0xAB;
    CHECK(all_ab && ram[0x500110] == 0);

    /* __div2i(-1000000000000, 7) */
    reset(c);
    int64_t num = -1000000000000LL;
    c->r[3] = (uint32_t)((uint64_t)num >> 32);
    c->r[4] = (uint32_t)num;
    c->r[5] = 0;
    c->r[6] = 7;
    f_80216d30(c);
    int64_t q = (int64_t)(((uint64_t)c->r[3] << 32) | c->r[4]);
    CHECK(q == -142857142857LL);

    /* PSMTXIdentity + PSMTXConcat */
    reset(c);
    const float t[12] = {1, 0, 0, 5, 0, 1, 0, 6, 0, 0, 1, 7};
    put_floats(0x80500200u, t, 12);
    c->r[3] = 0x80500300u;
    f_801fabdc(c); /* PSMTXIdentity(I) */
    CHECK(get_float(0x80500300u) == 1.0f && get_float(0x80500304u) == 0.0f && get_float(0x80500314u) == 1.0f);
    c->r[3] = 0x80500200u;
    c->r[4] = 0x80500300u;
    c->r[5] = 0x80500400u;
    f_801fac08(c); /* T * I */
    int same = 1;
    for (int i = 0; i < 12; i++) same &= get_float(0x80500400u + 4 * i) == t[i];
    CHECK(same);
    c->r[3] = 0x80500200u;
    c->r[4] = 0x80500200u;
    c->r[5] = 0x80500400u;
    f_801fac08(c); /* T * T: translation doubles */
    CHECK(get_float(0x8050040Cu) == 10.0f && get_float(0x8050041Cu) == 12.0f && get_float(0x8050042Cu) == 14.0f);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
