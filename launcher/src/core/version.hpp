#pragma once
#include <QString>

namespace launcher {
// "v1.2.0" / "1.2" style; <0, 0, >0 like strcmp. Missing parts count as 0.
int compare_versions(const QString& a, const QString& b);
}  // namespace launcher
