// Widescreen (Hor+) and the anchored HUD.
//
// aurora stretches the 640x480 logical frame over the whole window (AURORA_VIEWPORT_STRETCH), so:
// - 3D: camera frustums built with the 4:3 aspect (CullFrustum::update) get the window aspect
//   instead; projection and frustum culling both read it, so the view widens at constant vertical FOV.
// - 2D: every 2D projection (orthographic, or anything drawn by the P2D screen system or the 2D
//   effect manager) is squeezed back to 4:3 proportions around the screen centre (pillarboxed).
//   The squeeze is an affine map in NDC, folded into the projection matrix
//   (row0' = sx*row0 + ox*row3), and partial scissor rectangles get the same map. The title and
//   memory-card/file-select screens are also scissored to the 4:3 area.
// - Cinematic cameras (CamDataInfo) get the window aspect as well (title screen, cutscenes).
// - HUD (zen::DrawGameInfo screens): each top-level element is scaled by hud_scale around the
//   screen corner/edge it sits nearest to; that point maps to the real screen edge, so elements stay
//   edge-anchored on any aspect.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "enhance/enhance.hpp"
#include "rt/gfx.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"

#ifdef PIKMIN_GRAPHICS
#include <dolphin/gx.h>
#endif

extern "C" {
__attribute__((weak)) void f_8004281c_orig(ppc_ctx* c);  // CullFrustum::update(aspect, fov, near, far)
__attribute__((weak)) void f_8002a55c_orig(ppc_ctx* c);  // CamDataInfo::update(frame, Matrix4f&)
__attribute__((weak)) void f_801b161c_orig(ppc_ctx* c);  // P2DScreen::draw(x, y, const P2DGrafContext*)
__attribute__((weak)) void f_801af5ec_orig(ppc_ctx* c);  // P2DPane::draw(x, y, const P2DGrafContext*, bool)
__attribute__((weak)) void f_801b7124_orig(ppc_ctx* c);  // zen::DrawGameInfo::draw(Graphics&)
__attribute__((weak)) void f_80066ac4_orig(ppc_ctx* c);  // MapMgr::postrefresh(Graphics&)
__attribute__((weak)) void f_80044210_orig(ppc_ctx* c);  // Texture::grabBuffer(w, h, bool, bool)
__attribute__((weak)) void f_801e8300_orig(ppc_ctx* c);  // zen::EffectMgr2D::draw(Graphics&)
__attribute__((weak)) void f_801973e8_orig(ppc_ctx* c);  // zen::ogScrStartMgr::draw(Graphics&)
__attribute__((weak)) void f_8018a150_orig(ppc_ctx* c);  // zen::ogScrFileSelectMgr::draw(Graphics&)
__attribute__((weak)) void f_80192028_orig(ppc_ctx* c);  // zen::ogScrFileChkSelMgr::draw(Graphics&)
__attribute__((weak)) void f_8018d6e8_orig(ppc_ctx* c);  // zen::ogScrMemChkMgr::draw(Graphics&)
__attribute__((weak)) void f_80192884_orig(ppc_ctx* c);  // zen::ogScrMakeDefaultMgr::draw(Graphics&)
__attribute__((weak)) void f_80042c10_orig(ppc_ctx* c);  // Camera::projectWorldPoint(Graphics&, Vector3f&)
__attribute__((weak)) void f_8004bd38_orig(ppc_ctx* c);  // DGXGraphics::fillRectangle(RectArea&)
__attribute__((weak)) void f_8004b6f0_orig(ppc_ctx* c);  // DGXGraphics::drawRectangle(RectArea&, RectArea&, Vector3f*)
}

namespace enh {

float view_aspect() {
    const Settings& s = settings();
    if (s.aspect > 0.f) return s.aspect;
    uint32_t w = 0, h = 0;
    if (!rt::gfx_output_size(w, h)) return 4.f / 3.f;
    const float a = static_cast<float>(w) / static_cast<float>(h);
    return a < 4.f / 3.f ? 4.f / 3.f : a;  // narrower than 4:3: keep the original view
}

namespace {
constexpr float kW = 640.f, kH = 480.f;

// Map in logical 640x480 pixels: x' = ax + sx*(x - ax), y' = ay + sy*(y - ay).
struct Map2D {
    float ax = kW / 2, ay = kH / 2, sx = 1.f, sy = 1.f;
};

float g_proj[16];
uint32_t g_proj_type = 0;
bool g_have_proj = false;
int g_p2d_depth = 0;   // inside P2DScreen::draw
int g_clip_depth = 0;  // inside a menu screen that is clipped to 4:3 (see clip_to_4_3)
int g_hud_depth = 0;   // inside zen::DrawGameInfo::draw
bool g_viewport_full = true;
std::vector<Map2D> g_anchors;  // HUD element maps (innermost last)
bool g_in_postrefresh = false;  // inside MapMgr::postrefresh
bool g_fullscreen_fx = false;   // full-screen EFB effects: 2D spans the whole (wide) screen

float squeeze() { return (4.f / 3.f) / view_aspect(); }

bool is_2d(uint32_t type) { return g_p2d_depth > 0 || (type == 1 /*GX_ORTHOGRAPHIC*/ && g_viewport_full); }

Map2D current_map() {
    const float k = squeeze();
    if (g_anchors.empty() && g_fullscreen_fx) return Map2D{};
    Map2D m = g_anchors.empty() ? Map2D{} : g_anchors.back();
    m.sx *= k;
    return m;
}

void apply(float p[16], const Map2D& m) {
    // NDC: x_n = x/320 - 1, y_n = 1 - y/240 (GX: y up).
    const float axn = m.ax / (kW / 2) - 1.f, ayn = 1.f - m.ay / (kH / 2);
    const float ox = axn * (1.f - m.sx), oy = ayn * (1.f - m.sy);
    for (int col = 0; col < 4; ++col) {
        p[0 * 4 + col] = m.sx * p[0 * 4 + col] + ox * p[3 * 4 + col];
        p[1 * 4 + col] = m.sy * p[1 * 4 + col] + oy * p[3 * 4 + col];
    }
}

bool identity(const Map2D& m) { return m.sx == 1.f && m.sy == 1.f; }

uint32_t g_scissor[4] = {0, 0, 640, 480};  // last guest scissor, unmapped

// The title and memory-card/file-select screens are clipped to the 4:3 pillarbox: they park panes just off the
// 640x480 screen (slide-in cards, an oversized frame, bubbles), and squeezing would reveal them.
// Not every screen: in-game message boxes dim the whole screen with an oversized pane that should
// keep covering the wide screen.
bool clip_to_4_3() {
    return g_clip_depth > 0 && g_p2d_depth > 0 && g_hud_depth == 0 && g_anchors.empty() && !g_fullscreen_fx;
}

// The guest scissor in host terms for the current 2D state; false if it is passed through unchanged.
bool map_scissor(uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h) {
    if (!is_2d(g_proj_type)) return false;
    const bool full = x <= 2 && y <= 2 && x + w >= kW - 2 && y + h >= kH - 2;
    if (full && !clip_to_4_3()) return false;  // full screen (3D follows)
    const Map2D m = current_map();
    if (identity(m)) return false;
    const float x0 = m.ax + m.sx * (static_cast<float>(x) - m.ax);
    const float x1 = m.ax + m.sx * (static_cast<float>(x + w) - m.ax);
    const float y0 = m.ay + m.sy * (static_cast<float>(y) - m.ay);
    const float y1 = m.ay + m.sy * (static_cast<float>(y + h) - m.ay);
    x = static_cast<uint32_t>(std::max(0.f, std::floor(x0)));
    y = static_cast<uint32_t>(std::max(0.f, std::floor(y0)));
    w = static_cast<uint32_t>(std::max(0.f, std::ceil(x1) - static_cast<float>(x)));
    h = static_cast<uint32_t>(std::max(0.f, std::ceil(y1) - static_cast<float>(y)));
    return true;
}

// Re-sends the last guest projection and scissor, adjusted for the current 2D state.
void reissue() {
#ifdef PIKMIN_GRAPHICS
    if (!rt::gfx_active()) return;
    if (g_have_proj) {
        float m[16];
        std::memcpy(m, g_proj, sizeof m);
        if (is_2d(g_proj_type)) apply(m, current_map());
        GXSetProjection(m, GXProjectionType(g_proj_type));
    }
    uint32_t x = g_scissor[0], y = g_scissor[1], w = g_scissor[2], h = g_scissor[3];
    map_scissor(x, y, w, h);
    GXSetScissor(x, y, w, h);
#endif
}
}  // namespace

void on_set_projection(float m[16], uint32_t type) {
    // 2D particles (EffectMgr2D) project with a camera whose frustum got the window aspect
    // (CullFrustum::update below); in a 2D context that perspective goes back to 4:3 before the
    // squeeze, or it and every screen drawn after it with the same port are squeezed twice.
    if (type == 0 /*GX_PERSPECTIVE*/ && g_p2d_depth > 0 && m[0] != 0.f) {
        const float wide = view_aspect();
        if (wide > 4.f / 3.f + 0.001f && std::fabs(m[5] / m[0] - wide) < 0.01f) m[0] = m[5] * 0.75f;
    }
    std::memcpy(g_proj, m, sizeof g_proj);
    g_proj_type = type;
    g_have_proj = true;
    if (!is_2d(type)) return;
    const Map2D map = current_map();
    if (!identity(map)) apply(m, map);
}

void on_set_viewport(float x, float y, float w, float h) {
    g_viewport_full = std::fabs(x) < 1.f && std::fabs(y) < 1.f && w >= kW - 1.f && h >= kH - 1.f;
}

bool on_set_scissor(uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h) {
    g_scissor[0] = x, g_scissor[1] = y, g_scissor[2] = w, g_scissor[3] = h;
    return map_scissor(x, y, w, h);
}

namespace {
int16_t rs16(ppc_ctx* c, uint32_t ea) { return static_cast<int16_t>(rt::rd16(c, ea)); }

struct Element {
    uint32_t pane;
    float x0, y0, x1, y1;
    int group;
};

// Visits the pane tree (children list at +0xd0: node -> child at +0, next node at +0xc), collecting
// the outermost visible panes that are not (nearly) full screen. Global origin = parent's global
// origin + local rect origin (+0x18), as P2DPane::draw computes it.
void collect(ppc_ctx* c, uint32_t pane, int ox, int oy, int depth, std::vector<Element>& out) {
    if (depth > 16 || static_cast<int8_t>(rt::rd8(c, pane + 0xc)) >= 0) return;  // hidden
    const int x0 = ox + rs16(c, pane + 0x18), y0 = oy + rs16(c, pane + 0x1a);
    const int x1 = ox + rs16(c, pane + 0x1c), y1 = oy + rs16(c, pane + 0x1e);
    const int w = x1 - x0, h = y1 - y0;
    if (depth > 0 && w > 0 && h > 0 && !(w >= 0.6f * kW && h >= 0.6f * kH)) {
        out.push_back({pane, float(x0), float(y0), float(x1), float(y1), -1});
        return;
    }
    for (uint32_t node = rt::rd32(c, pane + 0xd0), n = 0; node && n < 256; node = rt::rd32(c, node + 0xc), ++n) {
        if (uint32_t child = rt::rd32(c, node)) collect(c, child, x0, y0, depth + 1, out);
    }
}
}  // namespace

std::unordered_map<uint32_t, Map2D> g_hud_plan;

// Groups the HUD's elements into clusters of touching rectangles (a counter row, the sun meter) and
// gives each cluster one anchor: the screen edge(s) it touches, else the third of the screen its
// centre lies in. Every element of a cluster scales around the same point, so clusters keep their
// internal layout and stick to their edges/corners.
void plan_hud(ppc_ctx* c, uint32_t screen, int x, int y) {
    std::vector<Element> els;
    collect(c, screen, x, y, 0, els);
    constexpr float kGap = 24.f;
    int groups = 0;
    for (auto& e : els) e.group = groups++;
    for (bool merged = true; merged;) {  // tiny n: naive transitive merge
        merged = false;
        for (auto& a : els) {
            for (auto& b : els) {
                if (a.group == b.group) continue;
                if (a.x0 - kGap <= b.x1 && b.x0 - kGap <= a.x1 && a.y0 - kGap <= b.y1 && b.y0 - kGap <= a.y1) {
                    const int from = b.group, to = a.group;
                    for (auto& e : els) {
                        if (e.group == from) e.group = to;
                    }
                    merged = true;
                }
            }
        }
    }
    const float s = settings().hud_scale;
    static const bool dump = std::getenv("PIKMIN_HUD_DUMP") != nullptr;
    for (int g = 0; g < groups; ++g) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& e : els) {
            if (e.group != g) continue;
            x0 = std::min(x0, e.x0), y0 = std::min(y0, e.y0), x1 = std::max(x1, e.x1), y1 = std::max(y1, e.y1);
        }
        if (x0 > x1) continue;
        const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        constexpr float kEdge = 64.f;
        Map2D m;
        const bool l = x0 < kEdge, r = x1 > kW - kEdge, t = y0 < kEdge, b = y1 > kH - kEdge;
        m.ax = l && r ? (cx < kW / 2 ? 0.f : kW) : l ? 0.f : r ? kW : cx < kW / 3 ? 0.f : cx > 2 * kW / 3 ? kW : kW / 2;
        m.ay = t && b ? (cy < kH / 2 ? 0.f : kH) : t ? 0.f : b ? kH : cy < kH / 3 ? 0.f : cy > 2 * kH / 3 ? kH : kH / 2;
        m.sx = m.sy = s;
        for (const auto& e : els) {
            if (e.group == g) g_hud_plan[e.pane] = m;
        }
        if (dump) rt::log_once(("hudgroup" + std::to_string(screen) + "_" + std::to_string(g)).c_str(),
                               "hud screen %08x group %.0f,%.0f-%.0f,%.0f -> anchor %.0f,%.0f", screen, x0, y0, x1, y1, m.ax, m.ay);
    }
}

}  // namespace enh

extern "C" {

// Hor+: frustums built for the 4:3 screen get the window aspect (vertical FOV unchanged).
void hle_update__11CullFrustumFffff(ppc_ctx* c) {
    HLE_ENTRY(c, "CullFrustum::update");
    const double a = c->f[1].ps0;
    const float wide = enh::view_aspect();
    if (std::fabs(a - 4.0 / 3.0) < 0.02 && wide > 4.f / 3.f + 0.001f) {
        c->f[1].ps0 = static_cast<double>(wide);
        c->f[1].ps1 = c->f[1].ps0;
    }
    if (f_8004281c_orig) f_8004281c_orig(c);
}

// Cinematic cameras (CamDataInfo, camera at +0xa8) never go through CullFrustum::update: their
// aspect (CullFrustum +0x1c4) keeps the 4:3 the constructor set, and DGXGraphics::setPerspective
// reads it when the scene is drawn (title screen, cutscenes). Give them the window aspect too.
void hle_update__11CamDataInfoFfR8Matrix4f(ppc_ctx* c) {
    HLE_ENTRY(c, "CamDataInfo::update");
    const uint32_t ea = rt::arg(c, 0) + 0xa8 + 0x1c4;
    uint32_t u = rt::rd32(c, ea);
    float a;
    std::memcpy(&a, &u, 4);
    const uint32_t self = rt::arg(c, 0);
    const float wide = enh::view_aspect();
    if (std::fabs(a - 4.f / 3.f) < 0.02f && wide > 4.f / 3.f + 0.001f) {
        std::memcpy(&u, &wide, 4);
        rt::wr32(c, ea, u);
    }
    if (f_8002a55c_orig) f_8002a55c_orig(c);
    // In-game movies (MoviePlayer active) are staged for 4:3: the intro's sky and landscape shots
    // are backdrops that end at the 4:3 frame edges. Fill the wide screen instead: keep the
    // original horizontal FOV and narrow the vertical one (crops top and bottom). The title
    // screen's cinematic (no MoviePlayer movie) stays Hor+.
    const uint32_t mp = rt::rd32(c, enh::kMoviePlayerPtr);
    if (enh::settings().cutscene_fill && wide > 4.f / 3.f + 0.001f && mp && rt::rd8(c, mp + 0x124)) {
        const uint32_t fov_ea = self + 0xa8 + 0x1cc;  // CullFrustum fov (degrees, vertical)
        uint32_t fu = rt::rd32(c, fov_ea);
        float fov;
        std::memcpy(&fov, &fu, 4);
        if (fov > 1.f && fov < 170.f) {
            constexpr float kDeg = 3.14159265f / 180.f;
            fov = 2.f * std::atan(std::tan(fov * 0.5f * kDeg) * (4.f / 3.f) / wide) / kDeg;
            std::memcpy(&fu, &fov, 4);
            rt::wr32(c, fov_ea, fu);
        }
    }
}

// MapMgr::postrefresh copies the EFB (Texture::grabBuffer) and draws it back over the whole screen
// with an orthographic projection (motion blur, screen fades). Those quads must cover the wide
// screen, so from the first grab to the end of postrefresh 2D is not pillarboxed.
void hle_postrefresh__6MapMgrFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "MapMgr::postrefresh");
    enh::g_in_postrefresh = true;
    if (f_80066ac4_orig) f_80066ac4_orig(c);
    enh::g_in_postrefresh = false;
    if (enh::g_fullscreen_fx) {
        enh::g_fullscreen_fx = false;
        enh::reissue();
    }
}

void hle_grabBuffer__7TextureFiibb(ppc_ctx* c) {
    HLE_ENTRY(c, "Texture::grabBuffer");
    if (enh::g_in_postrefresh && !enh::g_fullscreen_fx) {
        enh::g_fullscreen_fx = true;
        enh::reissue();
    }
    if (f_80044210_orig) f_80044210_orig(c);
}

namespace {
// Full-screen rectangles (fades, screen-copy effects) cover the wide screen instead of 4:3.
void full_screen_rect(ppc_ctx* c, uint32_t rect, void (*orig)(ppc_ctx*)) {
    const auto x0 = static_cast<int32_t>(rt::rd32(c, rect)), y0 = static_cast<int32_t>(rt::rd32(c, rect + 4));
    const auto x1 = static_cast<int32_t>(rt::rd32(c, rect + 8)), y1 = static_cast<int32_t>(rt::rd32(c, rect + 12));
    const bool full = !enh::g_fullscreen_fx && enh::g_anchors.empty() && x0 <= 0 && y0 <= 0 && x1 >= 640 && y1 >= 480;
    if (full) {
        enh::g_fullscreen_fx = true;
        enh::reissue();
    }
    if (orig) orig(c);
    if (full) {
        enh::g_fullscreen_fx = false;
        enh::reissue();
    }
}
}  // namespace

void hle_fillRectangle__11DGXGraphicsFR8RectArea(ppc_ctx* c) {
    HLE_ENTRY(c, "DGXGraphics::fillRectangle");
    full_screen_rect(c, rt::arg(c, 1), f_8004bd38_orig);
}

void hle_drawRectangle__11DGXGraphicsFR8RectAreaR8RectAreaP8Vector3f(ppc_ctx* c) {
    HLE_ENTRY(c, "DGXGraphics::drawRectangle");
    full_screen_rect(c, rt::arg(c, 1), f_8004b6f0_orig);
}

// World point -> 640x480 screen position (life gauges over creatures). The 3D view is wide but the
// 2D drawn at that position is pillarboxed, so pre-apply the inverse of the 2D squeeze.
void hle_projectWorldPoint__6CameraFR8GraphicsR8Vector3f(ppc_ctx* c) {
    HLE_ENTRY(c, "Camera::projectWorldPoint");
    const uint32_t v = rt::arg(c, 2);
    if (f_80042c10_orig) f_80042c10_orig(c);
    if (c->f[1].ps0 <= 0.0 || enh::g_fullscreen_fx) return;
    const float k = enh::squeeze();
    if (k >= 0.999f) return;
    uint32_t u = rt::rd32(c, v);
    float x;
    std::memcpy(&x, &u, 4);
    x = enh::kW / 2 + (x - enh::kW / 2) / k;
    std::memcpy(&u, &x, 4);
    rt::wr32(c, v, u);
}

void hle_draw__Q23zen12DrawGameInfoFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::DrawGameInfo::draw");
    ++enh::g_hud_depth;
    if (f_801b7124_orig) f_801b7124_orig(c);
    --enh::g_hud_depth;
}

void hle_draw__9P2DScreenFiiPC14P2DGrafContext(ppc_ctx* c) {
    HLE_ENTRY(c, "P2DScreen::draw");
    if (enh::g_hud_depth > 0 && enh::settings().hud_scale != 1.f) {
        enh::plan_hud(c, rt::arg(c, 0), static_cast<int32_t>(rt::arg(c, 1)), static_cast<int32_t>(rt::arg(c, 2)));
    }
    if (++enh::g_p2d_depth == 1) enh::reissue();  // the port was set before the screen draws
    if (f_801b161c_orig) f_801b161c_orig(c);
    if (--enh::g_p2d_depth == 0) enh::reissue();
}

// 2D particle effects (menu sparkles) are drawn in the P2D port between screens: squeeze them like
// the panes they decorate.
void hle_draw__Q23zen11EffectMgr2DFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::EffectMgr2D::draw");
    if (++enh::g_p2d_depth == 1) enh::reissue();
    if (f_801e8300_orig) f_801e8300_orig(c);
    if (--enh::g_p2d_depth == 0) enh::reissue();
}

namespace {
void clipped_menu(ppc_ctx* c, void (*orig)(ppc_ctx*)) {
    if (++enh::g_clip_depth == 1) enh::reissue();
    if (orig) orig(c);
    if (--enh::g_clip_depth == 0) enh::reissue();
}
}  // namespace

void hle_draw__Q23zen13ogScrTitleMgrFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrTitleMgr::draw");
    clipped_menu(c, enh::title_draw);  // the game's draw, or our settings screens (settings_menu.cpp)
}

void hle_draw__Q23zen13ogScrStartMgrFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrStartMgr::draw");
    clipped_menu(c, f_801973e8_orig);
}

void hle_draw__Q23zen18ogScrFileSelectMgrFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrFileSelectMgr::draw");
    clipped_menu(c, f_8018a150_orig);
}

void hle_draw__Q23zen18ogScrFileChkSelMgrFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrFileChkSelMgr::draw");
    clipped_menu(c, f_80192028_orig);
}

void hle_draw__Q23zen14ogScrMemChkMgrFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrMemChkMgr::draw");
    clipped_menu(c, f_8018d6e8_orig);
}

void hle_draw__Q23zen19ogScrMakeDefaultMgrFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrMakeDefaultMgr::draw");
    clipped_menu(c, f_80192884_orig);
}

// HUD elements get the map planned for them in plan_hud; their subtree draws inside it.
void hle_draw__7P2DPaneFiiPC14P2DGrafContextb(ppc_ctx* c) {
    HLE_ENTRY(c, "P2DPane::draw");
    const uint32_t pane = rt::arg(c, 0);
    bool pushed = false;
    if (enh::g_hud_depth > 0 && enh::g_anchors.empty()) {
        auto it = enh::g_hud_plan.find(pane);
        if (it != enh::g_hud_plan.end()) {
            enh::g_anchors.push_back(it->second);
            pushed = true;
            enh::reissue();
        }
    }
    if (f_801af5ec_orig) f_801af5ec_orig(c);
    if (pushed) {
        enh::g_anchors.pop_back();
        enh::reissue();
    }
}

}  // extern "C"
