// Windows self-update: a running program's own files (its .exe, DLLs, plugins) can't be
// overwritten, but they can be renamed. The update is unpacked into a staging folder, then each
// file is swapped in by renaming the old one to <name>.old; the .old files go on the next start.
#pragma once
#include <QString>

namespace launcher {

// Moves every file under `staged` to the same relative path under `target`, renaming any file it
// replaces to <name>.old. On failure, puts back what it already swapped and returns false with
// the reason in *error.
bool swap_in_tree(const QString& staged, const QString& target, QString* error);
// Deletes the *.old files a previous swap left under `dir` (ignores files still in use).
void remove_old_files(const QString& dir);

}  // namespace launcher
