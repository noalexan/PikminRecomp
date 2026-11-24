// Cheats, driven by the cheat menu (ui/cheat_menu.cpp, F1 or L3 + R3).
//
// The menu runs in the host frame (gfx_present) and only queues requests; they are carried out at
// the top of the next game frame (System::updateSysClock hook, enh::cheats_frame), on the game
// thread, by calling the game's own functions:
// Only while a captain is being updated (gameplay). 
// - Spawn pikmin: what Navi::procActionButton does when a sprout is plucked without the pluck
//   animation (PikiMgr::birth with meBirthMode, Piki::init(navi), initColor, setFlower,
//   resetPosition), then joined to the squad (changeMode(1 = crowd, navi)), placed in a ring around Olimar.
// - All flowers: setFlower(2) on every pikmin (PikiMgr iteration as the game does it).
// Continuous cheats:
// - Invincible: Olimar's health (Navi +0x58) is kept at its maximum (NaviProp +0x3c8).
// - Freeze time: WorldClock::update is skipped (the day's sun meter stops).
// - Fly: after Navi::update, Olimar is held at a height above the ground he took off from;
//   D-pad up/down raise and lower it (hidden from the game while flying). The creature flag 0x40
//   (+0xc8) is set meanwhile: without it the game applies the stick's velocity only on the ground.
//
// PIKMIN_CHEATS (apply_env_cheats) switches cheats on for scripted runs (tests, profiling).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "enhance/enhance.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"

extern "C" {
__attribute__((weak)) void f_80051470_orig(ppc_ctx* c);  // WorldClock::update(float)
__attribute__((weak)) void f_800faba0_orig(ppc_ctx* c);  // Navi::update()
}

namespace enh {
namespace {
// Guest globals and functions (PAL GPIP01, build.map).
constexpr uint32_t kPikiMgr = 0x803ecc48u;  // PikiMgr* pikiMgr
constexpr uint32_t kMeBirthMode = 0x803ecc4du;  // PikiMgr::meBirthMode (u8): allows one over the cap
constexpr uint32_t kPikiInit = 0x800cbefcu;     // Piki::init(Navi*)
constexpr uint32_t kInitColor = 0x800c9730u;    // Piki::initColor(int)
constexpr uint32_t kChangeMode = 0x800cd084u;   // Piki::changeMode(int, Navi*)
constexpr uint32_t kAIConstant = 0x803ecb60u;   // AIConstant* AIConstant::_instance
constexpr uint32_t kGameStatUpdate = 0x80112418u;  // GameStat::update()
constexpr uint32_t kVtBirth = 0x78;             // PikiMgr::birth()
constexpr uint32_t kVtGetCreature = 0x8, kVtGetFirst = 0xc, kVtGetNext = 0x10, kVtIsDone = 0x14;
constexpr uint32_t kVtResetPosition = 0x2c, kVtSetFlower = 0x130;  // Piki (ViewPiki)
constexpr uint16_t kPadUp = 0x0008, kPadDown = 0x0004;

Cheats g_cheats;
struct Spawn {
    int color, count, happa;
};
std::vector<Spawn> g_spawns;
bool g_all_flowers = false;
CheatStatus g_status;
uint16_t g_dpad = 0;   // D-pad up/down held (fly)
bool g_flying = false;  // fly height captured
float g_fly_y = 0.f;

float rdf(ppc_ctx* c, uint32_t ea) {
    uint32_t u = rt::rd32(c, ea);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
void wrf(ppc_ctx* c, uint32_t ea, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    rt::wr32(c, ea, u);
}

// Calls guest functions from a hook: the registers are restored afterwards and the callee runs
// on a fresh frame below the hook's stack, with scratch guest memory in it.
class GuestCall {
public:
    explicit GuestCall(ppc_ctx* c) : c_(c), saved_(*c) {
        c_->r[1] = (saved_.r[1] - kFrame) & ~0xfu;
        rt::wr32(c_, c_->r[1], saved_.r[1]);  // back chain
    }
    ~GuestCall() { *c_ = saved_; }
    uint32_t scratch() const { return c_->r[1] + 0x100; }  // 0x100 bytes the callee won't touch
    uint32_t call(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0) {
        const uint32_t sp = c_->r[1];
        c_->r[3] = a0;
        c_->r[4] = a1;
        c_->r[5] = a2;
        c_->lr = 0;
        ppc_lookup(fn)(c_);
        c_->r[1] = sp;
        return c_->r[3];
    }
    uint32_t vcall(uint32_t obj, uint32_t slot, uint32_t a1 = 0, uint32_t a2 = 0) {
        return call(rt::rd32(c_, rt::rd32(c_, obj) + slot), obj, a1, a2);
    }

private:
    static constexpr uint32_t kFrame = 0x400;
    ppc_ctx* c_;
    ppc_ctx saved_;
};

bool valid_ptr(uint32_t p) { return p >= 0x80000000u && p < 0x81800000u; }

uint32_t g_seen_navi = 0;  // the captain Navi::update ran for since the last game frame began

int for_each_piki(ppc_ctx* c, GuestCall& g, int happa) {
    const uint32_t mgr = rt::rd32(c, kPikiMgr);
    if (!valid_ptr(mgr)) return 0;
    int n = 0;
    for (uint32_t i = g.vcall(mgr, kVtGetFirst); !g.vcall(mgr, kVtIsDone, i); i = g.vcall(mgr, kVtGetNext, i)) {
        const uint32_t p = g.vcall(mgr, kVtGetCreature, i == 0xffffffffu ? 0 : i);
        if (!valid_ptr(p)) continue;
        if (happa >= 0) g.vcall(p, kVtSetFlower, static_cast<uint32_t>(happa));
        ++n;
    }
    return n;
}

void spawn(ppc_ctx* c, GuestCall& g, uint32_t navi, const Spawn& s) {
    const uint32_t mgr = rt::rd32(c, kPikiMgr);
    if (!valid_ptr(mgr)) return;
    // The game's limit of pikmin on the field (AIConstant +0x120, 100). PikiMgr::birth checks it
    // against GameStat::mapPikis, which only GameStat::update recomputes (once per frame), so
    // births in a loop never hit it: count the live pikmin instead. Past 100 the game breaks
    // (a crash in ActTransport::exec after spawning 150).
    const uint32_t aic = rt::rd32(c, kAIConstant);
    const int limit = valid_ptr(aic) ? static_cast<int>(rt::rd32(c, aic + 0x120)) : 100;
    const int room = std::max(0, std::min(limit, 100) - for_each_piki(c, g, -1));
    const int count = std::min(s.count, room);
    const float nx = rdf(c, navi + 0x94), ny = rdf(c, navi + 0x98), nz = rdf(c, navi + 0x9c);
    int made = 0;
    for (int i = 0; i < count; ++i) {
        rt::wr8(c, kMeBirthMode, 1);
        const uint32_t p = g.vcall(mgr, kVtBirth);
        rt::wr8(c, kMeBirthMode, 0);
        if (!valid_ptr(p)) break;  // the field is full (100 pikmin)
        g.call(kPikiInit, p, navi);
        g.call(kInitColor, p, static_cast<uint32_t>(s.color));
        g.vcall(p, kVtSetFlower, static_cast<uint32_t>(s.happa));
        const float a = 2.399963f * static_cast<float>(i);  // golden-angle spiral around Olimar
        const float r = 20.f + 6.f * std::sqrt(static_cast<float>(i));
        const uint32_t pos = g.scratch();
        wrf(c, pos + 0, nx + r * std::sin(a));
        wrf(c, pos + 4, ny);
        wrf(c, pos + 8, nz + r * std::cos(a));
        g.vcall(p, kVtResetPosition, pos);
        g.call(kChangeMode, p, 1, navi);  // 1 = squad (ActCrowd); 0 would leave it idle
        ++made;
    }
    g.call(kGameStatUpdate);  // mapPikis and the HUD totals
    rt::log("cheat", "spawned %d pikmin (color %d, happa %d)", made, s.color, s.happa);
}

}  // namespace

Cheats& cheats() { return g_cheats; }
const CheatStatus& cheat_status() { return g_status; }
void cheat_spawn(int color, int count, int happa) { g_spawns.push_back({color, count, happa}); }
void cheat_all_flowers() { g_all_flowers = true; }

// PIKMIN_CHEATS="fly,invincible,freeze,flowers,spawn=N[:color[:happa]]": cheats switched on when
// gameplay first starts, for scripted offscreen runs (tests, profiling).
void apply_env_cheats() {
    static bool done = false;
    if (done) return;
    done = true;
    const char* env = std::getenv("PIKMIN_CHEATS");
    if (!env) return;
    std::string s(env);
    for (size_t i = 0; i < s.size();) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        const std::string w = s.substr(i, j - i);
        i = j + 1;
        if (w == "fly") g_cheats.fly = true;
        else if (w == "invincible") g_cheats.invincible = true;
        else if (w == "freeze") g_cheats.freeze_time = true;
        else if (w == "flowers") g_all_flowers = true;
        else if (w.rfind("spawn=", 0) == 0) {
            int n = 0, color = 1, happa = 0;
            std::sscanf(w.c_str() + 6, "%d:%d:%d", &n, &color, &happa);
            if (n > 0) g_spawns.push_back({color, n, happa});
        }
    }
    rt::log("cheat", "PIKMIN_CHEATS=%s", env);
}

void cheats_frame(ppc_ctx* c) {
    // Only while gameplay runs: the captain was updated during the previous frame.
    const uint32_t navi = g_seen_navi;
    g_seen_navi = 0;
    g_status.in_game = navi != 0;
    if (navi) apply_env_cheats();
    if (!navi) {
        g_spawns.clear();
        g_all_flowers = false;
        g_flying = false;
        return;
    }
    GuestCall g(c);
    for (const Spawn& s : g_spawns) spawn(c, g, navi, s);
    g_spawns.clear();
    if (g_all_flowers) {
        rt::log("cheat", "%d pikmin turned into flowers", for_each_piki(c, g, 2));
        g_all_flowers = false;
    }
    g_status.pikmin = for_each_piki(c, g, -1);
}

void cheats_filter_pad(ppc_ctx* c, uint32_t status, bool menu_open) {
    static uint16_t held_at_close = 0;  // buttons still down when the menu closed (B, Esc = Start)
    uint16_t buttons = rt::rd16(c, status);
    if (menu_open) {  // the menu has the controller: the game sees a released pad
        for (uint32_t off = 0; off < 8; off += 4) rt::wr32(c, status + off, 0);
        rt::wr16(c, status + 8, 0);
        held_at_close = 0x1f7f;  // real buttons (0x80 is PAD_USE_ORIGIN)
        return;
    }
    held_at_close &= buttons;  // hidden from the game until released
    buttons &= static_cast<uint16_t>(~held_at_close);
    rt::wr16(c, status, buttons);
    g_dpad = g_cheats.fly ? (buttons & (kPadUp | kPadDown)) : 0;
    if (g_cheats.fly) rt::wr16(c, status, static_cast<uint16_t>(buttons & ~(kPadUp | kPadDown)));
}

}  // namespace enh

extern "C" {

void hle_update__10WorldClockFf(ppc_ctx* c) {
    HLE_ENTRY(c, "WorldClock::update");
    if (enh::g_cheats.freeze_time || !f_80051470_orig) return;
    f_80051470_orig(c);
}

void hle_update__4NaviFv(ppc_ctx* c) {
    HLE_ENTRY(c, "Navi::update");
    const uint32_t navi = rt::arg(c, 0);
    // Creature flag 0x40 (+0xc8): no gravity in Creature::moveNew, and Creature::updateAI applies
    // the walk velocity (moveVelocity) without ground under the feet (otherwise it needs flag 0x4,
    // on the ground). Set while flying so the stick still moves Olimar.
    constexpr uint32_t kFlagsOff = 0xc8, kAirMove = 0x40;
    static uint32_t s_flag_navi = 0;  // the captain we set the flag on
    if (enh::valid_ptr(navi)) {
        const uint32_t f = rt::rd32(c, navi + kFlagsOff);
        if (enh::g_cheats.fly && enh::g_flying) {
            rt::wr32(c, navi + kFlagsOff, f | kAirMove);
            s_flag_navi = navi;
        } else if (s_flag_navi == navi) {
            rt::wr32(c, navi + kFlagsOff, f & ~kAirMove);
            s_flag_navi = 0;
        }
    }
    if (f_800faba0_orig) f_800faba0_orig(c);
    if (!enh::valid_ptr(navi)) return;
    enh::g_seen_navi = navi;
    if (enh::g_cheats.invincible) {
        const uint32_t prop = rt::rd32(c, navi + 0x224);
        if (enh::valid_ptr(prop)) enh::wrf(c, navi + 0x58, enh::rdf(c, prop + 0x3c8));
    }
    if (!enh::g_cheats.fly) {
        enh::g_flying = false;
        return;
    }
    const uint32_t gsys = rt::rd32(c, enh::kGsysPtr);
    const float dt = gsys ? enh::rdf(c, gsys + enh::kFrameTimeOff) : 1.f / 30.f;
    if (!enh::g_flying) {
        enh::g_fly_y = enh::rdf(c, navi + 0x98) + 40.f;  // take off
        enh::g_flying = true;
    }
    if (enh::g_dpad & enh::kPadUp) enh::g_fly_y += 250.f * dt;
    if (enh::g_dpad & enh::kPadDown) enh::g_fly_y -= 250.f * dt;
    enh::wrf(c, navi + 0x98, enh::g_fly_y);
    enh::wrf(c, navi + 0x74, 0.f);  // no vertical velocity
    enh::wrf(c, navi + 0xa8, 0.f);
}

}  // extern "C"
