#!/usr/bin/env bash
# Cross-builds the Windows launcher (Fedora MinGW Qt) and packs
# build/PikminRecompLauncher-<ver>-windows.zip: the .exe, Qt/MinGW DLLs, the Qt plugins and QML
# modules it uses, and qt.conf. usage: launcher/packaging/windows.sh [VERSION]
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
ver="${1:-0.2.3}"
mingw=/usr/x86_64-w64-mingw32/sys-root/mingw
build="$root/build/launcher-win"
stage="$root/build/launcher-win-stage/PikminRecompLauncher"
cmake -S "$root/launcher" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake -DQT_HOST_PATH=/usr -DPIKMIN_LAUNCHER_VERSION="$ver" >/dev/null
cmake --build "$build" --target pikmin-launcher
rm -rf "$stage"; mkdir -p "$stage/plugins" "$stage/qml"
cp "$build/pikmin-launcher.exe" "$stage/PikminRecompLauncher.exe"
# Qt plugins: the Windows platform, HTTPS (schannel/openssl backends), images.
for p in platforms/qwindows.dll tls imageformats/qsvg.dll imageformats/qico.dll; do
    mkdir -p "$stage/plugins/$(dirname "$p")"
    cp -r "$mingw/lib/qt6/plugins/$p" "$stage/plugins/$(dirname "$p")/"
done
# QML modules used by the UI (and what they import).
for m in QtQml QtQuick; do cp -r "$mingw/lib/qt6/qml/$m" "$stage/qml/"; done
rm -rf "$stage"/qml/QtQuick/{VirtualKeyboard,Scene2D,Scene3D,Timeline,Particles,LocalStorage,Pdf,tooling,NativeStyle} \
       "$stage"/qml/QtQuick/Controls/{Fusion,Imagine,Material,Universal,FluentWinUI3,Windows,macOS,iOS} 2>/dev/null || true
printf '[Paths]\nPlugins = plugins\nQml2Imports = qml\n' > "$stage/qt.conf"
# DLLs for the exe and every plugin, into the exe's folder.
for f in "$stage/PikminRecompLauncher.exe" $(find "$stage/plugins" "$stage/qml" -name '*.dll'); do
    python3 "$root/tools/mingw_dlls.py" x86_64-w64-mingw32-objdump "$f" "$stage" "$mingw/bin" >/dev/null
done
out="$root/build/PikminRecompLauncher-$ver-windows.zip"
rm -f "$out"
(cd "$(dirname "$stage")" && zip -qr "$out" PikminRecompLauncher)
echo "$out ($(du -h "$out" | cut -f1))"
echo "sha256 $(basename "$out") $(sha256sum "$out" | cut -d' ' -f1)"
