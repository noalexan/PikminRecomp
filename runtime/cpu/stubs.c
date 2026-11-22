/* Default host hooks so generated code links before the real runtime exists. Each aborts loudly. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "ppc.h"

void ppc_unimpl(ppc_ctx *c, uint32_t addr, uint32_t word) {
    (void)c;
    fprintf(stderr, "ppc_unimpl: instruction 0x%08" PRIx32 " at 0x%08" PRIx32 "\n", word, addr);
    abort();
}

void ppc_bad_access(uint32_t ea) {
    fprintf(stderr, "ppc_bad_access: 0x%08" PRIx32 "\n", ea);
    abort();
}

void ppc_hle_missing(const char *name, uint32_t addr) {
    fprintf(stderr, "ppc_hle_missing: %s (0x%08" PRIx32 ") has no host implementation yet\n", name, addr);
    abort();
}

uint64_t ppc_timebase(void) {
    fprintf(stderr, "ppc_timebase: not implemented\n");
    abort();
}

uint32_t ppc_mmio_read(uint32_t phys, int size) {
    fprintf(stderr, "ppc_mmio_read: 0x%08" PRIx32 " (%d bytes)\n", phys, size);
    abort();
}

void ppc_mmio_write(uint32_t phys, uint32_t value, int size) {
    fprintf(stderr, "ppc_mmio_write: 0x%08" PRIx32 " = 0x%08" PRIx32 " (%d bytes)\n", phys, value, size);
    abort();
}

__attribute__((weak)) void ppc_poll(ppc_ctx *c, uint32_t addr) { (void)c; (void)addr; }
