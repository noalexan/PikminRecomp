// Settings screens built from the game's own screen files. Layout notes (s_select.blo, positions in
// the `yoko` group): row texts at y -71 / -29 / 11; row 0 is "Stereo  Mono" with dots on21/on22,
// rows 1-2 are a label at x -198 and a 10-dot bar (on01..on10, on11..on20). Every dot is a 20x20
// picture coloured at run time (zen::setTextColor from the on_c/offc text boxes) with an unnamed
// dot_24 ring drawn over it. heNN are DrawMenu's highlighted copies of the row texts.
#include "enhance/settings_screens.hpp"

#include <algorithm>
#include <cstdlib>

#include "rt/blo.hpp"

namespace enh::screens {
namespace {
using rt::blo::Item;
const std::string kUnnamed(4, '\0');

long find(const std::vector<Item>& v, const std::string& name) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].name == name) return static_cast<long>(i);
    return -1;
}

// An unnamed text box with this text (the shadow/label copies the game leaves unnamed).
long find_unnamed_text(const std::vector<Item>& v, const std::string& text, size_t from = 0) {
    for (size_t i = from; i < v.size(); ++i)
        if (v[i].tag == 0x13 && v[i].name == kUnnamed && v[i].text == text) return static_cast<long>(i);
    return -1;
}

// The dot_24 ring drawn over a dot picture.
long find_ring(const std::vector<Item>& v, const Item& dot) {
    for (size_t i = 0; i < v.size(); ++i) {
        const Item& p = v[i];
        if (p.tag == 0x12 && p.name == kUnnamed && p.texture == "dot_24.bti" && p.depth == dot.depth &&
            std::abs(p.x - dot.x) <= 4 && std::abs(p.y - dot.y) <= 4)
            return static_cast<long>(i);
    }
    return -1;
}

std::string dot_name(int on) {  // 1 -> "on01"
    return std::string("on") + static_cast<char>('0' + on / 10) + static_cast<char>('0' + on % 10);
}

int text_width(const std::string& s) { return 17 * static_cast<int>(s.size()); }  // sumiw9_2 at 32 px, roughly
}  // namespace

std::optional<std::vector<uint8_t>> make_rows_screen(const std::vector<uint8_t>& sound, const std::string& title,
                                                     const std::vector<Row>& rows) {
    if (rows.size() != 3 || rows[0].choices.empty() || rows[1].choices.empty()) return std::nullopt;
    std::vector<uint8_t> b = sound;
    auto v = rt::blo::all(b);
    const long titl = find(v, "titl"), he[3] = {find(v, "he00"), find(v, "he01"), find(v, "he02")};
    if (titl < 0 || he[0] < 0 || he[1] < 0 || he[2] < 0) return std::nullopt;
    const long row0 = find_unnamed_text(v, v[he[0]].text), lab1 = find_unnamed_text(v, v[he[1]].text),
               lab2 = find_unnamed_text(v, v[he[2]].text), tpl = find(v, "on21");
    if (row0 < 0 || lab1 < 0 || lab2 < 0 || tpl < 0 || find_ring(v, v[tpl]) < 0) return std::nullopt;
    const int row_y[3] = {v[row0].y, v[lab1].y, v[lab2].y};
    const int label_x = v[lab1].x, he_x = v[he[1]].x;

    // Hide the Sound rows: "Stereo  Mono", both dots, and the bars (row 2's stays for a bar row).
    const bool bar2 = rows[2].choices.empty();
    rt::blo::set_visible(b, static_cast<size_t>(row0), false);
    for (int on = 1; on <= 22; ++on) {
        if (bar2 && on >= 11 && on <= 20) continue;
        v = rt::blo::all(b);
        const long d = find(v, dot_name(on));
        if (d < 0) return std::nullopt;
        rt::blo::set_visible(b, static_cast<size_t>(d), false);
        if (long r = find_ring(v, v[d]); r >= 0) rt::blo::set_visible(b, static_cast<size_t>(r), false);
    }

    // Texts: title (and its unnamed shadow), row labels and their highlighted copies.
    v = rt::blo::all(b);
    const std::string old_title = v[titl].text;
    for (long i = 0; (i = find_unnamed_text(v, old_title, static_cast<size_t>(i))) >= 0; ++i)
        rt::blo::set_text(b, static_cast<size_t>(i), title), v = rt::blo::all(b);
    rt::blo::set_text(b, static_cast<size_t>(find(v, "titl")), title);
    for (int r = 0; r < 3; ++r) {
        v = rt::blo::all(b);
        rt::blo::set_text(b, static_cast<size_t>(find(v, "he0" + std::to_string(r))), rows[r].label);
    }
    v = rt::blo::all(b);
    rt::blo::set_pos(b, static_cast<size_t>(find(v, "he00")), he_x, v[find(v, "he00")].y);
    const long l1 = find_unnamed_text(v, v[lab1].text), l2 = find_unnamed_text(v, v[lab2].text);
    rt::blo::set_text(b, static_cast<size_t>(l2), rows[2].label);  // the later one first: indices stay valid
    rt::blo::set_text(b, static_cast<size_t>(l1), rows[1].label);
    // Row 0 gets a label like the others.
    if (!rt::blo::insert_clone(b, static_cast<size_t>(l1), "o0lb", label_x, row_y[0])) return std::nullopt;
    v = rt::blo::all(b);
    rt::blo::set_text(b, static_cast<size_t>(find(v, "o0lb")), rows[0].label);

    // Choices: dot (from on21) + ring + text (from the hidden row-0 text), left to right after the row label.
    for (int r = 0; r < 3; ++r) {
        int x = std::max(-120, label_x + 22 * static_cast<int>(rows[r].label.size()) + 16);  // labels are wider
        for (size_t k = 0; k < rows[r].choices.size(); ++k) {
            const std::string id = "o" + std::to_string(r) + std::to_string(k);
            const int dot_y = row_y[r] + 2;
            v = rt::blo::all(b);
            const long dot = find(v, "on21"), ring = find_ring(v, v[find(v, "on21")]);
            // Insert the later records first so the earlier index stays valid.
            const long row0_now = [&] {
                for (size_t i = 0; i < v.size(); ++i)
                    if (v[i].tag == 0x13 && v[i].name == kUnnamed && !v[i].visible && v[i].y == row_y[0]) return static_cast<long>(i);
                return -1L;
            }();
            if (dot < 0 || ring < 0 || row0_now < 0) return std::nullopt;
            const long dot_dx = v[ring].x - v[dot].x, dot_dy = v[ring].y - v[dot].y;
            std::vector<std::pair<long, std::string>> order = {{row0_now, id + "t"}, {ring, id + "b"}, {dot, id + "n"}};
            std::sort(order.begin(), order.end(), [](auto& a, auto& c) { return a.first > c.first; });
            for (auto& [at, name] : order) {
                int nx = x, ny = dot_y;
                if (name.back() == 'b') nx = x + static_cast<int>(dot_dx), ny = dot_y + static_cast<int>(dot_dy);
                if (name.back() == 't') nx = x + 30, ny = row_y[r];
                if (!rt::blo::insert_clone(b, static_cast<size_t>(at), name, nx, ny)) return std::nullopt;
            }
            v = rt::blo::all(b);
            for (const char* suffix : {"t", "b", "n"}) rt::blo::set_visible(b, static_cast<size_t>(find(v, id + suffix)), true);
            rt::blo::set_text(b, static_cast<size_t>(find(v, id + "t")), rows[r].choices[k]);
            x += 30 + text_width(rows[r].choices[k]) + 22;
        }
    }
    if (rt::blo::item_count(b) != 3) return std::nullopt;
    return b;
}

std::optional<std::vector<uint8_t>> make_pause_options(const std::vector<uint8_t>& option) {
    std::vector<uint8_t> b = option;
    auto v = rt::blo::all(b);
    const long hm2 = find(v, "hm02");
    if (hm2 < 0 || rt::blo::item_count(b) != 4) return std::nullopt;
    const std::string rumble = v[hm2].text;  // the game's own (translated) label
    const std::pair<const char*, std::string> texts[] = {
        {"hm01", rumble}, {"he01", rumble}, {"hm02", "Display"}, {"he02", "Display"}, {"hm03", "Game"}, {"he03", "Game"}};
    for (auto& [name, text] : texts) {
        v = rt::blo::all(b);
        const long i = find(v, name);
        if (i < 0 || !rt::blo::set_text(b, static_cast<size_t>(i), text)) return std::nullopt;
        rt::blo::set_visible(b, static_cast<size_t>(i), true);
    }
    return b;
}

std::optional<std::vector<uint8_t>> make_quit_screen(const std::vector<uint8_t>& pause_ok) {
    std::vector<uint8_t> b = pause_ok;
    auto v = rt::blo::all(b);
    const long titl = find(v, "titl"), yame = find(v, "yame"), even = find(v, "even");
    if (titl < 0 || yame < 0 || even < 0 || rt::blo::item_count(b) != 2) return std::nullopt;
    rt::blo::set_visible(b, static_cast<size_t>(yame), false);
    rt::blo::set_visible(b, static_cast<size_t>(even), false);
    // The title and its shadow (the unnamed text box right under it).
    const size_t shadow = static_cast<size_t>(titl) + 1;
    if (shadow < v.size() && v[shadow].tag == 0x13 && v[shadow].name == kUnnamed) rt::blo::set_text(b, shadow, "Quit Pikmin?");
    rt::blo::set_text(b, static_cast<size_t>(titl), "Quit Pikmin?");
    return b;
}

}  // namespace enh::screens
