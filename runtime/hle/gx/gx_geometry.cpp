// GX geometry shims: draws (with inline vertex data from the write-gather pipe), vertex arrays,
// display lists, textures and EFB copies. Guest addresses become host pointers into guest RAM;
// aurora reads the big-endian GameCube data in place.
#include <unordered_map>

#include "hle/gx/marshal.hpp"
#include "rt/log.hpp"

using rt::arg;
using rt::gx::host_ptr;

namespace {
bool g_draw_open = false;

// aurora's PC GXTexObj is 64 bytes; the game reserves the GameCube's 32. Initialising it in place would
// overwrite the fields after it, so each guest texture object gets a host-side shadow keyed by address.
std::unordered_map<uint32_t, GXTexObj>& tex_shadows() {
    static std::unordered_map<uint32_t, GXTexObj> m;
    return m;
}
}  // namespace

namespace rt {
// Strong definition replacing the weak no-op in gfx.cpp. GXEnd is an empty inline on the GameCube, so
// guest code never ends its draws; aurora's GXEnd publishes them.
void gx_close_open_draw() {
    if (g_draw_open) {
        g_draw_open = false;
        GXEnd();
    }
}
}  // namespace rt

extern "C" {

void hle_GXBegin(ppc_ctx* c) {
    GX_SHIM(c, "GXBegin");
    GXBegin(GXPrimitive(arg(c, 0)), GXVtxFmt(arg(c, 1)), u16(arg(c, 2)));
    g_draw_open = true;  // vertex data follows as write-gather-pipe stores (see rt/mmio.cpp)
}

void hle_GXSetArray(ppc_ctx* c) {
    GX_SHIM(c, "GXSetArray");
    // The SDK passes no size: 0 = unknown, inferred from each draw's indices (aurora patch 0001).
    GXSetArray(GXAttr(arg(c, 0)), host_ptr(c, arg(c, 1)), 0, u8(arg(c, 2)), false);
}

void hle_GXCallDisplayList(ppc_ctx* c) {
    GX_SHIM(c, "GXCallDisplayList");
    GXCallDisplayList(host_ptr(c, arg(c, 0)), arg(c, 1));
}

void hle_GXBeginDisplayList(ppc_ctx* c) {
    GX_SHIM(c, "GXBeginDisplayList");
    GXBeginDisplayList(host_ptr(c, arg(c, 0)), arg(c, 1));
}

void hle_GXEndDisplayList(ppc_ctx* c) {
    GX_SHIM(c, "GXEndDisplayList");
    rt::ret(c, GXEndDisplayList());
}

void hle_GXInitTexObj(ppc_ctx* c) {
    GX_SHIM(c, "GXInitTexObj");
    GXInitTexObj(&tex_shadows()[arg(c, 0)], host_ptr(c, arg(c, 1)), u16(arg(c, 2)), u16(arg(c, 3)),
                 GXTexFmt(arg(c, 4)), GXTexWrapMode(arg(c, 5)), GXTexWrapMode(arg(c, 6)), GXBool(arg(c, 7)));
}

void hle_GXInitTexObjLOD(ppc_ctx* c) {
    GX_SHIM(c, "GXInitTexObjLOD");
    auto f = [c](int i) { return static_cast<float>(rt::farg(c, i)); };
    GXInitTexObjLOD(&tex_shadows()[arg(c, 0)], GXTexFilter(arg(c, 1)), GXTexFilter(arg(c, 2)), f(0),
                    f(1), f(2), GXBool(arg(c, 3)), GXBool(arg(c, 4)), GXAnisotropy(arg(c, 5)));
}

void hle_GXLoadTexObj(ppc_ctx* c) {
    GX_SHIM(c, "GXLoadTexObj");
    auto it = tex_shadows().find(arg(c, 0));
    if (it == tex_shadows().end()) {
        rt::log_once("gx:texobj-miss", "GXLoadTexObj of a texture object never initialised (guest %08x)", arg(c, 0));
        return;
    }
    GXLoadTexObj(&it->second, GXTexMapID(arg(c, 1)));
}

void hle_GXCopyDisp(ppc_ctx* c) {
    GX_SHIM(c, "GXCopyDisp");
    GXCopyDisp(host_ptr(c, arg(c, 0)), GXBool(arg(c, 1)));
}

void hle_GXCopyTex(ppc_ctx* c) {
    GX_SHIM(c, "GXCopyTex");
    // aurora keys EFB copies by destination pointer, so a later GXInitTexObj on the same guest
    // address samples the copy.
    GXCopyTex(host_ptr(c, arg(c, 0)), GXBool(arg(c, 1)));
}

}  // extern "C"
