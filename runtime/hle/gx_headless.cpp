// Headless GX: every referenced GX call is inert. GXInit hands back a FIFO object in reserved RAM.
// Plan 3 replaces this file with the aurora-backed implementation.
#include <cstring>

#include "rt/guest.hpp"

#define GX_HEADLESS_NOOPS(X) \
    X(GXBegin) \
    X(GXBeginDisplayList) \
    X(GXCallDisplayList) \
    X(GXClearVtxDesc) \
    X(GXCopyDisp) \
    X(GXCopyTex) \
    X(GXDrawDone) \
    X(GXEndDisplayList) \
    X(GXFlush) \
    X(GXInitLightAttn) \
    X(GXInitLightColor) \
    X(GXInitLightDir) \
    X(GXInitLightPos) \
    X(GXInitSpecularDir) \
    X(GXInitTexObj) \
    X(GXInitTexObjLOD) \
    X(GXInvalidateTexAll) \
    X(GXInvalidateVtxCache) \
    X(GXLoadLightObjImm) \
    X(GXLoadNrmMtxImm) \
    X(GXLoadPosMtxImm) \
    X(GXLoadTexMtxImm) \
    X(GXLoadTexObj) \
    X(GXPixModeSync) \
    X(GXSetAlphaCompare) \
    X(GXSetAlphaUpdate) \
    X(GXSetArray) \
    X(GXSetBlendMode) \
    X(GXSetChanAmbColor) \
    X(GXSetChanCtrl) \
    X(GXSetChanMatColor) \
    X(GXSetCoPlanar) \
    X(GXSetColorUpdate) \
    X(GXSetCopyClear) \
    X(GXSetCopyFilter) \
    X(GXSetCullMode) \
    X(GXSetCurrentGXThread) \
    X(GXSetCurrentMtx) \
    X(GXSetDispCopyDst) \
    X(GXSetDispCopySrc) \
    X(GXSetDispCopyYScale) \
    X(GXSetDither) \
    X(GXSetFog) \
    X(GXSetFogRangeAdj) \
    X(GXSetLineWidth) \
    X(GXSetNumChans) \
    X(GXSetNumTevStages) \
    X(GXSetNumTexGens) \
    X(GXSetPixelFmt) \
    X(GXSetPointSize) \
    X(GXSetProjection) \
    X(GXSetScissor) \
    X(GXSetScissorBoxOffset) \
    X(GXSetTevAlphaIn) \
    X(GXSetTevAlphaOp) \
    X(GXSetTevColor) \
    X(GXSetTevColorIn) \
    X(GXSetTevColorOp) \
    X(GXSetTevColorS10) \
    X(GXSetTevKAlphaSel) \
    X(GXSetTevKColor) \
    X(GXSetTevKColorSel) \
    X(GXSetTevOp) \
    X(GXSetTevOrder) \
    X(GXSetTevSwapMode) \
    X(GXSetTevSwapModeTable) \
    X(GXSetTexCoordGen2) \
    X(GXSetTexCopyDst) \
    X(GXSetTexCopySrc) \
    X(GXSetViewport) \
    X(GXSetVtxAttrFmt) \
    X(GXSetVtxAttrFmtv) \
    X(GXSetVtxDesc) \
    X(GXSetVtxDescv) \
    X(GXSetZCompLoc) \
    X(GXSetZMode) \

extern "C" {

void hle_GXInit(ppc_ctx* c) {
    HLE_ENTRY(c, "GXInit");
    constexpr uint32_t kFifoObj = 0x817F0000u;
    std::memset(rt::host(c, kFifoObj, 0x80), 0, 0x80);
    rt::ret(c, kFifoObj);
}

#define DEFINE_NOOP(name) \
    void hle_##name(ppc_ctx* c) { HLE_ENTRY(c, #name); }
GX_HEADLESS_NOOPS(DEFINE_NOOP)
#undef DEFINE_NOOP

}  // extern "C"
