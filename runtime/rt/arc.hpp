// Game archives (dataDir/archives/*.arc + *.dir). The .dir is: u32 total size, u32 count, then per
// file u32 offset, u32 size, u32 name length (padded to 4), name (NUL-padded). Files in the .arc
// start 32-aligned and their sizes are multiples of 32.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rt::arc {

struct Entry {
    uint32_t offset = 0, size = 0;
    std::string name;
    size_t dir_at = 0;  // offset of this entry's record in the .dir
};

std::optional<std::vector<Entry>> parse_dir(const std::vector<uint8_t>& dir);
const Entry* find(const std::vector<Entry>& entries, const std::string& name);
// Appends `bytes` (padded to 32) to the archive and points the entry at it. False if it isn't there.
bool replace(std::vector<uint8_t>& arc, std::vector<uint8_t>& dir, const std::string& name,
             const std::vector<uint8_t>& bytes);

}  // namespace rt::arc
