// Guest ABI helpers for HLE functions: arguments, return values, guest memory.
#pragma once
#include <cstdint>
#include <string>

extern "C" {
#include "memory.h"
#include "ppc.h"
}

namespace rt {

inline uint32_t arg(ppc_ctx* c, int i) { return c->r[3 + i]; }
inline double farg(ppc_ctx* c, int i) { return c->f[1 + i].ps0; }
inline void ret(ppc_ctx* c, uint32_t v) { c->r[3] = v; }
inline void ret64(ppc_ctx* c, uint64_t v) {
    c->r[3] = static_cast<uint32_t>(v >> 32);
    c->r[4] = static_cast<uint32_t>(v);
}
inline void fret(ppc_ctx* c, double v) { c->f[1].ps0 = v; }

inline uint32_t rd32(ppc_ctx* c, uint32_t ea) { return mem_r32(c, ea); }
inline uint16_t rd16(ppc_ctx* c, uint32_t ea) { return mem_r16(c, ea); }
inline uint8_t rd8(ppc_ctx* c, uint32_t ea) { return mem_r8(c, ea); }
inline void wr32(ppc_ctx* c, uint32_t ea, uint32_t v) { mem_w32(c, ea, v); }
inline void wr16(ppc_ctx* c, uint32_t ea, uint16_t v) { mem_w16(c, ea, v); }
inline void wr8(ppc_ctx* c, uint32_t ea, uint8_t v) { mem_w8(c, ea, v); }

// Host pointer to [ea, ea+len) in guest RAM; faults if the range leaves RAM.
inline uint8_t* host(ppc_ctx* c, uint32_t ea, uint32_t len) {
    uint32_t phys = ea & 0x1FFFFFFFu;
    if (phys >= PPC_RAM_SIZE || len > PPC_RAM_SIZE - phys) {
        ppc_bad_access(ea);
    }
    return c->mem + phys;
}

inline std::string rdstr(ppc_ctx* c, uint32_t ea, size_t max = 4096) {
    std::string s;
    for (size_t i = 0; i < max; ++i) {
        char ch = static_cast<char>(rd8(c, ea + static_cast<uint32_t>(i)));
        if (!ch) break;
        s.push_back(ch);
    }
    return s;
}

// Breadcrumb of the most recent HLE call, for the watchdog and diagnostics.
struct CurrentFunction {
    const char* name = "";
    uint32_t lr = 0;
};
extern CurrentFunction current_function;

void safe_point();

}  // namespace rt

// First statement of every HLE function.
#define HLE_ENTRY(c, fname)                       \
    do {                                          \
        ::rt::current_function.name = (fname);    \
        ::rt::current_function.lr = (c)->lr;      \
        ::rt::safe_point();                       \
    } while (0)
