/* Guest CPU context and host hooks shared by generated code and the runtime. */
#ifndef PPC_H
#define PPC_H

#include <stdint.h>

#ifdef __cplusplus
#define PPC_NORETURN [[noreturn]]
extern "C" {
#else
#define PPC_NORETURN _Noreturn
#endif

#define PPC_RAM_SIZE 0x01800000u /* 24 MB main RAM */
#define PPC_LC_BASE 0xE0000000u  /* Gekko locked cache */
#define PPC_LC_SIZE 0x4000u

#define PPC_XER_SO 0x80000000u
#define PPC_XER_OV 0x40000000u
#define PPC_XER_CA 0x20000000u

typedef struct {
    double ps0, ps1;
} ppc_ps;

typedef struct ppc_ctx {
    uint32_t r[32];
    ppc_ps f[32];
    uint32_t cr, lr, ctr, xer, fpscr, hid2;
    uint32_t gqr[8];
    uint8_t *mem; /* PPC_RAM_SIZE bytes, big-endian guest RAM */
    uint8_t *lc;  /* PPC_LC_SIZE bytes, locked cache */
} ppc_ctx;

typedef void (*ppc_fn)(ppc_ctx *);

/* Provided by the runtime (or test harnesses). */
ppc_fn ppc_lookup(uint32_t addr);
PPC_NORETURN void ppc_unimpl(ppc_ctx *c, uint32_t addr, uint32_t word);
PPC_NORETURN void ppc_bad_access(uint32_t ea);
PPC_NORETURN void ppc_hle_missing(const char *name, uint32_t addr);
uint64_t ppc_timebase(void);
uint32_t ppc_mmio_read(uint32_t phys, int size);
void ppc_mmio_write(uint32_t phys, uint32_t value, int size);

#ifdef __cplusplus
}
#endif

#endif
