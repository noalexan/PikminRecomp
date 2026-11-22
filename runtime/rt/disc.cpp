#include "rt/disc.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace rt {
namespace {
constexpr uint32_t kMagic = 0xC2339F3D;

uint32_t be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

std::string normalise(std::string_view path) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find('/', i);
        if (j == std::string_view::npos) j = path.size();
        std::string part(path.substr(i, j - i));
        std::transform(part.begin(), part.end(), part.begin(), [](unsigned char ch) { return std::tolower(ch); });
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        i = j + 1;
    }
    std::string out;
    for (auto& p : parts) out += "/" + p;
    return out.empty() ? "/" : out;
}

std::unique_ptr<Disc>& current() {
    static std::unique_ptr<Disc> d;
    return d;
}
}  // namespace

void set_disc(std::unique_ptr<Disc> d) { current() = std::move(d); }
Disc* disc() { return current().get(); }

Disc::Disc(Disc&& o) noexcept { *this = std::move(o); }
Disc& Disc::operator=(Disc&& o) noexcept {
    std::swap(fd_, o.fd_);
    header_ = std::move(o.header_);
    entries_ = std::move(o.entries_);
    paths_ = std::move(o.paths_);
    return *this;
}
Disc::~Disc() {
    if (fd_ >= 0) close(fd_);
}

size_t Disc::read(uint64_t offset, std::span<uint8_t> out) const {
    size_t done = 0;
    while (done < out.size()) {
        ssize_t n = pread(fd_, out.data() + done, out.size() - done, static_cast<off_t>(offset + done));
        if (n <= 0) break;
        done += static_cast<size_t>(n);
    }
    return done;
}

Disc Disc::open(const std::string& path) {
    Disc d;
    d.fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (d.fd_ < 0) throw DiscError(path + ": cannot open disc image: " + std::strerror(errno));
    d.header_.resize(0x440);
    if (d.read(0, d.header_) != d.header_.size()) throw DiscError(path + ": too small to be a GameCube disc image");
    if (be32(&d.header_[0x1C]) != kMagic) throw DiscError(path + ": not a GameCube disc image (bad magic)");
    std::string id(reinterpret_cast<const char*>(d.header_.data()), 6);
    if (id != kGameId) {
        throw DiscError(path + ": disc is " + id + ", expected " + kGameId + " (Pikmin, PAL)");
    }
    uint32_t fst_off = be32(&d.header_[0x424]), fst_size = be32(&d.header_[0x428]);
    std::vector<uint8_t> fst(fst_size);
    if (fst_size < 12 || d.read(fst_off, fst) != fst_size) throw DiscError(path + ": cannot read the file table");
    uint32_t count = be32(&fst[8]);
    if (count == 0 || static_cast<uint64_t>(count) * 12 > fst_size) throw DiscError(path + ": corrupt file table");
    const uint32_t names = count * 12;

    d.entries_.resize(count);
    d.entries_[0] = {0, count, true};
    d.paths_["/"] = 0;
    // Walk the flat table, tracking (end index, path) of enclosing directories.
    std::vector<std::pair<uint32_t, std::string>> stack{{count, ""}};
    for (uint32_t i = 1; i < count; ++i) {
        while (stack.size() > 1 && i >= stack.back().first) stack.pop_back();
        const uint8_t* e = &fst[i * 12];
        uint32_t w0 = be32(e), w1 = be32(e + 4), w2 = be32(e + 8);
        uint32_t name_off = names + (w0 & 0xFFFFFF);
        std::string name;
        for (uint32_t k = name_off; k < fst_size && fst[k]; ++k) name.push_back(static_cast<char>(std::tolower(fst[k])));
        std::string full = stack.back().second + "/" + name;
        bool is_dir = (w0 >> 24) != 0;
        d.entries_[i] = {is_dir ? 0 : w1, w2, is_dir};  // dirs: length field holds the next-entry index
        d.paths_[full] = static_cast<int>(i);
        if (is_dir) stack.push_back({w2, full});
    }
    return d;
}

int Disc::path_to_entry(std::string_view path) const {
    auto it = paths_.find(normalise(path));
    return it == paths_.end() ? -1 : it->second;
}

}  // namespace rt
