// P2D screen file reader/patcher. The stream is a sequence of big-endian u16 tags
// (P2DScreen::makeHiearachyPanes): 0 = end, 1 = children follow (+u16), 2 = end of children (+u16),
// 0x10 pane, 0x11 window, 0x12 picture, 0x13 text box. Every pane record starts with
// u8 visible, u8 pad, char name[4], s16 x, y, w, h; pictures and text boxes then carry their own
// fields and end aligned to 4 bytes (absolute stream offset). See docs/superpowers/notes/blo-format.md.
#include "rt/blo.hpp"

#include <algorithm>
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
std::vector<uint8_t> clone(const std::vector<uint8_t>& b, const Record& r, const std::string& name, int dy,
                           const std::string& text) {
    std::vector<uint8_t> out(b.begin() + static_cast<long>(r.begin), b.begin() + static_cast<long>(r.text_at));
    std::memcpy(out.data() + 4, name.data(), 4);
    put16(out, 10, r.y + dy);
    out.push_back(static_cast<uint8_t>(text.size() >> 8));
    out.push_back(static_cast<uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    while (out.size() % 4) out.push_back(0);
    return out;
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

std::optional<std::vector<uint8_t>> add_menu_items(const std::vector<uint8_t>& blo,
                                                   const std::vector<std::string>& labels) {
    auto recs = parse(blo);
    if (!recs) return std::nullopt;
    const int n = count(*recs);
    const Record* hm0 = find(*recs, "hm", 0);
    if (!hm0 || n + static_cast<int>(labels.size()) > 99) return std::nullopt;
    // Last item of hm00's group (pause.blo keeps a third item elsewhere).
    int last = 0;
    for (int i = 1; i < n; ++i) {
        const Record* r = find(*recs, "hm", i);
        if (r && r->depth == hm0->depth && r->begin > hm0->begin) last = i;
    }
    const Record* hm_last = find(*recs, "hm", last);
    const Record* hm_prev = find(*recs, "hm", last - 1);
    const Record* he_last = find(*recs, "he", last);
    if (last < 1 || !hm_last || !hm_prev || !he_last || hm_last->begin % 4 || he_last->begin % 4) return std::nullopt;
    const int spacing = hm_last->y - hm_prev->y;
    // The first new item goes below the lowest existing one (heNN are absolute), with the menu's
    // own gap; hmNN (relative to their group) move by the same amount.
    int bottom = he_last->y + he_last->h;
    for (int i = 0; i < n; ++i)
        if (const Record* e = find(*recs, "he", i)) bottom = std::max(bottom, e->y + e->h);
    const int first_dy = bottom + (spacing - he_last->h) - he_last->y;

    std::vector<uint8_t> hm_bytes, he_bytes;
    for (size_t j = 0; j < labels.size(); ++j) {
        const int index = n + static_cast<int>(j);
        const int dy = first_dy + spacing * static_cast<int>(j);
        auto a = clone(blo, *hm_last, item_name("hm", index), dy, labels[j]);
        auto e = clone(blo, *he_last, item_name("he", index), dy, labels[j]);
        hm_bytes.insert(hm_bytes.end(), a.begin(), a.end());
        he_bytes.insert(he_bytes.end(), e.begin(), e.end());
    }
    std::vector<uint8_t> out = blo;
    // Insert the later position first so the earlier offset stays valid.
    if (hm_last->end > he_last->end) {
        out.insert(out.begin() + static_cast<long>(hm_last->end), hm_bytes.begin(), hm_bytes.end());
        out.insert(out.begin() + static_cast<long>(he_last->end), he_bytes.begin(), he_bytes.end());
    } else {
        out.insert(out.begin() + static_cast<long>(he_last->end), he_bytes.begin(), he_bytes.end());
        out.insert(out.begin() + static_cast<long>(hm_last->end), hm_bytes.begin(), hm_bytes.end());
    }
    if (item_count(out) != n + static_cast<int>(labels.size())) return std::nullopt;
    return out;
}

}  // namespace rt::blo
