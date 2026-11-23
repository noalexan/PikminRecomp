// Options > Display / Game, the pause menu's Options, and Quit Game, run as zen::DrawMenus next to
// the game's own (screens made by enhance/menu_entries.cpp).
//
// Title (zen::ogScrTitleMgr): +0x98 (s16) is the sub-menu: 0 title menu, 1 Options list, 2 Sound,
// 3 Rumble, 4 Language. We add 5 Display, 6 Game, 7 Quit: in those, our hook runs instead of the
// original update (which does nothing for them anyway) and draw.
// Pause (zen::ogScrPauseMgr): +4 is its state (0 = menu up). When the menu finishes on Options or
// Quit Game it idles (the game sees an index it ignores) and our hook runs the Options list
// (Sound, Rumble, Display, Game) or the quit confirmation over it, then re-opens the pause menu.
// The Sound and Rumble screens there do what ogScrTitleMgr::update does for them.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "enhance/enhance.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"
#include "ui/settings.hpp"

namespace rt {
bool card_busy();  // hle/card.cpp
}

extern "C" {
__attribute__((weak)) void f_8017f79c_orig(ppc_ctx* c);  // ogScrTitleMgr::ogScrTitleMgr()
__attribute__((weak)) void f_8017fd0c_orig(ppc_ctx* c);  // ogScrTitleMgr::update(Controller*)
__attribute__((weak)) void f_801806c8_orig(ppc_ctx* c);  // ogScrTitleMgr::draw(Graphics&)
__attribute__((weak)) void f_801809cc_orig(ppc_ctx* c);  // ogScrPauseMgr::ogScrPauseMgr()
__attribute__((weak)) void f_80180d7c_orig(ppc_ctx* c);  // ogScrPauseMgr::update(Controller*)
__attribute__((weak)) void f_8018113c_orig(ppc_ctx* c);  // ogScrPauseMgr::draw(Graphics&)
}

namespace {
// Guest functions (PAL GPIP01, build.map).
constexpr uint32_t kAlloc = 0x80046bd8;           // System::alloc(u32) (operator new style: r3 = size)
constexpr uint32_t kMenuCt = 0x801c184c;          // zen::DrawMenu::DrawMenu(char*, bool, bool)
constexpr uint32_t kMenuStart = 0x801c2408;       // zen::DrawMenu::start(int)
constexpr uint32_t kMenuUpdate = 0x801c2c24;      // zen::DrawMenu::update(Controller*)
constexpr uint32_t kMenuDraw = 0x801c3030;        // zen::DrawMenu::draw(Graphics&)
constexpr uint32_t kSearch = 0x801afadc;          // P2DPane::search(u32 tag, bool)
constexpr uint32_t kSetTextColor = 0x8017d1a0;    // zen::setTextColor(P2DTextBox*, P2DPicture*)
constexpr uint32_t kZenCtrlCt = 0x801bea60;       // zen::ZenController::ZenController(Controller*)
constexpr uint32_t kZenCtrlUpdate = 0x801beaf4;   // zen::ZenController::update()
constexpr uint32_t kPlaySysSe = 0x800a522c;       // SeSystem::playSysSe(int)
constexpr uint32_t kSetBgmVol = 0x8005393c, kSetSfxVol = 0x8005397c;    // GamePrefs::set*Vol(u8)
constexpr uint32_t kSetStereo = 0x800539bc, kSetVibe = 0x80053a58;      // GamePrefs::set*Mode(bool)
constexpr uint32_t kSaveOptions = 0x8007424c;     // MemoryCard::saveOptions()
constexpr uint32_t kGamePrefs = 0x803a26cc, kMemoryCard = 0x803a265c;
constexpr uint32_t kPrefFlags = 0x803a26e4;       // u32: 2 = stereo, 1 = rumble
constexpr uint32_t kPrefBgm = 0x803a26e8, kPrefSfx = 0x803a26e9;  // u8 0..10

// DrawMenu fields.
constexpr uint32_t kState = 0x100, kSelected = 0x110, kCancelNo = 0x1d0, kCancelled = 0x1d4, kFlags = 0x1cc;
constexpr uint32_t kLeft = 0x400000, kRight = 0x100000;  // ZenController +4: left/right with key repeat
constexpr int kSeChange = 0x133;

uint32_t call(ppc_ctx* c, uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    const uint32_t lr = c->lr;
    c->r[3] = a0, c->r[4] = a1, c->r[5] = a2, c->r[6] = a3;
    ppc_lookup(fn)(c);
    c->lr = lr;
    return c->r[3];
}

uint32_t tag(const char* s) { return uint32_t(uint8_t(s[0])) << 24 | uint8_t(s[1]) << 16 | uint8_t(s[2]) << 8 | uint8_t(s[3]); }

uint32_t pane(ppc_ctx* c, uint32_t menu, const char* name) { return call(c, kSearch, menu + 4, tag(name), 1); }

// A DrawMenu over "screen/blo/<file>" (the game resolves it per language), like the game builds its own.
uint32_t make_menu(ppc_ctx* c, const char* file, bool sound_style) {
    const std::string path = std::string("screen/blo/") + file;
    const uint32_t str = call(c, kAlloc, static_cast<uint32_t>(path.size() + 1 + 3) & ~3u);
    const uint32_t menu = call(c, kAlloc, 0x1d8);
    if (!str || !menu) return 0;
    for (size_t i = 0; i <= path.size(); ++i) rt::wr8(c, str + static_cast<uint32_t>(i), i < path.size() ? uint8_t(path[i]) : 0);
    call(c, kMenuCt, menu, str, 0, 0);
    if (uint32_t back = pane(c, menu, "back")) rt::wr8(c, back + 0xf0, 0);  // as the game does: no black backdrop
    if (sound_style) rt::wr32(c, menu + kFlags, 0x111);                    // as for Sound / Rumble / Language
    return menu;
}

bool finished(ppc_ctx* c, uint32_t m) { return rt::rd32(c, m + kState) == 0; }
// The game's own reading of a menu's result/cursor (-1 = cancelled with B).
int selected(ppc_ctx* c, uint32_t m) {
    if (static_cast<int32_t>(rt::rd32(c, m + kCancelNo)) < 0 && rt::rd8(c, m + kCancelled)) return -1;
    return static_cast<int>(rt::rd32(c, m + kSelected));
}
void start(ppc_ctx* c, uint32_t m, int item) { call(c, kMenuStart, m, static_cast<uint32_t>(item)); }

// Colours a dot like StereoOnOff / DispBar*: the on_c or offc text colour, fully opaque.
void dot(ppc_ctx* c, uint32_t menu, const char* name, bool on) {
    const uint32_t p = pane(c, menu, name), col = pane(c, menu, on ? "on_c" : "offc");
    if (!p || !col) return;
    call(c, kSetTextColor, col, p);
    rt::wr8(c, p + 0xf0, 0xff);
}
void bar(ppc_ctx* c, uint32_t menu, int first, int value) {  // on<first>..on<first+9>
    for (int i = 0; i < 10; ++i) {
        char n[5];
        std::snprintf(n, sizeof n, "on%02d", first + i);
        dot(c, menu, n, i < value);
    }
}

// Display / Game: rows of settings.
struct RowsScreen {
    ui::Setting rows[3];
};
const RowsScreen kDisplay{{ui::Setting::Window, ui::Setting::Aspect, ui::Setting::Vsync}};
const RowsScreen kGame{{ui::Setting::Fps, ui::Setting::Camera, ui::Setting::Hud}};

void show_rows(ppc_ctx* c, uint32_t menu, const RowsScreen& s) {
    for (int r = 0; r < 3; ++r) {
        const ui::Setting set = s.rows[r];
        if (set == ui::Setting::Hud) {
            bar(c, menu, 11, ui::value(set));
            continue;
        }
        for (int k = 0; k < ui::choices(set); ++k) {
            char n[5];
            std::snprintf(n, sizeof n, "o%d%dn", r, k);
            dot(c, menu, n, ui::value(set) == k);
        }
    }
}

// Left/right on the current row; true once the screen is finished.
bool run_rows(ppc_ctx* c, uint32_t menu, const RowsScreen& s, uint32_t zc, uint32_t ctrl) {
    rt::wr32(c, zc, ctrl);
    call(c, kZenCtrlUpdate, zc);
    call(c, kMenuUpdate, menu, ctrl);
    const uint32_t keys = rt::rd32(c, zc + 4);
    const int row = selected(c, menu);
    if (row >= 0 && row < 3 && (keys & (kLeft | kRight))) {
        const ui::Setting set = s.rows[row];
        const int v = ui::value(set), nv = v + ((keys & kRight) ? 1 : -1);
        if (nv >= ui::min_value(set) && nv < ui::choices(set)) {
            ui::set(set, nv);
            call(c, kPlaySysSe, kSeChange);
        }
    }
    show_rows(c, menu, s);
    if (!finished(c, menu)) return false;
    ui::save();
    return true;
}

[[noreturn]] void quit() {
    rt::log("settings", "quit");
    rt::exit_process(0);
}

// ---- Title --------------------------------------------------------------------------------------
enum TitleMode : int16_t { kTitleMain = 0, kTitleOptions = 1, kTitleDisplay = 5, kTitleGame = 6, kTitleQuit = 7 };
struct Title {
    uint32_t disp = 0, game = 0, quit = 0;
    bool quitting = false;
};
std::map<uint32_t, Title> g_titles;

// ---- Pause --------------------------------------------------------------------------------------
enum class Sub { None, Options, Sound, Rumble, Display, Game, Quit };
struct Pause {
    uint32_t options = 0, sound = 0, rumble = 0, disp = 0, game = 0, quit = 0, zc = 0;
    Sub sub = Sub::None;
    bool prefs_changed = false, quitting = false;
};
std::map<uint32_t, Pause> g_pauses;

bool stereo(ppc_ctx* c) { return rt::rd32(c, kPrefFlags) & 2; }
bool rumble(ppc_ctx* c) { return rt::rd32(c, kPrefFlags) & 1; }

void show_sound(ppc_ctx* c, uint32_t m) {
    dot(c, m, "on21", stereo(c));
    dot(c, m, "on22", !stereo(c));
    bar(c, m, 1, rt::rd8(c, kPrefBgm));
    bar(c, m, 11, rt::rd8(c, kPrefSfx));
}

void open_options(ppc_ctx* c, Pause& p, int item) {
    p.sub = Sub::Options;
    start(c, p.options, item);
}

void run_pause(ppc_ctx* c, uint32_t self, Pause& p, uint32_t ctrl) {
    auto update = [&](uint32_t m) { call(c, kMenuUpdate, m, ctrl); };
    switch (p.sub) {
        case Sub::None: break;
        case Sub::Options: {
            update(p.options);
            if (!finished(c, p.options)) break;
            switch (selected(c, p.options)) {
                case 0: p.sub = Sub::Sound, start(c, p.sound, 0), show_sound(c, p.sound); break;
                case 1: p.sub = Sub::Rumble, start(c, p.rumble, rumble(c) ? 0 : 1); break;
                case 2: p.sub = Sub::Display, start(c, p.disp, 0), show_rows(c, p.disp, kDisplay); break;
                case 3: p.sub = Sub::Game, start(c, p.game, 0), show_rows(c, p.game, kGame); break;
                default:  // B: back to the pause menu, saving sound/rumble like the title does
                    if (p.prefs_changed) call(c, kSaveOptions, kMemoryCard);
                    p.prefs_changed = false;
                    p.sub = Sub::None;
                    start(c, rt::rd32(c, self + 0xc), -1);
            }
            break;
        }
        case Sub::Sound: {
            rt::wr32(c, p.zc, ctrl);
            call(c, kZenCtrlUpdate, p.zc);
            update(p.sound);
            const uint32_t keys = rt::rd32(c, p.zc + 4);
            const int row = selected(c, p.sound);
            if (keys & (kLeft | kRight)) {
                const int d = (keys & kRight) ? 1 : -1;
                bool changed = false;
                if (row == 0 && stereo(c) != (d < 0)) call(c, kSetStereo, kGamePrefs, d < 0), changed = true;
                const uint32_t vol = row == 1 ? kPrefBgm : kPrefSfx;
                const int v = rt::rd8(c, vol) + d;
                if ((row == 1 || row == 2) && v >= 0 && v <= 10)
                    call(c, row == 1 ? kSetBgmVol : kSetSfxVol, kGamePrefs, static_cast<uint32_t>(v)), changed = true;
                if (changed) call(c, kPlaySysSe, kSeChange), p.prefs_changed = true;
            }
            show_sound(c, p.sound);
            if (finished(c, p.sound)) open_options(c, p, 0);
            break;
        }
        case Sub::Rumble: {
            update(p.rumble);
            const int i = selected(c, p.rumble);
            if (i >= 0 && (i == 0) != rumble(c)) call(c, kSetVibe, kGamePrefs, i == 0), p.prefs_changed = true;
            if (finished(c, p.rumble)) open_options(c, p, 1);
            break;
        }
        case Sub::Display:
            if (run_rows(c, p.disp, kDisplay, p.zc, ctrl)) open_options(c, p, 2);
            break;
        case Sub::Game:
            if (run_rows(c, p.game, kGame, p.zc, ctrl)) open_options(c, p, 3);
            break;
        case Sub::Quit:
            if (p.quitting) {
                if (!rt::card_busy()) quit();
                break;
            }
            update(p.quit);
            if (!finished(c, p.quit)) break;
            if (selected(c, p.quit) == 0) {
                p.quitting = true;
                if (!rt::card_busy()) quit();
                rt::log("settings", "quit: waiting for the memory card");
            } else {
                p.sub = Sub::None;
                start(c, rt::rd32(c, self + 0xc), -1);
            }
            break;
    }
}

uint32_t current_menu(const Pause& p) {
    switch (p.sub) {
        case Sub::Options: return p.options;
        case Sub::Sound: return p.sound;
        case Sub::Rumble: return p.rumble;
        case Sub::Display: return p.disp;
        case Sub::Game: return p.game;
        case Sub::Quit: return p.quit;
        case Sub::None: break;
    }
    return 0;
}
}  // namespace

extern "C" {

void hle___ct__Q23zen13ogScrTitleMgrFv(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrTitleMgr::ogScrTitleMgr");
    const uint32_t self = rt::arg(c, 0);
    if (f_8017f79c_orig) f_8017f79c_orig(c);
    Title t;
    t.disp = make_menu(c, "x_disp.blo", true);
    t.game = make_menu(c, "x_game.blo", true);
    t.quit = make_menu(c, "x_quit.blo", false);
    g_titles[self] = t;
    c->r[3] = self;
}

void hle_update__Q23zen13ogScrTitleMgrFP10Controller(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrTitleMgr::update");
    const uint32_t self = rt::arg(c, 0), ctrl = rt::arg(c, 1);
    auto it = g_titles.find(self);
    const int16_t mode = static_cast<int16_t>(rt::rd16(c, self + 0x98));
    if (it == g_titles.end() || rt::rd32(c, self) != 0 || mode < kTitleDisplay) {
        if (f_8017fd0c_orig) f_8017fd0c_orig(c);
        if (it == g_titles.end()) return;
        const uint32_t result = c->r[3];
        Title& t = it->second;
        // A menu finished on one of our items: open our screen.
        const uint32_t main = rt::rd32(c, self + 0x14), options = rt::rd32(c, self + 0x18);
        if (int i = enh::take_menu_choice(main); i == 0 && t.quit) {
            rt::wr16(c, self + 0x98, kTitleQuit);
            start(c, t.quit, 1);
        }
        if (int i = enh::take_menu_choice(options); i >= 0) {
            const uint32_t m = i == 0 ? t.disp : t.game;
            if (m) {
                rt::wr16(c, self + 0x98, i == 0 ? kTitleDisplay : kTitleGame);
                start(c, m, 0);
                show_rows(c, m, i == 0 ? kDisplay : kGame);
            }
        }
        c->r[3] = result;
        return;
    }
    Title& t = it->second;
    const uint32_t zc = rt::rd32(c, self + 8);
    if (mode == kTitleDisplay || mode == kTitleGame) {
        const bool disp = mode == kTitleDisplay;
        if (run_rows(c, disp ? t.disp : t.game, disp ? kDisplay : kGame, zc, ctrl)) {
            rt::wr16(c, self + 0x98, kTitleOptions);
            start(c, rt::rd32(c, self + 0x18), disp ? 4 : 5);
        }
    } else if (mode == kTitleQuit) {
        if (t.quitting) {
            if (!rt::card_busy()) quit();
        } else {
            call(c, kMenuUpdate, t.quit, ctrl);
            if (finished(c, t.quit)) {
                if (selected(c, t.quit) == 0) {
                    t.quitting = true;
                    if (!rt::card_busy()) quit();
                } else {
                    rt::wr16(c, self + 0x98, kTitleMain);
                    start(c, rt::rd32(c, self + 0x14), -1);
                }
            }
        }
    }
    c->r[3] = rt::rd32(c, self);
}

}  // extern "C"

void enh::title_draw(ppc_ctx* c) {
    const uint32_t self = rt::arg(c, 0), gfx = rt::arg(c, 1);
    auto it = g_titles.find(self);
    const int16_t mode = static_cast<int16_t>(rt::rd16(c, self + 0x98));
    const int32_t state = static_cast<int32_t>(rt::rd32(c, self));
    if (it == g_titles.end() || mode < kTitleDisplay) {
        if (f_801806c8_orig) f_801806c8_orig(c);
        return;
    }
    if (state == -1 || state == 1) return;
    const Title& t = it->second;
    const uint32_t m = mode == kTitleDisplay ? t.disp : mode == kTitleGame ? t.game : t.quit;
    if (m) call(c, kMenuDraw, m, gfx);
}

extern "C" {

void hle___ct__Q23zen13ogScrPauseMgrFv(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrPauseMgr::ogScrPauseMgr");
    const uint32_t self = rt::arg(c, 0);
    if (f_801809cc_orig) f_801809cc_orig(c);
    Pause p;
    p.options = make_menu(c, "x_popt.blo", false);
    p.sound = make_menu(c, "s_select.blo", true);
    p.rumble = make_menu(c, "v_select.blo", true);
    p.disp = make_menu(c, "x_disp.blo", true);
    p.game = make_menu(c, "x_game.blo", true);
    p.quit = make_menu(c, "x_quit.blo", false);
    if ((p.zc = call(c, kAlloc, 0x6c))) call(c, kZenCtrlCt, p.zc, 0);
    if (!p.options || !p.sound || !p.rumble || !p.disp || !p.game || !p.quit || !p.zc)
        rt::log("settings", "pause: cannot build the options menus");
    g_pauses[self] = p;
    c->r[3] = self;
}

void hle_update__Q23zen13ogScrPauseMgrFP10Controller(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrPauseMgr::update");
    const uint32_t self = rt::arg(c, 0), ctrl = rt::arg(c, 1);
    if (f_80180d7c_orig) f_80180d7c_orig(c);
    const uint32_t result = c->r[3];
    auto it = g_pauses.find(self);
    if (it == g_pauses.end()) return;
    Pause& p = it->second;
    if (rt::rd32(c, self + 4) != 0) {  // not showing its menu (fading, closed)
        p.sub = Sub::None;
        c->r[3] = result;
        return;
    }
    const uint32_t main = rt::rd32(c, self + 0xc);
    if (p.sub == Sub::None) {
        const int i = enh::take_menu_choice(main);
        if (i == 0 && p.options && p.zc) open_options(c, p, 0);
        else if (i == 1 && p.quit) p.sub = Sub::Quit, start(c, p.quit, 1);
        else if (i >= 0) start(c, main, -1);  // a screen we couldn't build: back to the menu
    } else {
        run_pause(c, self, p, ctrl);
    }
    c->r[3] = result;
}

void hle_draw__Q23zen13ogScrPauseMgrFR8Graphics(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::ogScrPauseMgr::draw");
    const uint32_t self = rt::arg(c, 0), gfx = rt::arg(c, 1);
    if (f_8018113c_orig) f_8018113c_orig(c);
    auto it = g_pauses.find(self);
    if (it == g_pauses.end() || rt::rd32(c, self + 4) == static_cast<uint32_t>(-1)) return;
    if (uint32_t m = current_menu(it->second)) call(c, kMenuDraw, m, gfx);
}

}  // extern "C"
