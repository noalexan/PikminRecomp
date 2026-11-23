// P2D screen file reader/patcher. The stream is a sequence of big-endian u16 tags
// (P2DScreen::makeHiearachyPanes): 0 = end, 1 = children follow (+u16), 2 = end of children (+u16),
// 0x10 pane, 0x11 window, 0x12 picture, 0x13 text box. Every pane record starts with
// u8 visible, u8 pad, char name[4], s16 x, y, w, h; pictures and text boxes then carry their own
// fields and end aligned to 4 bytes (absolute stream offset). See docs/superpowers/notes/blo-format.md.
#include "rt/blo.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace rt::blo {
namespace {

struct Record {
    uint16_t tag;
    int depth;
    size_t begin, end;  // [begin, end) including alignment padding
    std::string name;
    int16_t x, y, w, h;
    size_t text_at = 0;  // text boxes: offset of the s16 text length
    std::string text;
};

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& b) : b_(b) {}
    bool ok() const { return ok_; }
    size_t pos() const { return i_; }
    uint8_t u8() {
        if (i_ + 1 > b_.size()) return fail();
        return b_[i_++];
    }
    uint16_t u16() {
        if (i_ + 2 > b_.size()) return fail();
        uint16_t v = static_cast<uint16_t>(b_[i_] << 8 | b_[i_ + 1]);
        i_ += 2;
        return v;
    }
    int16_t s16() { return static_cast<int16_t>(u16()); }
    std::string raw(size_t n) {
        if (i_ + n > b_.size()) {
            fail();
            return {};
        }
        std::string s(reinterpret_cast<const char*>(b_.data() + i_), n);
        i_ += n;
        return s;
    }
    void skip(size_t n) { raw(n); }
    void resource() {  // P2DStream::getResource: u8 kind, u8 length, name (kinds 0 and 2)
        const uint8_t kind = u8(), n = u8();
        if (n && (kind == 0 || kind == 2)) skip(n);
    }
    void align() {
        const size_t a = (i_ + 3) & ~size_t{3};
        if (a > b_.size()) fail();
        i_ = a;
    }

private:
    uint8_t fail() {
        ok_ = false;
        i_ = b_.size();
        return 0;
    }
    const std::vector<uint8_t>& b_;
    size_t i_ = 0;
    bool ok_ = true;
};

// Mirrors makeHiearachyPanes; false on anything it wouldn't accept (or a window, unused here).
bool parse_level(Reader& r, int depth, std::vector<Record>& out) {
    for (;;) {
        const size_t begin = r.pos();
        const uint16_t tag = r.u16();
        if (!r.ok()) return false;
        switch (tag) {
            case 0: return depth == 0;
            case 1:
                r.u16();
                if (!r.ok() || !parse_level(r, depth + 1, out)) return false;
                break;
            case 2: r.u16(); return r.ok() && depth > 0;
            case 0x10:
            case 0x12:
            case 0x13: {
                Record rec{tag, depth, begin, 0, {}, 0, 0, 0, 0, 0, {}};
                r.u8();
                r.u8();
                rec.name = r.raw(4);
                rec.x = r.s16();
                rec.y = r.s16();
                rec.w = r.s16();
                rec.h = r.s16();
                if (tag == 0x12) {
                    r.resource();  // texture
                    r.resource();  // palette
                    r.u8();
                    r.u8();
                    r.align();
                } else if (tag == 0x13) {
                    r.resource();  // font
                    r.skip(8);     // two colours
                    const uint8_t flags = r.u8();
                    r.u8();
                    if (flags & 0x80) r.skip(8);
                    rec.text_at = r.pos();
                    const int16_t n = r.s16();
                    if (n < 0) return false;
                    rec.text = r.raw(static_cast<size_t>(n));
                    r.align();
                }
                if (!r.ok()) return false;
                rec.end = r.pos();
                out.push_back(std::move(rec));
                break;
            }
            default: return false;  // 0x11 windows and unknown tags
        }
    }
}

std::optional<std::vector<Record>> parse(const std::vector<uint8_t>& b) {
    if (b.empty()) return std::nullopt;
    Reader r(b);
    std::vector<Record> out;
    if (!parse_level(r, 0, out)) return std::nullopt;
    return out;
}

// "hm07" -> 7 for prefix "hm"; -1 otherwise.
int item_index(const Record& r, const char* prefix) {
    if (r.tag != 0x13 || r.name.size() != 4 || r.name.compare(0, 2, prefix) != 0) return -1;
    if (r.name[2] < '0' || r.name[2] > '9' || r.name[3] < '0' || r.name[3] > '9') return -1;
    return (r.name[2] - '0') * 10 + (r.name[3] - '0');
}

const Record* find(const std::vector<Record>& recs, const char* prefix, int index) {
    for (const auto& r : recs)
        if (item_index(r, prefix) == index) return &r;
    return nullptr;
}

int count(const std::vector<Record>& recs) {
    int n = 0;
    while (find(recs, "he", n)) ++n;
    return n;
}

void put16(std::vector<uint8_t>& b, size_t at, int v) {
    b[at] = static_cast<uint8_t>(v >> 8);
    b[at + 1] = static_cast<uint8_t>(v);
}

// The record's bytes renamed, moved down by dy and with new text; the result is 4-aligned in length
// (records start 4-aligned, so the bytes after an inserted clone keep their alignment).
std::vector<uint8_t> clone(const std::vector<uint8_t>& b, const Record& r, const std::string& name, int y,
                           const std::string& text) {
    std::vector<uint8_t> out(b.begin() + static_cast<long>(r.begin), b.begin() + static_cast<long>(r.text_at));
    std::memcpy(out.data() + 4, name.data(), 4);
    put16(out, 10, y);
    out.push_back(static_cast<uint8_t>(text.size() >> 8));
    out.push_back(static_cast<uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    while (out.size() % 4) out.push_back(0);
    return out;
}

// A piece of a menu's glass frame: top-level picture p000, p001, ...
bool is_frame(const Record& r) {
    return r.tag == 0x12 && r.depth == 1 && r.name.size() == 4 && r.name[0] == 'p' &&
           std::isdigit(static_cast<unsigned char>(r.name[1])) && std::isdigit(static_cast<unsigned char>(r.name[2])) &&
           std::isdigit(static_cast<unsigned char>(r.name[3]));
}

std::string item_name(const char* prefix, int index) {
    char s[5];
    s[0] = prefix[0];
    s[1] = prefix[1];
    s[2] = static_cast<char>('0' + index / 10);
    s[3] = static_cast<char>('0' + index % 10);
    s[4] = '\0';
    return s;
}

}  // namespace

std::vector<Item> menu_items(const std::vector<uint8_t>& blo) {
    std::vector<Item> out;
    auto recs = parse(blo);
    if (!recs) return out;
    for (const auto& r : *recs) {
        if (item_index(r, "hm") < 0 && item_index(r, "he") < 0) continue;
        out.push_back({r.name, r.text, r.depth, r.x, r.y, r.w, r.h});
    }
    return out;
}

int item_count(const std::vector<uint8_t>& blo) {
    auto recs = parse(blo);
    return recs ? count(*recs) : 0;
}

std::vector<Item> panes(const std::vector<uint8_t>& blo) {
    std::vector<Item> out;
    auto recs = parse(blo);
    if (!recs) return out;
    for (const auto& r : *recs)
        if (r.tag == 0x10) out.push_back({r.name, {}, r.depth, r.x, r.y, r.w, r.h});
    return out;
}

std::vector<Item> pictures(const std::vector<uint8_t>& blo) {
    std::vector<Item> out;
    auto recs = parse(blo);
    if (!recs) return out;
    for (const auto& r : *recs)
        if (r.tag == 0x12) out.push_back({r.name, {}, r.depth, r.x, r.y, r.w, r.h});
    return out;
}

std::optional<std::vector<uint8_t>> add_menu_items(const std::vector<uint8_t>& blo,
                                                   const std::vector<std::string>& labels) {
    auto parsed = parse(blo);
    if (!parsed) return std::nullopt;
    const std::vector<Record>& recs = *parsed;
    const int n = count(recs);
    const int added = static_cast<int>(labels.size());
    const Record* hm0 = find(recs, "hm", 0);
    if (!hm0 || n + added > 99) return std::nullopt;
    // The hm00 group: hmNN siblings of hm00 (pause.blo keeps a third item elsewhere).
    int last = 0;
    for (int i = 1; i < n; ++i) {
        const Record* r = find(recs, "hm", i);
        if (r && r->depth == hm0->depth && r->begin > hm0->begin) last = i;
    }
    const Record* hm1 = find(recs, "hm", 1);
    if (last < 1 || !hm1) return std::nullopt;
    for (const auto& r : recs)
        if (r.begin % 4) return std::nullopt;

    // Spacing: menus of 4+ items use at most 36 (m_selec2.blo tightens to 39 for 3 items; 36 keeps a
    // 4-5 item title menu clear of the copyright line).
    const int s0 = hm1->y - hm0->y;
    const int spacing = (last + 1 + added >= 4) ? std::min(s0, 36) : s0;

    std::vector<uint8_t> out = blo;
    auto set_y = [&](const Record& r, int y) { put16(out, r.begin + 10, y); };
    auto by_name = [&](const std::string& name) -> const Record* {
        for (const auto& r : recs)
            if (r.name == name) return &r;
        return nullptr;
    };
    auto digits = [](int i) { return item_name("xx", i).substr(2); };
    auto icon = [&](int i, char side) { return by_name("i" + digits(i) + side); };

    // 1. Re-space the group (heNN absolute, hmNN and iNN[lr] relative: all move by the same amount).
    std::vector<int> dy(static_cast<size_t>(n), 0);
    for (int i = 0; i <= last; ++i) dy[static_cast<size_t>(i)] = hm0->y + i * spacing - find(recs, "hm", i)->y;
    int old_bottom = 0, bottom = 0;
    for (int i = 0; i < n; ++i) {
        const Record* e = find(recs, "he", i);
        if (!e) continue;
        old_bottom = std::max(old_bottom, e->y + e->h);
        bottom = std::max(bottom, e->y + dy[static_cast<size_t>(i)] + e->h);
    }

    // 2. New items below the lowest one, cloned from the group's last item.
    const Record* hm_last = find(recs, "hm", last);
    const Record* he_last = find(recs, "he", last);
    const Record* il = icon(last, 'l');
    const Record* ir = icon(last, 'r');
    if (!hm_last || !he_last) return std::nullopt;
    const int first_y = bottom + (spacing - he_last->h);  // absolute (he) position of the first new item
    const int new_bottom = first_y + (added - 1) * spacing + he_last->h;
    const int extra = std::max(0, new_bottom - old_bottom);
    // The frame grows by `extra`; it and the items move up by at most 55 (the room under the title logo).
    const int up = std::min(extra, 55);

    for (int i = 0; i <= last; ++i) {
        const int d = dy[static_cast<size_t>(i)];
        if (!d) continue;
        set_y(*find(recs, "hm", i), find(recs, "hm", i)->y + d);
        if (const Record* e = find(recs, "he", i)) set_y(*e, e->y + d);
        for (char side : {'l', 'r'})
            if (const Record* c = icon(i, side)) set_y(*c, c->y + d);
    }
    // 3. The glass capsule: the unnamed pictures in the item group, before hm00 (m_selec2.blo grows
    //    them for its third item). They move with the group and grow downwards by `extra`.
    const Record* yoko = by_name("yoko");
    for (const auto& r : recs) {
        if (yoko && r.tag == 0x12 && r.depth == hm0->depth && r.begin > yoko->begin && r.begin < hm0->begin &&
            r.name == std::string(4, '\0')) {
            put16(out, r.begin + 14, r.h + extra);
        }
    }
    // 4. Shift everything that is positioned absolutely, and the groups holding the relative ones.
    for (const auto& r : recs) {
        const bool he = item_index(r, "he") >= 0;
        const bool lone_hm = item_index(r, "hm") >= 0 && r.depth != hm0->depth;
        const bool group = r.name == "yoko" || r.name == "iall";
        const bool frame = is_frame(r);  // the light blobs behind the menu
        if (!he && !lone_hm && !group && !frame) continue;
        const int cur = static_cast<int16_t>(out[r.begin + 10] << 8 | out[r.begin + 11]);
        set_y(r, cur - up);
    }

    // 5. Clones, inserted from the back so earlier offsets stay valid.
    std::vector<std::pair<size_t, std::vector<uint8_t>>> inserts;
    auto add = [&](size_t at, std::vector<uint8_t> bytes) {
        for (auto& [pos, b] : inserts)
            if (pos == at) {
                b.insert(b.end(), bytes.begin(), bytes.end());
                return;
            }
        inserts.emplace_back(at, std::move(bytes));
    };
    for (int j = 0; j < added; ++j) {
        const int index = n + j;
        const int he_y = first_y + j * spacing;            // before the shift
        const int d = he_y - he_last->y;                   // relative to the original last item
        add(hm_last->end, clone(out, *hm_last, item_name("hm", index), hm_last->y + d, labels[static_cast<size_t>(j)]));
        add(he_last->end, clone(out, *he_last, item_name("he", index), he_last->y + d - up, labels[static_cast<size_t>(j)]));
        for (const Record* c : {il, ir}) {
            if (!c) continue;
            std::vector<uint8_t> b(out.begin() + static_cast<long>(c->begin), out.begin() + static_cast<long>(c->end));
            const std::string nm = "i" + digits(index) + c->name[3];
            std::memcpy(b.data() + 4, nm.data(), 4);
            put16(b, 10, c->y + d);
            add(il ? il->end : c->end, std::move(b));
        }
    }
    std::sort(inserts.begin(), inserts.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& [pos, b] : inserts) out.insert(out.begin() + static_cast<long>(pos), b.begin(), b.end());
    if (item_count(out) != n + added) return std::nullopt;
    return out;
}

}  // namespace rt::blo
