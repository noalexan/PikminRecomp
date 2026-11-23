#include "core/progress.hpp"

#include <QRegularExpression>

namespace launcher {
std::optional<std::pair<int, int>> parse_ninja_progress(const QString& line) {
    static const QRegularExpression re(R"(^\[(\d+)/(\d+)\])");
    const auto m = re.match(line);
    if (!m.hasMatch()) return std::nullopt;
    return std::pair{m.captured(1).toInt(), m.captured(2).toInt()};
}
}  // namespace launcher
