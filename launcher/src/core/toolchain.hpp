// Is the game's toolchain installed? And how to install what's missing.
#pragma once
#include <QString>
#include <QStringList>

namespace launcher {

struct Tool {
    QString name;        // "cmake"
    bool found = false;
    QString version;     // first version-looking token of its --version output
    bool tooOld = false;
};

enum class Os { Fedora, Debian, Arch, OtherLinux, Windows };

// Runs `<program> --version` (searching PATH plus extraPaths) and compares with minVersion ("" = any).
Tool probe_tool(const QString& program, const QString& minVersion, const QStringList& extraPaths = {});
// Extracts "3.31.6" from "cmake version 3.31.6".
QString version_token(const QString& text);
Os detect_os();
// One command line installing every missing tool + the game's libraries for that OS.
QString install_hint(Os os);

}  // namespace launcher
