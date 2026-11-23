#pragma once
#include <QString>
#include <optional>
#include <utility>

namespace launcher {
// ninja status lines "[123/4567] Building ..." -> {123, 4567}.
std::optional<std::pair<int, int>> parse_ninja_progress(const QString& line);
}  // namespace launcher
