#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "check.hpp"
#include "rt/arc.hpp"
#include "rt/blo.hpp"
#include "enhance/settings_screens.hpp"

namespace {
const std::string kScreens = std::string(PIKMIN_SOURCE_DATA) + "/files/dataDir/screen/";

std::vector<uint8_t> load(const std::string& rel) {
    std::ifstream f(kScreens + rel, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

const rt::blo::Item* find(const std::vector<rt::blo::Item>& items, const std::string& name) {
    for (auto& i : items)
        if (i.name == name) return &i;
    return nullptr;
}
}  // namespace

int main() {
    auto sel = load("eng_blo/m_select.blo");
    if (sel.empty()) {
        std::puts("skip: game data not available");
        return 77;
    }

    // menu_items_of_originals
    auto items = rt::blo::menu_items(sel);
    CHECK(rt::blo::item_count(sel) == 2);
    CHECK(find(items, "hm00") && find(items, "hm00")->text == "Start");
    CHECK(find(items, "he01") && find(items, "hm01")->text.find("ons") != std::string::npos);
    CHECK(rt::blo::item_count(load("eng_blo/m_selec2.blo")) == 3);
    auto pause = load("eng_blo/pause.blo");
    CHECK(rt::blo::item_count(pause) == 3);
    CHECK(find(rt::blo::menu_items(pause), "hm00")->text == "Continue");

    // add_two_items
    auto patched = rt::blo::add_menu_items(sel, {"Settings", "Quit Game"});
    CHECK(patched.has_value());
    if (patched) {
        auto p = rt::blo::menu_items(*patched);
        CHECK(rt::blo::item_count(*patched) == 4);
        const auto *h0 = find(p, "hm00"), *h1 = find(p, "hm01"), *h2 = find(p, "hm02"), *h3 = find(p, "hm03");
        const auto *e1 = find(p, "he01"), *e2 = find(p, "he02"), *e3 = find(p, "he03");
        CHECK(h0 && h1 && h2 && h3 && e1 && e2 && e3);
        if (h0 && h1 && h2 && h3 && e1 && e2 && e3) {
            CHECK(h2->text == "Settings" && h3->text == "Quit Game" && e2->text == "Settings");
            CHECK(h2->depth == h1->depth && e2->depth == e1->depth);
            CHECK(h2->w == h1->w && h2->h == h1->h);
            // evenly spaced like the originals (their spacing may be tightened to fit)
            CHECK(h1->y - h0->y == h2->y - h1->y && h3->y - h2->y == h2->y - h1->y);
            CHECK(e2->y - e1->y == h2->y - h1->y && e2->x == e1->x);
            // original items keep their names and text
            CHECK(h0->text == "Start" && h1->text == find(items, "hm01")->text);
        }
    }

    // new items go below the lowest existing item (pause.blo: under "Continue from Last Save")
    auto pp = rt::blo::add_menu_items(pause, {"Settings", "Quit Game"});
    CHECK(pp.has_value());
    if (pp) {
        auto p = rt::blo::menu_items(*pp);
        const auto *e1 = find(p, "he01"), *e2 = find(p, "he02"), *e3 = find(p, "he03"), *e4 = find(p, "he04");
        const auto *h1 = find(p, "hm01"), *h3 = find(p, "hm03");
        CHECK(e1 && e2 && e3 && e4 && h1 && h3);
        if (e1 && e2 && e3 && e4 && h1 && h3) {
            CHECK(e3->y >= e2->y + e2->h);
            CHECK(e3->y - (e2->y + e2->h) == 4);    // 4+ items: spacing 36 - height 32
            CHECK(e4->y - e3->y == 36);
            CHECK(h3->y - h1->y == e3->y - e1->y);  // hm (relative) moves with he (absolute)
            CHECK(e4->y + e4->h <= 480);
        }
    }

    // frame_grows_and_icons_follow: the glass frame (p000..p005) grows to hold the new items, the
    // side orbs (iNNl/iNNr) exist for them, and everything stays on the 640x480 screen.
    {
        auto pics = [](const std::vector<uint8_t>& b) { return rt::blo::pictures(b); };
        auto before = pics(sel);
        auto after = pics(*patched);
        auto get = [](const std::vector<rt::blo::Item>& v, const std::string& n) -> const rt::blo::Item* {
            for (auto& i : v)
                if (i.name == n) return &i;
            return nullptr;
        };
        // The glass capsule is the two unnamed w08_160 pictures in the item group (yoko); they grow
        // to hold the new items (m_selec2.blo grows them for its third item). The light blobs pNNN
        // keep their size.
        const auto *b0 = get(before, "p000"), *a0 = get(after, "p000");
        CHECK(b0 && a0 && a0->h == b0->h && a0->y < b0->y && !get(after, "p006"));
        std::vector<const rt::blo::Item*> wb, wa;
        for (auto& pic : before)
            if (pic.name == std::string(4, '\0') && pic.depth == 2) wb.push_back(&pic);
        for (auto& pic : after)
            if (pic.name == std::string(4, '\0') && pic.depth == 2) wa.push_back(&pic);
        CHECK(wb.size() == 2 && wa.size() == 2);
        int frame_bottom = 0;
        if (wb.size() == 2 && wa.size() == 2) {
            CHECK(wa[0]->h > wb[0]->h && wa[1]->h - wb[1]->h == wa[0]->h - wb[0]->h);
            CHECK(wa[0]->y == wb[0]->y && wa[1]->y == wb[1]->y);  // relative to the group, which moves
        }
        // frame bottom in absolute coordinates: group (yoko) position + the outer picture
        {
            int yoko_y = 0;
            auto panes = rt::blo::panes(*patched);
            for (auto& pn : panes)
                if (pn.name == "yoko") yoko_y = pn.y;
            if (wa.size() == 2) frame_bottom = yoko_y + wa[0]->y + wa[0]->h;
        }
        CHECK(get(after, "i02l") && get(after, "i03r"));
        const auto *i1 = get(after, "i01l"), *i2 = get(after, "i02l");
        CHECK(i1 && i2 && i2->y - i1->y == 36);
        auto items = rt::blo::menu_items(*patched);
        const auto* h3 = find(items, "he03");
        CHECK(frame_bottom <= 426);         // checked by eye: clear of the copyright line (y ~ 425 drawn)
        CHECK(a0 && a0->y >= 200);          // clear of the PIKMIN logo
        // new last item still inside the grown frame
        CHECK(h3 && h3->y + h3->h <= frame_bottom);
    }

    // test_patch_all_languages
    for (const char* lang : {"eng", "fre", "ger", "ita", "spa"}) {
        for (const char* file : {"m_select.blo", "m_selec2.blo", "pause.blo"}) {
            auto orig = load(std::string(lang) + "_blo/" + file);
            CHECK(!orig.empty());
            const int n = rt::blo::item_count(orig);
            auto out = rt::blo::add_menu_items(orig, {"Settings", "Quit Game"});
            if (!out) std::fprintf(stderr, "cannot patch %s_blo/%s\n", lang, file);
            CHECK(out.has_value());
            if (out) CHECK(rt::blo::item_count(*out) == n + 2);
        }
    }

    // archive_replace: screens load from dataDir/archives/blo__<lang>.arc + .dir
    {
        const std::string arcs = std::string(PIKMIN_SOURCE_DATA) + "/files/dataDir/archives/";
        auto read = [&](const std::string& f) {
            std::ifstream in(arcs + f, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
        };
        auto arc = read("blo__eng.arc"), dir = read("blo__eng.dir");
        auto entries = rt::arc::parse_dir(dir);
        CHECK(entries.has_value() && entries->size() == 303);
        const auto* e = entries ? rt::arc::find(*entries, "screen/eng_blo/m_select.blo") : nullptr;
        CHECK(e != nullptr);
        if (e) {
            std::vector<uint8_t> orig(arc.begin() + e->offset, arc.begin() + e->offset + e->size);
            CHECK(orig == sel);
            const size_t old_arc = arc.size(), old_dir = dir.size();
            auto patched_sel = *rt::blo::add_menu_items(sel, {"Settings", "Quit Game"});
            CHECK(rt::arc::replace(arc, dir, "screen/eng_blo/m_select.blo", patched_sel));
            auto after = rt::arc::parse_dir(dir);
            const auto* n = after ? rt::arc::find(*after, "screen/eng_blo/m_select.blo") : nullptr;
            CHECK(n && n->offset == old_arc && n->size % 32 == 0 && n->size >= patched_sel.size());
            CHECK(dir.size() == old_dir && arc.size() == old_arc + (n ? n->size : 0));
            if (n) CHECK(std::equal(patched_sel.begin(), patched_sel.end(), arc.begin() + n->offset));
            if (n) CHECK(rt::blo::item_count(std::vector<uint8_t>(arc.begin() + n->offset, arc.begin() + n->offset + n->size)) == 4);
            const auto* p0 = rt::arc::find(*after, "screen/eng_blo/pause.blo");
            const auto* q0 = rt::arc::find(*entries, "screen/eng_blo/pause.blo");
            CHECK(p0 && q0 && p0->offset == q0->offset && p0->size == q0->size);
        }
        CHECK(!rt::arc::replace(arc, dir, "screen/eng_blo/nope.blo", sel));
        std::vector<uint8_t> bad(dir.begin(), dir.begin() + 30);
        CHECK(!rt::arc::parse_dir(bad).has_value());
    }

    // settings_screens_all_languages
    namespace sc = enh::screens;
    for (const char* lang : {"eng", "fre", "ger", "ita", "spa"}) {
        const std::string l = std::string(lang) + "_blo/";
        // Display / Game are built from the English Sound screen in every language (English labels).
        auto disp = sc::make_rows_screen(load("eng_blo/s_select.blo"), "Display",
                                         {{"Screen", {"Full", "Window"}}, {"Aspect", {"Wide", "4:3"}}, {"V-sync", {"On", "Off"}}});
        auto game = sc::make_rows_screen(load("eng_blo/s_select.blo"), "Game",
                                         {{"FPS", {"Original", "60", "Max"}}, {"Camera", {"Modern", "Original"}}, {"HUD size", {}}});
        CHECK(disp && game);
        if (disp) {
            auto all = rt::blo::all(*disp);
            CHECK(find(all, "he00") && find(all, "he00")->text == "Screen" && find(all, "he02")->text == "V-sync");
            CHECK(find(all, "o21t") && find(all, "o21t")->text == "Off" && find(all, "o21n") && find(all, "o21n")->visible);
            CHECK(find(all, "titl")->text == "Display" && !find(all, "on11")->visible);
        }
        if (game) {
            auto all = rt::blo::all(*game);
            CHECK(find(all, "o02t") && find(all, "o02t")->text == "Max" && !find(all, "o20n"));
            CHECK(find(all, "on11")->visible && find(all, "on20")->visible && !find(all, "on01")->visible);
        }
        auto opts = sc::make_pause_options(load(l + "option.blo"));
        CHECK(opts && rt::blo::item_count(*opts) == 4 && find(rt::blo::all(*opts), "he03")->text == "Game");
        auto quit = sc::make_quit_screen(load(l + "pause_ok.blo"));
        CHECK(quit && rt::blo::item_count(*quit) == 2 && find(rt::blo::all(*quit), "titl")->text == "Quit Pikmin?");
    }
    CHECK(!sc::make_rows_screen(sel, "x", {{"a", {"b"}}, {"c", {"d"}}, {"e", {}}}).has_value());  // not a Sound screen

    // garbage_is_rejected
    std::mt19937 rng(1);
    std::vector<uint8_t> junk(2000);
    for (auto& b : junk) b = static_cast<uint8_t>(rng());
    CHECK(!rt::blo::add_menu_items(junk, {"Settings"}).has_value());
    std::vector<uint8_t> cut(sel.begin(), sel.begin() + 470);  // ends inside hm00
    CHECK(!rt::blo::add_menu_items(cut, {"Settings"}).has_value());
    CHECK(rt::blo::menu_items(cut).empty());
    CHECK(!rt::blo::add_menu_items({}, {"Settings"}).has_value());
    return check_result();
}
