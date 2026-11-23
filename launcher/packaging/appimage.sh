#!/usr/bin/env bash
# Builds the Linux launcher and packs build/PikminRecompLauncher-<ver>-x86_64.AppImage with
# linuxdeploy (+ its Qt plugin), expected in build/tools:
#   https://github.com/linuxdeploy/linuxdeploy/releases (linuxdeploy-x86_64.AppImage)
#   https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases (linuxdeploy-plugin-qt-x86_64.AppImage)
# usage: launcher/packaging/appimage.sh [VERSION]
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
ver="${1:-0.2.0}"
build="$root/build/launcher-release"
appdir="$root/build/launcher-appdir"
tools="$root/build/tools"
cmake -S "$root/launcher" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DPIKMIN_LAUNCHER_VERSION="$ver" >/dev/null
cmake --build "$build" --target pikmin-launcher
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" >/dev/null
export QML_SOURCES_PATHS="$root/launcher/qml" EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export QMAKE=/usr/bin/qmake6 APPIMAGE_EXTRACT_AND_RUN=1 LINUXDEPLOY_OUTPUT_VERSION="$ver"
export LDAI_OUTPUT="$root/build/PikminRecompLauncher-$ver-x86_64.AppImage"
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
    -d "$root/launcher/packaging/pikminrecomp-launcher.desktop" -i "$root/launcher/assets/pikminrecomp-launcher.svg" \
    --plugin qt --output appimage >/dev/null
out="$LDAI_OUTPUT"
echo "$out ($(du -h "$out" | cut -f1))"
echo "sha256 $(basename "$out") $(sha256sum "$out" | cut -d' ' -f1)"
