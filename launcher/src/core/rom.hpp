// Disc image check: the GameCube PAL Pikmin (GPIP01) this project recompiles.
#pragma once
#include <QString>

namespace launcher {

struct RomResult {
    bool ok = false;
    QString gameId;     // e.g. "GPIP01" ("" if not a GameCube image)
    QString message;    // for the player: why it was refused, or a short "verified" line
    QString dolSha1, fstSha1;
};

// Reads the header, main.dol and the file table (not the whole image: fast, and padding may differ
// between clean dumps).
RomResult check_rom(const QString& path);

// Reference values (main.dol / fst.bin of the GPIP01 disc the recompiler targets).
inline constexpr const char* kDolSha1 = "e5794ffe8ac72c3ead3849edbd2f27ea5e0ed440";
inline constexpr const char* kFstSha1 = "8521721056876c4e61a405791e648c555d9d2f80";

}  // namespace launcher
