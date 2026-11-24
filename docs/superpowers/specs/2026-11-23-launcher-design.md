# Pikmin Recomp Launcher — Design

## Why

The project is a *static recompilation*: the game should be recompiled on the player's machine from
their own disc, never shipped prebuilt. Doing that by hand (toolchain, extract, configure, build,
install, keep up to date) is too much for players. A launcher — the only thing published on GitHub —
does it all behind a friendly, Pikmin-styled window, on Linux and Windows.

## Goals

- **One published artifact per platform: the launcher** (Linux AppImage, Windows zip), on GitHub
  Releases of `noalexan/PikminRecomp`. No game binary and no Nintendo data are ever published.
- **Self-update:** on start the launcher checks the latest GitHub release; if newer, it offers to
  update, replaces itself and restarts.
- **Version-locked game source:** launcher vX builds the game from the vX tag's source archive
  (downloaded from GitHub, no git needed). Updating the launcher → the game is rebuilt from the new
  source.
- **Checks, each shown as a row with a state (ok / problem with a fix / working):**
  - **Toolchain:** C/C++ compiler (GCC), cmake ≥ 3.25, ninja, Python ≥ 3.11, git (the game build
    fetches aurora and Dawn with git) and the game's libraries
    (Vulkan headers, libpng, zlib, zstd, freetype; SDL3 is built from source and linked statically into
    the game, `-DPIKMIN_VENDOR_SDL3=ON`, so only its backends' headers are needed: Wayland, X11, ALSA,
    PipeWire, PulseAudio, udev, D-Bus). Missing pieces are listed with the exact
    command to install them (Linux: one `sudo dnf install …` / `apt` line; Windows: MSYS2, below).
  - **ROM:** the player picks a disc image. Checked: GameCube magic, game ID `GPIP01` (PAL Pikmin),
    and the SHA-1 of `main.dol` and the file table against the values this project is built for
    (the recompiler's input). A wrong game/region, NKit/RVZ/compressed image or modified dump gets a
    clear message. The full-image SHA-1 is shown for information.
  - **Game:** built from version X / not built / needs rebuild (launcher or source changed).
- **Build:** extract the disc into the source tree (`tools/extract_gcm.py`), configure, build the
  Release game, install it into the launcher's folder. A progress bar driven by ninja's `[n/total]`,
  current step text, and an expandable log. Cancel stops the build; a later build resumes.
- **Play:** starts the installed game. Settings stay in the game (in-game menus); saves and config
  stay where the game keeps them.
- **Look: "Forest"** (chosen from mockups): deep green radial gradient, soft leaf shapes, frosted
  glass panels with rounded corners like the game's menus, yellow rounded "PLAY/BUILD" button with a
  drop shadow, a yellow-outlined "Update available" pill. All artwork drawn (QML shapes, gradients,
  our own icons) — no Nintendo assets. Smooth transitions between states.

## Non-goals

- Downloading or bundling the ROM, prebuilt game binaries, or any Nintendo data.
- Installing system packages silently: the launcher shows commands (and on Windows can start the
  MSYS2 installer after the player confirms), it never runs `sudo` itself.
- macOS. Game settings in the launcher (they live in the game).
- CI: releases are built on the maintainer's Fedora machine for now (Linux natively, Windows
  cross-compiled with MinGW Qt).

## Anti-goals

- A launcher that publishes or uploads anything of the player's.
- Building a game from source that doesn't match the launcher's version.
- Breaking a working install: a failed or cancelled build keeps the previous game playable.

## Constraints

- Qt 6 + QML (Qt Quick, Qt Quick Controls with a custom style), C++20, CMake. Fedora packages:
  `qt6-qtbase-devel qt6-qtdeclarative-devel` (Linux), `mingw64-qt6-qtbase mingw64-qt6-qtdeclarative`
  (Windows cross build).
- The launcher is its own CMake project (`launcher/`), independent of the game build (it must build
  without the game's dependencies).
- Network: GitHub REST API (`/repos/noalexan/PikminRecomp/releases/latest`) and release asset /
  source-archive downloads, through Qt Network (TLS from the system).

## Approaches considered

- **Qt/QML (chosen by the user).** Polished, stylable, native; Qt adds ~30–50 MB to the package.
- SDL3 + RmlUi: smallest, reuses aurora's stack, CSS-like styling; less tooling for animated UI.
- Tauri: easiest beautiful UI, but adds Rust and WebKitGTK at runtime on Linux.
- Python + Tkinter: no real styling.

## How it works

### Folders

| | Linux | Windows |
|---|---|---|
| Launcher data | `~/.local/share/pikminrecomp-launcher/` | `%LOCALAPPDATA%\pikminrecomp-launcher\` |
| Source | `<data>/src/<version>/` (tag archive, `orig/GPIP01` extracted here) | same |
| Build | `<data>/build/` (kept between versions: incremental rebuilds, fetched Dawn reused) | same |
| Game | `<data>/game/` (`cmake --install`; `bin/pikmin` + `share/pikminrecomp/data` on Linux, `pikmin.exe` + DLLs + `data/` on Windows) | same |
| State | `<data>/launcher.json` (ISO path, built version, last check) | same |

Saves/config remain the game's (`~/.local/share/pikminrecomp`, `%APPDATA%\pikminrecomp`).

### Windows toolchain

MSYS2 UCRT64 (`C:\msys64` or found via the registry/PATH): `pacman -S --needed
mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,python,vulkan-headers,vulkan-loader,libpng,zlib,zstd,freetype}`.
If MSYS2 is missing, the launcher offers to download and run its official installer (after the
player confirms), then shows the pacman line to paste (or runs it in an MSYS2 shell on confirm).
The game build runs `cmake`/`ninja` from that environment. *Untested until a Windows machine is
available; the cross-compiled game proves the code builds with MinGW.*

### Self-update

- Release assets: `PikminRecompLauncher-<ver>-x86_64.AppImage`, `PikminRecompLauncher-<ver>-windows.zip`.
- Linux: download to a temp file next to the AppImage, verify size (and the SHA-256 listed in the
  release body), `rename` over the running AppImage, re-exec.
- Windows: download the zip, extract next to the install, rename the running `.exe` to `.old`, move
  the new one in, restart; delete `.old` on next start.
- Never auto-updates silently: an "Update" pill, one click.

### Build steps (each logged)

1. Download `/archive/refs/tags/v<ver>.tar.gz` (Linux) / `.zip` (Windows) into `src/<ver>/` if missing.
2. `python tools/extract_gcm.py <iso> src/<ver>/orig/GPIP01` (skipped if present and unchanged).
3. `cmake -S src/<ver> -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<data>/game-new`
   (first run fetches aurora/Dawn: shown as "Downloading dependencies").
4. `cmake --build build --target pikmin` (progress from ninja's `[n/total]` lines).
5. `cmake --install build`, then swap `game-new` → `game` (the old game stays until this succeeds).

### ROM reference values

Built into the launcher (`launcher/src/rom.cpp`): game ID `GPIP01`, SHA-1 of `main.dol` and of
`fst.bin` as extracted from the maintainer's verified disc. (Full-image SHA-1 of that disc:
`40c46bd6921e55558e9838930a9ffd2179802b4f`; *the builder should confirm it against the Redump
entry for GPIP01 before release.*)

## Versioning (my call)

Releases are tagged `vMAJOR.MINOR.PATCH` starting at `v0.1.0`; the launcher's version is compiled in
from the tag. Publishing a release (`gh release create` with the two artifacts) is done by the
maintainer — the build scripts only produce the files.

## Left to the builder

- QML component structure, exact animations, fonts (a free rounded font such as Nunito, bundled).
- Distro detection for the Linux install command (dnf / apt / pacman lines).
- AppImage packaging (`linuxdeploy` + Qt plugin) and Windows zip (`windeployqt`-equivalent copy of
  MinGW Qt DLLs/plugins/QML modules) scripts in `launcher/packaging/`.
