// Extra items and screens in Pikmin's own menus (see enhance/settings_menu.cpp for what they do).
//
// zen::DrawMenu builds its items from panes named heNN/hmNN in a screen file (see
// docs/superpowers/notes/blo-format.md). At boot, in every language:
// - m_select.blo / m_selec2.blo (title) get "Quit Game"; pause.blo gets "Options" and "Quit Game";
//   option.blo (title Options list) gets "Display" and "Game" (rt::blo::add_menu_items);
// - new screens are added: x_disp.blo / x_game.blo (Display / Game, from the English Sound screen),
//   x_popt.blo (the pause Options list) and x_quit.blo ("Quit Pikmin?").
// Screens are loaded from dataDir/archives/blo__<lang>.arc, so patched and new files are appended to
// the archive and its .dir (rt::arc), both served from memory through rt::set_file_override.
// DrawMenu::update is hooked: when a menu finishes on an added item, the item is recorded for
// take_menu_choice() and the menu's result is replaced by an index no game code acts on.
#include <map>
#include <string>

#include "enhance/enhance.hpp"
#include "enhance/settings_screens.hpp"
#include "rt/arc.hpp"
#include "rt/blo.hpp"
#include "rt/disc.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"

extern "C" {
__attribute__((weak)) void f_801c184c_orig(ppc_ctx* c);  // zen::DrawMenu::DrawMenu(char*, bool, bool)
__attribute__((weak)) void f_801c2c24_orig(ppc_ctx* c);  // zen::DrawMenu::update(Controller*)
}

namespace {
struct Patch {
    const char* file;
    std::vector<std::string> labels;
};
const Patch kPatches[] = {
    {"m_select.blo", {"Quit Game"}},
    {"m_selec2.blo", {"Quit Game"}},
    {"pause.blo", {"Options", "Quit Game"}},
    {"option.blo", {"Display", "Game"}},
};
constexpr const char* kLangs[] = {"eng", "fre", "ger", "ita", "spa"};

// DrawMenu fields (DrawMenu::update / start).
constexpr uint32_t kState = 0x100;  // 0 done, 1 opening, 2 choosing, 3 closing
constexpr uint32_t kSelected = 0x110;
constexpr uint32_t kCancelled = 0x1d4;  // u8: closed with B
constexpr uint32_t kNoItem = 0x7fff;    // what the game sees instead of an added item

std::map<std::string, int> g_native;  // file name -> native item count
std::map<uint32_t, int> g_menus;      // patched DrawMenu instance -> native item count
std::map<uint32_t, int> g_choice;     // DrawMenu instance -> added item it finished on

std::string basename(const std::string& p) { return p.substr(p.find_last_of('/') + 1); }

std::vector<uint8_t> slice(const std::vector<uint8_t>& arc, const rt::arc::Entry* e) {
    if (!e || static_cast<size_t>(e->offset) + e->size > arc.size()) return {};
    return {arc.begin() + e->offset, arc.begin() + e->offset + e->size};
}
}  // namespace

namespace enh {

int take_menu_choice(uint32_t menu) {
    auto it = g_choice.find(menu);
    if (it == g_choice.end()) return -1;
    const int index = it->second;
    g_choice.erase(it);
    return index;
}

void install_menu_entries() {
    int patched = 0, added = 0;
    // Display / Game come from the English Sound screen (the other languages lay it out differently).
    std::vector<uint8_t> eng_sound;
    if (const int e = rt::disc()->path_to_entry("dataDir/archives/blo__eng.arc"),
        d = rt::disc()->path_to_entry("dataDir/archives/blo__eng.dir");
        e >= 0 && d >= 0) {
        auto arc = rt::read_entry(e), dir = rt::read_entry(d);
        if (auto entries = rt::arc::parse_dir(dir)) eng_sound = slice(arc, rt::arc::find(*entries, "screen/eng_blo/s_select.blo"));
    }
    namespace sc = screens;
    const auto disp = sc::make_rows_screen(eng_sound, "Display",
                                           {{"Screen", {"Full", "Window"}}, {"Aspect", {"Wide", "4:3"}}, {"V-sync", {"On", "Off"}}});
    const auto game = sc::make_rows_screen(eng_sound, "Game",
                                           {{"FPS", {"Original", "60", "Max"}}, {"Camera", {"Modern", "Original"}}, {"HUD size", {}}});
    if (!disp || !game) rt::log("menu", "cannot build the Display / Game screens");

    for (const char* lang : kLangs) {
        const int arc_entry = rt::disc()->path_to_entry(std::string("dataDir/archives/blo__") + lang + ".arc");
        const int dir_entry = rt::disc()->path_to_entry(std::string("dataDir/archives/blo__") + lang + ".dir");
        if (arc_entry < 0 || dir_entry < 0) continue;
        auto arc = rt::read_entry(arc_entry), dir = rt::read_entry(dir_entry);
        const auto entries = rt::arc::parse_dir(dir);
        if (!entries) {
            rt::log("menu", "cannot read blo__%s.dir; menus unchanged", lang);
            continue;
        }
        const std::string prefix = std::string("screen/") + lang + "_blo/";
        auto original = [&](const char* file) { return slice(arc, rt::arc::find(*entries, prefix + file)); };
        bool changed = false;
        for (const Patch& p : kPatches) {
            const std::string name = prefix + p.file;
            const std::vector<uint8_t> orig = original(p.file);
            auto out = rt::blo::add_menu_items(orig, p.labels);
            if (!out) {
                rt::log("menu", "cannot patch %s; menu unchanged", name.c_str());
                continue;
            }
            g_native[p.file] = rt::blo::item_count(orig);  // the same in every language
            if (int loose = rt::disc()->path_to_entry("dataDir/" + name); loose >= 0) rt::set_file_override(loose, *out);
            if (rt::arc::replace(arc, dir, name, *out)) changed = true, ++patched;
        }
        const std::pair<const char*, std::optional<std::vector<uint8_t>>> extra[] = {
            {"x_disp.blo", disp},
            {"x_game.blo", game},
            {"x_popt.blo", sc::make_pause_options(original("option.blo"))},
            {"x_quit.blo", sc::make_quit_screen(original("pause_ok.blo"))},
        };
        for (const auto& [file, bytes] : extra) {
            if (!bytes) {
                rt::log("menu", "cannot build %s%s", prefix.c_str(), file);
                continue;
            }
            if (rt::arc::add(arc, dir, prefix + file, *bytes)) changed = true, ++added;
        }
        if (changed) {
            rt::set_file_override(arc_entry, std::move(arc));
            rt::set_file_override(dir_entry, std::move(dir));
        }
    }
    rt::log("menu", "patched %d menu screens, added %d settings screens", patched, added);
}

}  // namespace enh

extern "C" {

void hle___ct__Q23zen8DrawMenuFPcbb(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::DrawMenu::DrawMenu");
    const uint32_t self = rt::arg(c, 0);
    const std::string name = basename(rt::rdstr(c, rt::arg(c, 1), 256));
    g_menus.erase(self);  // a new menu at a reused address
    g_choice.erase(self);
    if (f_801c184c_orig) f_801c184c_orig(c);
    if (auto it = g_native.find(name); it != g_native.end()) g_menus[self] = it->second;
}

void hle_update__Q23zen8DrawMenuFP10Controller(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::DrawMenu::update");
    const uint32_t self = rt::arg(c, 0);
    const uint32_t before = rt::rd32(c, self + kState);
    if (f_801c2c24_orig) f_801c2c24_orig(c);
    auto it = g_menus.find(self);
    if (it == g_menus.end() || before == 0 || rt::rd32(c, self + kState) != 0 || rt::rd8(c, self + kCancelled)) return;
    const int index = static_cast<int>(rt::rd32(c, self + kSelected));
    if (index < it->second || index == static_cast<int>(kNoItem)) return;
    g_choice[self] = index - it->second;  // 0 = the first added item
    rt::wr32(c, self + kSelected, kNoItem);
    rt::log("menu", "added item %d chosen", index - it->second);
}

}  // extern "C"
