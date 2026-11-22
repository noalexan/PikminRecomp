#include "rt/disc.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

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
    spans_ = std::move(o.spans_);
    return *this;
}
Disc::~Disc() {
    if (fd_ >= 0) close(fd_);
}

size_t Disc::read(uint64_t offset, std::span<uint8_t> out) const {
    if (fd_ >= 0) {
        size_t done = 0;
        while (done < out.size()) {
            ssize_t n = pread(fd_, out.data() + done, out.size() - done, static_cast<off_t>(offset + done));
            if (n <= 0) break;
            done += static_cast<size_t>(n);
        }
        return done;
    }
    // Directory backend: the header region comes from boot.bin, file ranges from their files,
    // anything else (padding, system areas) reads as zeros.
    size_t done = 0;
    while (done < out.size()) {
        const uint64_t pos = offset + done;
        size_t want = out.size() - done;
        if (pos < header_.size()) {
            size_t n = std::min<size_t>(want, header_.size() - pos);
            std::memcpy(out.data() + done, header_.data() + pos, n);
            done += n;
            continue;
        }
        auto it = std::upper_bound(spans_.begin(), spans_.end(), pos,
                                   [](uint64_t p, const Span& s) { return p < s.offset; });
        if (it != spans_.begin() && pos < std::prev(it)->offset + std::prev(it)->length) {
            const Span& s = *std::prev(it);
            size_t n = std::min<uint64_t>(want, s.offset + s.length - pos);
            int fd = ::open(s.path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) break;
            size_t got = 0;
            while (got < n) {
                ssize_t r = pread(fd, out.data() + done + got, n - got, static_cast<off_t>(pos - s.offset + got));
                if (r <= 0) break;
                got += static_cast<size_t>(r);
            }
            close(fd);
            done += got;
            if (got < n) break;
            continue;
        }
        uint64_t next = it == spans_.end() ? pos + want : it->offset;
        size_t n = std::min<uint64_t>(want, next - pos);
        std::memset(out.data() + done, 0, n);
        done += n;
    }
    return done;
}

namespace {
void check_header(const std::string& where, const std::vector<uint8_t>& header) {
    if (be32(&header[0x1C]) != kMagic) throw DiscError(where + ": not a GameCube disc image (bad magic)");
    std::string id(reinterpret_cast<const char*>(header.data()), 6);
    if (id != Disc::kGameId) throw DiscError(where + ": disc is " + id + ", expected " + Disc::kGameId + " (Pikmin, PAL)");
}

std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw DiscError(path + ": cannot open");
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
}  // namespace

Disc Disc::open(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return open_dir(path);
    Disc d;
    d.fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (d.fd_ < 0) throw DiscError(path + ": cannot open disc image: " + std::strerror(errno));
    d.header_.resize(0x440);
    if (d.read(0, d.header_) != d.header_.size()) throw DiscError(path + ": too small to be a GameCube disc image");
    check_header(path, d.header_);
    fstat(d.fd_, &st);
    const uint64_t image_size = static_cast<uint64_t>(st.st_size);
    uint32_t fst_off = be32(&d.header_[0x424]), fst_size = be32(&d.header_[0x428]);
    if (static_cast<uint64_t>(fst_off) + fst_size > image_size) throw DiscError(path + ": truncated disc image (file table)");
    std::vector<uint8_t> fst(fst_size);
    if (fst_size < 12 || d.read(fst_off, fst) != fst_size) throw DiscError(path + ": cannot read the file table");
    d.parse_fst(path, fst, image_size);
    return d;
}

Disc Disc::open_dir(const std::string& dir) {
    Disc d;
    d.header_ = slurp(dir + "/sys/boot.bin");
    if (d.header_.size() < 0x440) throw DiscError(dir + "/sys/boot.bin: too small");
    d.header_.resize(0x440);
    check_header(dir, d.header_);
    d.parse_fst(dir, slurp(dir + "/sys/fst.bin"), 0);
    // paths.txt: line i = files/-relative path of entry i (tools/fst_paths.py).
    std::ifstream idx(dir + "/sys/paths.txt");
    if (!idx) throw DiscError(dir + "/sys/paths.txt: missing (re-run tools/extract_gcm.py or tools/fst_paths.py)");
    std::string line;
    for (size_t i = 0; std::getline(idx, line); ++i) {
        if (line.empty() || i >= d.entries_.size() || d.entries_[i].is_dir) continue;
        const FileRef& f = d.entries_[i];
        d.spans_.push_back({f.offset, f.length, dir + "/files/" + line});
    }
    std::sort(d.spans_.begin(), d.spans_.end(), [](const Span& a, const Span& b) { return a.offset < b.offset; });
    size_t files = std::count_if(d.entries_.begin(), d.entries_.end(), [](const FileRef& f) { return !f.is_dir; });
    if (d.spans_.size() != files) throw DiscError(dir + "/sys/paths.txt: does not match sys/fst.bin");
    return d;
}

void Disc::parse_fst(const std::string& path, const std::vector<uint8_t>& fst, uint64_t image_size) {
    const uint32_t fst_size = static_cast<uint32_t>(fst.size());
    if (fst_size < 12) throw DiscError(path + ": cannot read the file table");
    uint32_t count = be32(&fst[8]);
    if (count == 0 || static_cast<uint64_t>(count) * 12 > fst_size) throw DiscError(path + ": corrupt file table");
    const uint32_t names = count * 12;

    entries_.resize(count);
    entries_[0] = {0, count, true};
    paths_["/"] = 0;
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
        if (image_size && !is_dir && static_cast<uint64_t>(w1) + w2 > image_size) {
            throw DiscError(path + ": truncated disc image (" + full + " ends past the end of the file)");
        }
        entries_[i] = {is_dir ? 0 : w1, w2, is_dir};  // dirs: length field holds the next-entry index
        paths_[full] = static_cast<int>(i);
        if (is_dir) stack.push_back({w2, full});
    }
}

int Disc::path_to_entry(std::string_view path) const {
    auto it = paths_.find(normalise(path));
    return it == paths_.end() ? -1 : it->second;
}

}  // namespace rt
