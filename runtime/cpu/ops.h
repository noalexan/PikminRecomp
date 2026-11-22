/* Instruction helpers: rotate masks, CR/XER updates, float conversions. */
#ifndef PPC_OPS_H
#define PPC_OPS_H

#include <math.h>
#include <string.h>

#include "ppc.h"

static inline uint32_t ppc_rotl(uint32_t v, unsigned n) {
    n &= 31;
    return n ? (v << n) | (v >> (32 - n)) : v;
}

/* MASK(mb, me) with big-endian bit numbering; wraps when mb > me. */
static inline uint32_t ppc_mask(unsigned mb, unsigned me) {
    uint32_t a = 0xFFFFFFFFu >> mb;
    uint32_t b = me == 31 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> (me + 1));
    return mb <= me ? (a & b) : (a | b);
}

/* CR field n occupies bits 28-4n..31-4n; nibble LT=8 GT=4 EQ=2 SO=1. */
static inline void ppc_cr_set(ppc_ctx *c, unsigned field, uint32_t nib) {
    unsigned shift = 28 - 4 * field;
    c->cr = (c->cr & ~(0xFu << shift)) | ((nib & 0xF) << shift);
}

static inline uint32_t ppc_cr_get(ppc_ctx *c, unsigned field) {
    return (c->cr >> (28 - 4 * field)) & 0xF;
}

static inline uint32_t ppc_cr_bit(ppc_ctx *c, unsigned bi) {
    return (c->cr >> (31 - bi)) & 1;
}

static inline void ppc_cr_setbit(ppc_ctx *c, unsigned bi, uint32_t v) {
    uint32_t m = 1u << (31 - bi);
    c->cr = v ? (c->cr | m) : (c->cr & ~m);
}

static inline uint32_t ppc_so(ppc_ctx *c) {
    return (c->xer >> 31) & 1;
}

static inline void ppc_cmp_s(ppc_ctx *c, unsigned field, int32_t a, int32_t b) {
    ppc_cr_set(c, field, (a < b ? 8 : a > b ? 4 : 2) | ppc_so(c));
}

static inline void ppc_cmp_u(ppc_ctx *c, unsigned field, uint32_t a, uint32_t b) {
    ppc_cr_set(c, field, (a < b ? 8 : a > b ? 4 : 2) | ppc_so(c));
}

static inline void ppc_rc(ppc_ctx *c, uint32_t result) {
    ppc_cmp_s(c, 0, (int32_t)result, 0);
}

static inline void ppc_cmp_f(ppc_ctx *c, unsigned field, double a, double b) {
    ppc_cr_set(c, field, (isnan(a) || isnan(b)) ? 1 : a < b ? 8 : a > b ? 4 : 2);
}

static inline uint32_t ppc_get_ca(ppc_ctx *c) {
    return (c->xer >> 29) & 1;
}

static inline void ppc_set_ca(ppc_ctx *c, uint32_t ca) {
    c->xer = ca ? (c->xer | PPC_XER_CA) : (c->xer & ~PPC_XER_CA);
}

static inline void ppc_set_ov(ppc_ctx *c, uint32_t ov) {
    c->xer = ov ? (c->xer | PPC_XER_OV | PPC_XER_SO) : (c->xer & ~PPC_XER_OV);
}

static inline double ppc_round_single(double x) {
    return (double)(float)x;
}

/* fctiw/fctiwz result word: saturating, NaN -> 0x80000000. */
static inline uint32_t ppc_fctiw(double x, int round_toward_zero) {
    if (isnan(x)) {
        return 0x80000000u;
    }
    double r = round_toward_zero ? trunc(x) : nearbyint(x);
    if (r >= 2147483647.0) {
        return 0x7FFFFFFFu;
    }
    if (r <= -2147483648.0) {
        return 0x80000000u;
    }
    return (uint32_t)(int32_t)r;
}

static inline uint64_t ppc_dbits(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

static inline double ppc_bitsd(uint64_t u) {
    double d;
    memcpy(&d, &u, 8);
    return d;
}

#endif
