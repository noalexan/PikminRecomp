// GX state shims: scalars, floats, colours (by pointer), matrices (big-endian), lists, lighting, TEV.
#include <cstdlib>
#include <vector>

#include "rt/log.hpp"

#include "hle/gx/marshal.hpp"
#include "rt/sched.hpp"

using rt::arg;
using rt::gx::color_at;

namespace {
inline uint32_t A(ppc_ctx* c, int i) { return arg(c, i); }
inline float F(ppc_ctx* c, int i) { return static_cast<float>(rt::farg(c, i)); }
}  // namespace

#define SHIM(name, ...)                       \
    extern "C" void hle_##name(ppc_ctx* c) {  \
        GX_SHIM(c, #name);                    \
        __VA_ARGS__;                          \
    }

// ---- scalar passthrough -------------------------------------------------------------------------
SHIM(GXSetAlphaCompare, GXSetAlphaCompare(GXCompare(A(c, 0)), u8(A(c, 1)), GXAlphaOp(A(c, 2)), GXCompare(A(c, 3)), u8(A(c, 4))))
SHIM(GXSetAlphaUpdate, GXSetAlphaUpdate(GXBool(A(c, 0))))
SHIM(GXSetBlendMode, GXSetBlendMode(GXBlendMode(A(c, 0)), GXBlendFactor(A(c, 1)), GXBlendFactor(A(c, 2)), GXLogicOp(A(c, 3))))
SHIM(GXSetChanCtrl, GXSetChanCtrl(GXChannelID(A(c, 0)), GXBool(A(c, 1)), GXColorSrc(A(c, 2)), GXColorSrc(A(c, 3)), A(c, 4),
                                  GXDiffuseFn(A(c, 5)), GXAttnFn(A(c, 6))))
SHIM(GXSetCoPlanar, GXSetCoPlanar(GXBool(A(c, 0))))
SHIM(GXSetColorUpdate, GXSetColorUpdate(GXBool(A(c, 0))))
SHIM(GXSetCullMode, GXSetCullMode(GXCullMode(A(c, 0))))
SHIM(GXSetCurrentMtx, GXSetCurrentMtx(A(c, 0)))
SHIM(GXSetDispCopyDst, if (std::getenv("PIKMIN_GFX_TRACE")) rt::log_once("dcd", "GXSetDispCopyDst(%u,%u)", A(c,0),A(c,1)); GXSetDispCopyDst(u16(A(c, 0)), u16(A(c, 1))))
SHIM(GXSetDispCopySrc, if (std::getenv("PIKMIN_GFX_TRACE")) rt::log_once("dcs", "GXSetDispCopySrc(%u,%u,%u,%u)", A(c,0),A(c,1),A(c,2),A(c,3)); GXSetDispCopySrc(u16(A(c, 0)), u16(A(c, 1)), u16(A(c, 2)), u16(A(c, 3))))
SHIM(GXSetDither, GXSetDither(GXBool(A(c, 0))))
SHIM(GXSetLineWidth, GXSetLineWidth(u8(A(c, 0)), GXTexOffset(A(c, 1))))
SHIM(GXSetNumChans, GXSetNumChans(u8(A(c, 0))))
SHIM(GXSetNumTevStages, GXSetNumTevStages(u8(A(c, 0))))
SHIM(GXSetNumTexGens, GXSetNumTexGens(u8(A(c, 0))))
SHIM(GXSetPixelFmt, GXSetPixelFmt(GXPixelFmt(A(c, 0)), GXZFmt16(A(c, 1))))
SHIM(GXSetPointSize, GXSetPointSize(u8(A(c, 0)), GXTexOffset(A(c, 1))))
SHIM(GXSetScissor, GXSetScissor(A(c, 0), A(c, 1), A(c, 2), A(c, 3)))
SHIM(GXSetScissorBoxOffset, GXSetScissorBoxOffset(s32(A(c, 0)), s32(A(c, 1))))
SHIM(GXSetTevAlphaIn, GXSetTevAlphaIn(GXTevStageID(A(c, 0)), GXTevAlphaArg(A(c, 1)), GXTevAlphaArg(A(c, 2)),
                                      GXTevAlphaArg(A(c, 3)), GXTevAlphaArg(A(c, 4))))
SHIM(GXSetTevAlphaOp, GXSetTevAlphaOp(GXTevStageID(A(c, 0)), GXTevOp(A(c, 1)), GXTevBias(A(c, 2)), GXTevScale(A(c, 3)),
                                      GXBool(A(c, 4)), GXTevRegID(A(c, 5))))
SHIM(GXSetTevColorIn, GXSetTevColorIn(GXTevStageID(A(c, 0)), GXTevColorArg(A(c, 1)), GXTevColorArg(A(c, 2)),
                                      GXTevColorArg(A(c, 3)), GXTevColorArg(A(c, 4))))
SHIM(GXSetTevColorOp, GXSetTevColorOp(GXTevStageID(A(c, 0)), GXTevOp(A(c, 1)), GXTevBias(A(c, 2)), GXTevScale(A(c, 3)),
                                      GXBool(A(c, 4)), GXTevRegID(A(c, 5))))
SHIM(GXSetTevKAlphaSel, GXSetTevKAlphaSel(GXTevStageID(A(c, 0)), GXTevKAlphaSel(A(c, 1))))
SHIM(GXSetTevKColorSel, GXSetTevKColorSel(GXTevStageID(A(c, 0)), GXTevKColorSel(A(c, 1))))
SHIM(GXSetTevOp, GXSetTevOp(GXTevStageID(A(c, 0)), GXTevMode(A(c, 1))))
SHIM(GXSetTevOrder, GXSetTevOrder(GXTevStageID(A(c, 0)), GXTexCoordID(A(c, 1)), GXTexMapID(A(c, 2)), GXChannelID(A(c, 3))))
SHIM(GXSetTevSwapMode, GXSetTevSwapMode(GXTevStageID(A(c, 0)), GXTevSwapSel(A(c, 1)), GXTevSwapSel(A(c, 2))))
SHIM(GXSetTevSwapModeTable, GXSetTevSwapModeTable(GXTevSwapSel(A(c, 0)), GXTevColorChan(A(c, 1)), GXTevColorChan(A(c, 2)),
                                                  GXTevColorChan(A(c, 3)), GXTevColorChan(A(c, 4))))
SHIM(GXSetTexCoordGen2, GXSetTexCoordGen2(GXTexCoordID(A(c, 0)), GXTexGenType(A(c, 1)), GXTexGenSrc(A(c, 2)), A(c, 3),
                                          GXBool(A(c, 4)), A(c, 5)))
SHIM(GXSetTexCopyDst, GXSetTexCopyDst(u16(A(c, 0)), u16(A(c, 1)), GXTexFmt(A(c, 2)), GXBool(A(c, 3))))
SHIM(GXSetTexCopySrc, GXSetTexCopySrc(u16(A(c, 0)), u16(A(c, 1)), u16(A(c, 2)), u16(A(c, 3))))
SHIM(GXSetZCompLoc, GXSetZCompLoc(GXBool(A(c, 0))))
SHIM(GXSetZMode, GXSetZMode(GXBool(A(c, 0)), GXCompare(A(c, 1)), GXBool(A(c, 2))))
SHIM(GXSetVtxAttrFmt, GXSetVtxAttrFmt(GXVtxFmt(A(c, 0)), GXAttr(A(c, 1)), GXCompCnt(A(c, 2)), GXCompType(A(c, 3)), u8(A(c, 4))))
SHIM(GXSetVtxDesc, GXSetVtxDesc(GXAttr(A(c, 0)), GXAttrType(A(c, 1))))
SHIM(GXClearVtxDesc, GXClearVtxDesc())
SHIM(GXInvalidateVtxCache, ++rt::gx_counters().invalidate_vtx; GXInvalidateVtxCache())
SHIM(GXInvalidateTexAll, GXInvalidateTexAll())
SHIM(GXPixModeSync, GXPixModeSync())
SHIM(GXFlush, GXFlush())
SHIM(GXDrawDone, GXDrawDone())

// ---- floats -------------------------------------------------------------------------------------
SHIM(GXSetViewport, GXSetViewport(F(c, 0), F(c, 1), F(c, 2), F(c, 3), F(c, 4), F(c, 5)))
SHIM(GXInitLightAttn, GXInitLightAttn(static_cast<GXLightObj*>(rt::gx::host_ptr(c, A(c, 0))), F(c, 0), F(c, 1), F(c, 2),
                                      F(c, 3), F(c, 4), F(c, 5)))
SHIM(GXInitLightDir, GXInitLightDir(static_cast<GXLightObj*>(rt::gx::host_ptr(c, A(c, 0))), F(c, 0), F(c, 1), F(c, 2)))
SHIM(GXInitLightPos, GXInitLightPos(static_cast<GXLightObj*>(rt::gx::host_ptr(c, A(c, 0))), F(c, 0), F(c, 1), F(c, 2)))
SHIM(GXInitSpecularDir, GXInitSpecularDir(static_cast<GXLightObj*>(rt::gx::host_ptr(c, A(c, 0))), F(c, 0), F(c, 1), F(c, 2)))
SHIM(GXLoadLightObjImm, GXLoadLightObjImm(static_cast<GXLightObj*>(rt::gx::host_ptr(c, A(c, 0))), GXLightID(A(c, 1))))

extern "C" void hle_GXSetDispCopyYScale(ppc_ctx* c) {
    GX_SHIM(c, "GXSetDispCopyYScale");
    rt::ret(c, GXSetDispCopyYScale(F(c, 0)));
}

// ---- colours by pointer -------------------------------------------------------------------------
SHIM(GXSetCopyClear, GXSetCopyClear(color_at(c, A(c, 0)), A(c, 1)))
SHIM(GXSetChanAmbColor, GXSetChanAmbColor(GXChannelID(A(c, 0)), color_at(c, A(c, 1))))
SHIM(GXSetChanMatColor, GXSetChanMatColor(GXChannelID(A(c, 0)), color_at(c, A(c, 1))))
SHIM(GXSetTevColor, GXSetTevColor(GXTevRegID(A(c, 0)), color_at(c, A(c, 1))))
SHIM(GXSetTevKColor, GXSetTevKColor(GXTevKColorID(A(c, 0)), color_at(c, A(c, 1))))
SHIM(GXSetTevColorS10, GXSetTevColorS10(GXTevRegID(A(c, 0)), rt::gx::color_s10_at(c, A(c, 1))))
SHIM(GXInitLightColor, GXInitLightColor(static_cast<GXLightObj*>(rt::gx::host_ptr(c, A(c, 0))), color_at(c, A(c, 1))))
SHIM(GXSetFog, GXSetFog(GXFogType(A(c, 0)), F(c, 0), F(c, 1), F(c, 2), F(c, 3), color_at(c, A(c, 1))))

// ---- matrices (big-endian rows in guest memory) -------------------------------------------------
extern "C" void hle_GXLoadPosMtxImm(ppc_ctx* c) {
    GX_SHIM(c, "GXLoadPosMtxImm");
    float m[12];
    rt::gx::floats_at(c, A(c, 0), m, 12);
    GXLoadPosMtxImm(m, A(c, 1));
}

extern "C" void hle_GXLoadNrmMtxImm(ppc_ctx* c) {
    GX_SHIM(c, "GXLoadNrmMtxImm");
    float m[9];
    rt::gx::floats_at(c, A(c, 0), m, 9);
    GXLoadNrmMtxImm(m, A(c, 1));
}

extern "C" void hle_GXLoadTexMtxImm(ppc_ctx* c) {
    GX_SHIM(c, "GXLoadTexMtxImm");
    float m[12];
    const auto type = GXTexMtxType(A(c, 2));
    rt::gx::floats_at(c, A(c, 0), m, type == GX_MTX2x4 ? 8 : 12);
    GXLoadTexMtxImm(m, A(c, 1), type);
}

extern "C" void hle_GXSetProjection(ppc_ctx* c) {
    GX_SHIM(c, "GXSetProjection");
    float m[16];
    rt::gx::floats_at(c, A(c, 0), m, 16);
    GXSetProjection(m, GXProjectionType(A(c, 1)));
}

// ---- lists --------------------------------------------------------------------------------------
extern "C" void hle_GXSetVtxDescv(ppc_ctx* c) {
    GX_SHIM(c, "GXSetVtxDescv");
    std::vector<GXVtxDescList> list;
    for (uint32_t ea = A(c, 0);; ea += 8) {
        GXVtxDescList e{GXAttr(rt::rd32(c, ea)), GXAttrType(rt::rd32(c, ea + 4))};
        list.push_back(e);
        if (e.attr == GX_VA_NULL || list.size() > 64) break;
    }
    list.back().attr = GX_VA_NULL;
    GXSetVtxDescv(list.data());
}

extern "C" void hle_GXSetVtxAttrFmtv(ppc_ctx* c) {
    GX_SHIM(c, "GXSetVtxAttrFmtv");
    std::vector<GXVtxAttrFmtList> list;
    for (uint32_t ea = A(c, 1);; ea += 16) {
        GXVtxAttrFmtList e{GXAttr(rt::rd32(c, ea)), GXCompCnt(rt::rd32(c, ea + 4)), GXCompType(rt::rd32(c, ea + 8)),
                           rt::rd8(c, ea + 12)};
        list.push_back(e);
        if (e.attr == GX_VA_NULL || list.size() > 64) break;
    }
    list.back().attr = GX_VA_NULL;
    GXSetVtxAttrFmtv(GXVtxFmt(A(c, 0)), list.data());
}

extern "C" void hle_GXSetFogRangeAdj(ppc_ctx* c) {
    GX_SHIM(c, "GXSetFogRangeAdj");
    GXFogAdjTable table{};
    const uint32_t ea = A(c, 2);
    for (int i = 0; i < 10 && ea; ++i) table.r[i] = rt::rd16(c, ea + 2 * static_cast<uint32_t>(i));
    GXSetFogRangeAdj(GXBool(A(c, 0)), u16(A(c, 1)), ea ? &table : nullptr);
}

extern "C" void hle_GXSetCopyFilter(ppc_ctx* c) {
    GX_SHIM(c, "GXSetCopyFilter");
    auto* pattern = static_cast<const u8(*)[2]>(rt::gx::host_ptr(c, A(c, 1)));
    auto* vfilter = static_cast<const u8*>(rt::gx::host_ptr(c, A(c, 3)));
    GXSetCopyFilter(GXBool(A(c, 0)), pattern, GXBool(A(c, 2)), vfilter);
}

// ---- special ------------------------------------------------------------------------------------
extern "C" void hle_GXInit(ppc_ctx* c) {
    HLE_ENTRY(c, "GXInit");
    constexpr uint32_t kFifoObj = 0x817F0000u;  // guest-visible FIFO object in runtime-reserved RAM
    std::memset(rt::host(c, kFifoObj, 0x80), 0, 0x80);
    if (rt::gfx_active()) GXInit(rt::gx::host_ptr(c, A(c, 0)), A(c, 1));
    rt::ret(c, kFifoObj);
}

extern "C" void hle_GXSetCurrentGXThread(ppc_ctx* c) {
    HLE_ENTRY(c, "GXSetCurrentGXThread");
    rt::ret(c, rt::sched().current()->ea);
}
