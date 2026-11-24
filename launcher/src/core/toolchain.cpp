#include "core/toolchain.hpp"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include "core/version.hpp"

namespace launcher {

QString version_token(const QString& text) {
    static const QRegularExpression re(R"((\d+\.\d+(?:\.\d+)?))");
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

Tool probe_tool(const QString& program, const QString& minVersion, const QStringList& extraPaths) {
    Tool t;
    t.name = program;
    QString exe = QStandardPaths::findExecutable(program, extraPaths);
    if (exe.isEmpty()) exe = QStandardPaths::findExecutable(program);
    if (exe.isEmpty()) return t;
    QProcess p;
    p.start(exe, {"--version"});
    if (!p.waitForFinished(10000)) return t;
    t.found = true;
    t.version = version_token(QString::fromLocal8Bit(p.readAllStandardOutput() + p.readAllStandardError()));
    t.tooOld = !minVersion.isEmpty() && !t.version.isEmpty() && compare_versions(t.version, minVersion) < 0;
    return t;
}

Os detect_os() {
#ifdef Q_OS_WIN
    return Os::Windows;
#else
    QFile f("/etc/os-release");
    const QString s = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).toLower() : QString();
    if (s.contains("fedora") || s.contains("rhel")) return Os::Fedora;
    if (s.contains("debian") || s.contains("ubuntu")) return Os::Debian;
    if (s.contains("arch")) return Os::Arch;
    return Os::OtherLinux;
#endif
}

QString install_hint(Os os) {
    switch (os) {
        case Os::Fedora:
            return "sudo dnf install -y gcc-c++ cmake ninja-build python3 git vulkan-headers vulkan-loader-devel "
                   "libpng-devel zlib-devel libzstd-devel freetype-devel fmt-devel wayland-devel libxkbcommon-devel "
                   "libdecor-devel libX11-devel libXext-devel libXcursor-devel libXi-devel libXfixes-devel "
                   "libXrandr-devel libXScrnSaver-devel libXtst-devel alsa-lib-devel pipewire-devel "
                   "pulseaudio-libs-devel systemd-devel dbus-devel";
        case Os::Debian:
            return "sudo apt install -y g++ cmake ninja-build python3 git libvulkan-dev libpng-dev zlib1g-dev "
                   "libzstd-dev libfreetype-dev libfmt-dev libwayland-dev libxkbcommon-dev libdecor-0-dev "
                   "libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev "
                   "libxtst-dev libasound2-dev libpipewire-0.3-dev libpulse-dev libudev-dev libdbus-1-dev";
        case Os::Arch:
            return "sudo pacman -S --needed gcc cmake ninja python git vulkan-headers vulkan-icd-loader libpng zlib zstd "
                   "freetype2 fmt wayland wayland-protocols libxkbcommon libdecor libx11 libxext libxcursor libxi "
                   "libxfixes libxrandr libxss libxtst alsa-lib pipewire libpulse systemd-libs dbus";
        case Os::Windows:
            return "pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,python,vulkan-headers,vulkan-loader,libpng,"
                   "zlib,zstd,freetype} git";
        case Os::OtherLinux: break;
    }
    return "Install: a C++20 compiler, cmake 3.25+, ninja, Python 3.11+, git, Vulkan headers/loader, libpng, zlib, "
           "zstd, freetype, and SDL3's build dependencies: Wayland, X11, ALSA, PipeWire, PulseAudio, udev, D-Bus "
           "(development packages; SDL3 itself is built with the game).";
}

}  // namespace launcher
