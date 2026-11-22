// GX calls not yet backed by aurora (Plan 3 Task 5 replaces these). Inert.
#include <cstring>

#include "rt/guest.hpp"

#define GX_HEADLESS_NOOPS(X) \
    X(GXBegin) \
    X(GXBeginDisplayList) \
    X(GXCallDisplayList) \
    X(GXCopyDisp) \
    X(GXCopyTex) \
    X(GXEndDisplayList) \
    X(GXInitTexObj) \
    X(GXInitTexObjLOD) \
    X(GXLoadTexObj) \
    X(GXSetArray) \

extern "C" {


#define DEFINE_NOOP(name) \
    void hle_##name(ppc_ctx* c) { HLE_ENTRY(c, #name); }
GX_HEADLESS_NOOPS(DEFINE_NOOP)
#undef DEFINE_NOOP

}  // extern "C"
