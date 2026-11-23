// "Settings" and "Quit Game" in Pikmin's own title and pause menus.
//
// zen::DrawMenu builds its items from panes named heNN/hmNN in a screen file (see
// docs/superpowers/notes/blo-format.md). At boot every language's m_select.blo, m_selec2.blo and
// pause.blo get two more items (rt::blo). Screens are loaded from dataDir/archives/blo__<lang>.arc,
// so the patched copies are appended to the archive and its .dir is repointed (rt::arc), both
// served from memory through rt::set_file_override.
// DrawMenu::update is hooked: when a menu built from one of them is confirmed on an added item, the
// menu is re-opened on that item (the game never sees an index it doesn't know) and the overlay
// takes over.
#include <map>
#include <string>

#include "enhance/enhance.hpp"
#include "rt/arc.hpp"
#include "rt/blo.hpp"
#include "rt/disc.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"
#include "ui/overlay.hpp"

extern "C" {
__attribute__((weak)) void f_801c184c_orig(ppc_ctx* c);  // zen::DrawMenu::DrawMenu(char*, bool, bool)
__attribute__((weak)) void f_801c2c24_orig(ppc_ctx* c);  // zen::DrawMenu::update(Controller*)
__attribute__((weak)) void f_801c2408(ppc_ctx* c);       // zen::DrawMenu::start(int)
}

namespace {
constexpr const char* kFiles[] = {"m_select.blo", "m_selec2.blo", "pause.blo"};
constexpr const char* kLangs[] = {"eng", "fre", "ger", "ita", "spa"};

// DrawMenu fields (DrawMenu::update / start).
constexpr uint32_t kState = 0x100;     // 0 done, 1 opening, 2 choosing, 3 closing
constexpr uint32_t kSelected = 0x110;
constexpr uint32_t kCancelled = 0x1d4;  // u8: closed with B

std::map<std::string, int> g_native;  // file name -> native item count
std::map<uint32_t, int> g_menus;      // patched DrawMenu instance -> native item count

std::string basename(const std::string& p) { return p.substr(p.find_last_of('/') + 1); }
}  // namespace

namespace enh {

void install_menu_entries() {
    const std::vector<std::string> labels = {"Settings", "Quit Game"};
    int patched = 0;
    for (const char* lang : kLangs) {
        // The game loads screens from dataDir/archives/blo__<lang>.arc (indexed by its .dir):
        // patched screens are appended to the archive and their index entries repointed.
        const int arc_entry = rt::disc()->path_to_entry(std::string("dataDir/archives/blo__") + lang + ".arc");
        const int dir_entry = rt::disc()->path_to_entry(std::string("dataDir/archives/blo__") + lang + ".dir");
        if (arc_entry < 0 || dir_entry < 0) continue;
        auto arc = rt::read_entry(arc_entry), dir = rt::read_entry(dir_entry);
        const auto entries = rt::arc::parse_dir(dir);
        if (!entries) {
            rt::log("menu", "cannot read blo__%s.dir; menus unchanged", lang);
            continue;
        }
        bool changed = false;
        for (const char* file : kFiles) {
            const std::string name = std::string("screen/") + lang + "_blo/" + file;
            const rt::arc::Entry* e = rt::arc::find(*entries, name);
            if (!e || static_cast<size_t>(e->offset) + e->size > arc.size()) continue;
            const std::vector<uint8_t> orig(arc.begin() + e->offset, arc.begin() + e->offset + e->size);
            auto out = rt::blo::add_menu_items(orig, labels);
            if (!out) {
                rt::log("menu", "cannot patch %s; menu unchanged", name.c_str());
                continue;
            }
            g_native[file] = rt::blo::item_count(orig);  // the same in every language
            // Loose copy too (dataDir/screen/...), in case a screen is ever read outside the archive.
            if (int loose = rt::disc()->path_to_entry("dataDir/" + name); loose >= 0) rt::set_file_override(loose, *out);
            if (rt::arc::replace(arc, dir, name, *out)) {
                changed = true;
                ++patched;
            }
        }
        if (changed) {
            rt::set_file_override(arc_entry, std::move(arc));
            rt::set_file_override(dir_entry, std::move(dir));
        }
    }
    rt::log("menu", "added Settings and Quit Game to %d menu screens", patched);
}

}  // namespace enh

extern "C" {

void hle___ct__Q23zen8DrawMenuFPcbb(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::DrawMenu::DrawMenu");
    const uint32_t self = rt::arg(c, 0);
    const std::string name = basename(rt::rdstr(c, rt::arg(c, 1), 256));
    g_menus.erase(self);  // a new menu at a reused address
    if (f_801c184c_orig) f_801c184c_orig(c);
    if (auto it = g_native.find(name); it != g_native.end()) g_menus[self] = it->second;
}

void hle_update__Q23zen8DrawMenuFP10Controller(ppc_ctx* c) {
    HLE_ENTRY(c, "zen::DrawMenu::update");
    const uint32_t self = rt::arg(c, 0);
    const uint32_t before = rt::rd32(c, self + kState);
    if (f_801c2c24_orig) f_801c2c24_orig(c);
    auto it = g_menus.find(self);
    if (it == g_menus.end() || before != 2 || rt::rd32(c, self + kState) != 3 || rt::rd8(c, self + kCancelled)) return;
    const int index = static_cast<int>(rt::rd32(c, self + kSelected));
    if (index < it->second || !f_801c2408) return;
    // An added item: keep the menu open on it and hand over to the overlay.
    const uint32_t r3 = c->r[3], r4 = c->r[4], lr = c->lr;
    c->r[3] = self;
    c->r[4] = static_cast<uint32_t>(index);
    f_801c2408(c);
    c->r[3] = r3, c->r[4] = r4, c->lr = lr;
    ui::request(index == it->second ? ui::Request::Settings : ui::Request::Quit);
}

}  // extern "C"
