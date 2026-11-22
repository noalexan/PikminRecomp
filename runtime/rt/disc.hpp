// Read-only access to a plain GameCube disc image (ISO/GCM): header, FST lookups, reads.
#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rt {

struct DiscError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct FileRef {
    uint32_t offset = 0;
    uint32_t length = 0;
    bool is_dir = false;
};

class Disc {
public:
    static constexpr const char* kGameId = "GPIP01";

    static Disc open(const std::string& path);
    Disc(Disc&&) noexcept;
    Disc& operator=(Disc&&) noexcept;
    ~Disc();

    const uint8_t* header() const { return header_.data(); }  // first 0x440 bytes
    // Case-insensitive; leading '/' optional; '.' and '..' resolved. -1 if absent.
    int path_to_entry(std::string_view path) const;
    FileRef file(int entry) const { return entries_.at(static_cast<size_t>(entry)); }
    size_t entry_count() const { return entries_.size(); }
    size_t read(uint64_t offset, std::span<uint8_t> out) const;

private:
    Disc() = default;
    int fd_ = -1;
    std::vector<uint8_t> header_;
    std::vector<FileRef> entries_;
    std::unordered_map<std::string, int> paths_;  // normalised lowercase path -> entry
};

void set_disc(std::unique_ptr<Disc> d);
Disc* disc();

}  // namespace rt
