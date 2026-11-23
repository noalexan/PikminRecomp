#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "check.hpp"
#include "rt/blo.hpp"

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
            CHECK(e3->y - (e2->y + e2->h) == 10);   // the menu's own gap: spacing 42 - height 32
            CHECK(e4->y - e3->y == 42);
            CHECK(h3->y - h1->y == e3->y - e1->y);  // hm (relative) moves with he (absolute)
            CHECK(e4->y + e4->h <= 480);
        }
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
