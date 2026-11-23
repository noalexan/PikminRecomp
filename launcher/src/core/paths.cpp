#include "core/paths.hpp"

#include <QDir>
#include <QStandardPaths>

namespace launcher {
QString data_dir() {
#ifdef Q_OS_WIN
    const QString base = qEnvironmentVariable("LOCALAPPDATA", QDir::homePath() + "/AppData/Local");
#else
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    const QString base = xdg.isEmpty() ? QDir::homePath() + "/.local/share" : xdg;
#endif
    return QDir::cleanPath(base + "/pikminrecomp-launcher");
}
}  // namespace launcher
