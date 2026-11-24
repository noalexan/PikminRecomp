/* Paired-single quantized loads/stores driven by the GQR registers. */
#ifndef PPC_PS_H
#define PPC_PS_H

#include <math.h>
#include <string.h>

#include "memory.h"

enum { PPC_QT_F32 = 0, PPC_QT_U8 = 4, PPC_QT_U16 = 5, PPC_QT_S8 = 6, PPC_QT_S16 = 7 };

static inline int ppc_qscale(uint32_t raw6) {
    return (raw6 & 0x20) ? (int)raw6 - 64 : (int)raw6;
}

static inline unsigned ppc_qsize(unsigned type) {
    switch (type) {
    case PPC_QT_U8:
    case PPC_QT_S8:
        return 1;
    case PPC_QT_U16:
    case PPC_QT_S16:
        return 2;
    default:
        return 4;
    }
}

/* 2^k as a double (|k| <= 32): multiplying by it is exact, like ldexp, without the libm call. */
PPC_INLINE double ppc_pow2(int k) {
    uint64_t u = (uint64_t)(1023 + k) << 52;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

PPC_INLINE double ppc_dequant(ppc_ctx *c, uint32_t ea, unsigned type, int scale) {
    switch (type) {
    case PPC_QT_U8:
        return (double)mem_r8(c, ea) * ppc_pow2(-scale);
    case PPC_QT_S8:
        return (double)(int8_t)mem_r8(c, ea) * ppc_pow2(-scale);
    case PPC_QT_U16:
        return (double)mem_r16(c, ea) * ppc_pow2(-scale);
    case PPC_QT_S16:
        return (double)(int16_t)mem_r16(c, ea) * ppc_pow2(-scale);
    default:
        return (double)mem_rf32(c, ea);
    }
}

static inline double ppc_qclamp(double v, double lo, double hi) {
    if (!(v >= lo)) { /* also catches NaN */
        return lo;
    }
    return v > hi ? hi : v;
}

PPC_INLINE void ppc_quant(ppc_ctx *c, uint32_t ea, double x, unsigned type, int scale) {
    if (type != PPC_QT_U8 && type != PPC_QT_S8 && type != PPC_QT_U16 && type != PPC_QT_S16) {
        mem_wf32(c, ea, (float)x);
        return;
    }
    double v = x * ppc_pow2(scale);
    switch (type) {
    case PPC_QT_U8:
        mem_w8(c, ea, (uint8_t)ppc_qclamp(v, 0.0, 255.0));
        break;
    case PPC_QT_S8:
        mem_w8(c, ea, (uint8_t)(int8_t)ppc_qclamp(v, -128.0, 127.0));
        break;
    case PPC_QT_U16:
        mem_w16(c, ea, (uint16_t)ppc_qclamp(v, 0.0, 65535.0));
        break;
    case PPC_QT_S16:
        mem_w16(c, ea, (uint16_t)(int16_t)ppc_qclamp(v, -32768.0, 32767.0));
        break;
    default:
        mem_wf32(c, ea, (float)x);
        break;
    }
}

PPC_INLINE void ppc_psq_load(ppc_ctx *c, uint32_t ea, int w, int gqr_idx, double *ps0, double *ps1) {
    uint32_t g = c->gqr[gqr_idx];
    unsigned type = (g >> 16) & 7;
    int scale = ppc_qscale((g >> 24) & 0x3F);
    *ps0 = ppc_dequant(c, ea, type, scale);
    *ps1 = w ? 1.0 : ppc_dequant(c, ea + ppc_qsize(type), type, scale);
}

PPC_INLINE void ppc_psq_store(ppc_ctx *c, uint32_t ea, double ps0, double ps1, int w, int gqr_idx) {
    uint32_t g = c->gqr[gqr_idx];
    unsigned type = g & 7;
    int scale = ppc_qscale((g >> 8) & 0x3F);
    ppc_quant(c, ea, ps0, type, scale);
    if (!w) {
        ppc_quant(c, ea + ppc_qsize(type), ps1, type, scale);
    }
}

#endif
