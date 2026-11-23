#include "rt/arc.hpp"

#include <algorithm>

namespace rt::arc {
namespace {
uint32_t rd32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) << 24 | static_cast<uint32_t>(b[at + 1]) << 16 |
           static_cast<uint32_t>(b[at + 2]) << 8 | b[at + 3];
}
void wr32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = static_cast<uint8_t>(v >> 24);
    b[at + 1] = static_cast<uint8_t>(v >> 16);
    b[at + 2] = static_cast<uint8_t>(v >> 8);
    b[at + 3] = static_cast<uint8_t>(v);
}
}  // namespace

std::optional<std::vector<Entry>> parse_dir(const std::vector<uint8_t>& dir) {
    if (dir.size() < 8) return std::nullopt;
    const uint32_t count = rd32(dir, 4);
    std::vector<Entry> out;
    size_t i = 8;
    for (uint32_t k = 0; k < count; ++k) {
        if (i + 12 > dir.size()) return std::nullopt;
        Entry e{rd32(dir, i), rd32(dir, i + 4), {}, i};
        const uint32_t n = rd32(dir, i + 8);
        if (n > dir.size() - i - 12) return std::nullopt;
        const auto* s = reinterpret_cast<const char*>(dir.data() + i + 12);
        size_t len = 0;
        while (len < n && s[len]) ++len;
        e.name.assign(s, len);
        out.push_back(std::move(e));
        i += 12 + n;
    }
    return out;
}

const Entry* find(const std::vector<Entry>& entries, const std::string& name) {
    for (const auto& e : entries)
        if (e.name == name) return &e;
    return nullptr;
}

bool replace(std::vector<uint8_t>& arc, std::vector<uint8_t>& dir, const std::string& name,
             const std::vector<uint8_t>& bytes) {
    auto entries = parse_dir(dir);
    if (!entries) return false;
    const Entry* e = find(*entries, name);
    if (!e) return false;
    arc.resize((arc.size() + 31) & ~size_t{31}, 0);
    const size_t at = arc.size();
    arc.insert(arc.end(), bytes.begin(), bytes.end());
    arc.resize((arc.size() + 31) & ~size_t{31}, 0);
    wr32(dir, e->dir_at, static_cast<uint32_t>(at));
    wr32(dir, e->dir_at + 4, static_cast<uint32_t>(arc.size() - at));
    return true;
}

bool add(std::vector<uint8_t>& arc, std::vector<uint8_t>& dir, const std::string& name, const std::vector<uint8_t>& bytes) {
    auto entries = parse_dir(dir);
    if (!entries || find(*entries, name)) return false;
    arc.resize((arc.size() + 31) & ~size_t{31}, 0);
    const size_t at = arc.size();
    arc.insert(arc.end(), bytes.begin(), bytes.end());
    arc.resize((arc.size() + 31) & ~size_t{31}, 0);
    const uint32_t name_len = static_cast<uint32_t>((name.size() + 1 + 3) & ~size_t{3});
    const size_t rec = dir.size();
    dir.resize(rec + 12 + name_len, 0);
    wr32(dir, rec, static_cast<uint32_t>(at));
    wr32(dir, rec + 4, static_cast<uint32_t>(arc.size() - at));
    wr32(dir, rec + 8, name_len);
    std::copy(name.begin(), name.end(), dir.begin() + static_cast<long>(rec + 12));
    wr32(dir, 0, static_cast<uint32_t>(dir.size()));
    wr32(dir, 4, rd32(dir, 4) + 1);
    return true;
}

}  // namespace rt::arc
