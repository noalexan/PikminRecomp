/* Guest memory access: big-endian RAM, uncached mirror, MMIO, locked cache. */
#ifndef PPC_MEMORY_H
#define PPC_MEMORY_H

#include <string.h>

#include "ppc.h"

#define PPC_LIKELY(x) __builtin_expect(!!(x), 1)
#define PPC_UNLIKELY(x) __builtin_expect(!!(x), 0)

/* Host pointer for an n-byte access, or NULL when the access is MMIO. */
static inline uint8_t *ppc_route(ppc_ctx *c, uint32_t ea, uint32_t n) {
    if (PPC_UNLIKELY(ea - PPC_LC_BASE < PPC_LC_SIZE)) {
        if (ea - PPC_LC_BASE > PPC_LC_SIZE - n) {
            ppc_bad_access(ea); /* straddles the end of the locked cache */
        }
        return c->lc + (ea - PPC_LC_BASE);
    }
    uint32_t phys = ea & 0x1FFFFFFFu;
    if (PPC_LIKELY(phys <= PPC_RAM_SIZE - n)) {
        return c->mem + phys;
    }
    if (phys >= 0x0C000000u) {
        return NULL;
    }
    ppc_bad_access(ea);
}

static inline uint8_t *mem_host(ppc_ctx *c, uint32_t ea) {
    uint32_t phys = ea & 0x1FFFFFFFu;
    if (phys >= PPC_RAM_SIZE) {
        ppc_bad_access(ea);
    }
    return c->mem + phys;
}

static inline uint8_t mem_r8(ppc_ctx *c, uint32_t ea) {
    uint8_t *p = ppc_route(c, ea, 1);
    return p ? *p : (uint8_t)ppc_mmio_read(ea & 0x1FFFFFFFu, 1);
}

static inline uint16_t mem_r16(ppc_ctx *c, uint32_t ea) {
    uint8_t *p = ppc_route(c, ea, 2);
    if (!p) {
        return (uint16_t)ppc_mmio_read(ea & 0x1FFFFFFFu, 2);
    }
    uint16_t v;
    memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}

static inline uint32_t mem_r32(ppc_ctx *c, uint32_t ea) {
    uint8_t *p = ppc_route(c, ea, 4);
    if (!p) {
        return ppc_mmio_read(ea & 0x1FFFFFFFu, 4);
    }
    uint32_t v;
    memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}

static inline uint64_t mem_r64(ppc_ctx *c, uint32_t ea) {
    uint8_t *p = ppc_route(c, ea, 8);
    if (!p) {
        uint64_t hi = ppc_mmio_read(ea & 0x1FFFFFFFu, 4);
        return (hi << 32) | ppc_mmio_read((ea + 4) & 0x1FFFFFFFu, 4);
    }
    uint64_t v;
    memcpy(&v, p, 8);
    return __builtin_bswap64(v);
}

static inline void mem_w8(ppc_ctx *c, uint32_t ea, uint8_t v) {
    uint8_t *p = ppc_route(c, ea, 1);
    if (p) {
        *p = v;
    } else {
        ppc_mmio_write(ea & 0x1FFFFFFFu, v, 1);
    }
}

static inline void mem_w16(ppc_ctx *c, uint32_t ea, uint16_t v) {
    uint8_t *p = ppc_route(c, ea, 2);
    if (p) {
        v = __builtin_bswap16(v);
        memcpy(p, &v, 2);
    } else {
        ppc_mmio_write(ea & 0x1FFFFFFFu, v, 2);
    }
}

static inline void mem_w32(ppc_ctx *c, uint32_t ea, uint32_t v) {
    uint8_t *p = ppc_route(c, ea, 4);
    if (p) {
        v = __builtin_bswap32(v);
        memcpy(p, &v, 4);
    } else {
        ppc_mmio_write(ea & 0x1FFFFFFFu, v, 4);
    }
}

static inline void mem_w64(ppc_ctx *c, uint32_t ea, uint64_t v) {
    uint8_t *p = ppc_route(c, ea, 8);
    if (p) {
        v = __builtin_bswap64(v);
        memcpy(p, &v, 8);
    } else {
        ppc_mmio_write(ea & 0x1FFFFFFFu, (uint32_t)(v >> 32), 4);
        ppc_mmio_write((ea + 4) & 0x1FFFFFFFu, (uint32_t)v, 4);
    }
}

static inline float mem_rf32(ppc_ctx *c, uint32_t ea) {
    uint32_t u = mem_r32(c, ea);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static inline double mem_rf64(ppc_ctx *c, uint32_t ea) {
    uint64_t u = mem_r64(c, ea);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static inline void mem_wf32(ppc_ctx *c, uint32_t ea, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    mem_w32(c, ea, u);
}

static inline void mem_wf64(ppc_ctx *c, uint32_t ea, double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    mem_w64(c, ea, u);
}

#endif
