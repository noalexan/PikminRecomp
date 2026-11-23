#include "core/version.hpp"

#include <QStringList>

namespace launcher {
int compare_versions(const QString& a, const QString& b) {
    auto parts = [](QString s) {
        if (s.startsWith('v')) s.remove(0, 1);
        return s.split('.');
    };
    const QStringList x = parts(a), y = parts(b);
    for (int i = 0; i < std::max(x.size(), y.size()); ++i) {
        const int p = i < x.size() ? x[i].toInt() : 0, q = i < y.size() ? y[i].toInt() : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}
}  // namespace launcher
