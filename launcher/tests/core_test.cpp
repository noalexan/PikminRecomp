#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "core/progress.hpp"
#include "core/release.hpp"
#include "core/rom.hpp"
#include "core/toolchain.hpp"
#include "core/version.hpp"

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

using namespace launcher;

static void write(const QString& path, const QByteArray& b) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(b);
}

int main() {
    // versions
    CHECK(compare_versions("v1.2.0", "1.10.0") < 0);
    CHECK(compare_versions("1.2", "v1.2.0") == 0);
    CHECK(compare_versions("v2.0.0", "v1.9.9") > 0);

    // ninja progress
    auto p = parse_ninja_progress("[123/4567] Building C object gen/gen_00.c.obj");
    CHECK(p && p->first == 123 && p->second == 4567);
    CHECK(!parse_ninja_progress("-- Configuring done"));

    // release JSON
    const QByteArray json = R"({"tag_name":"v0.2.0","body":"notes\nsha256 PikminRecompLauncher-0.2.0-x86_64.AppImage abcdef0123\n",
      "assets":[{"name":"PikminRecompLauncher-0.2.0-windows.zip","size":10,"browser_download_url":"https://x/w.zip"},
                {"name":"PikminRecompLauncher-0.2.0-x86_64.AppImage","size":20,"browser_download_url":"https://x/l.AppImage"}]})";
    auto r = parse_release(json, "linux");
    CHECK(r && r->version == "0.2.0" && r->assetUrl == "https://x/l.AppImage" && r->assetSize == 20 && r->sha256 == "abcdef0123");
    auto w = parse_release(json, "windows");
    CHECK(w && w->assetUrl == "https://x/w.zip" && w->sha256.isEmpty());
    CHECK(!parse_release("{\"message\":\"API rate limit exceeded\"}", "linux"));
    CHECK(!parse_release("not json", "linux"));
    // the release list: newest non-draft with this platform's asset, prereleases included
    const QByteArray list = R"([
      {"tag_name":"v0.4.0","draft":true,"assets":[{"name":"PikminRecompLauncher-0.4.0-windows.zip","browser_download_url":"https://x/d.zip"}]},
      {"tag_name":"v0.3.0","prerelease":true,"assets":[{"name":"PikminRecompLauncher-0.3.0-windows.zip","browser_download_url":"https://x/3.zip"}]},
      {"tag_name":"v0.2.0","assets":[{"name":"PikminRecompLauncher-0.2.0-x86_64.AppImage","browser_download_url":"https://x/2.AppImage"}]}])";
    auto lw = parse_release(list, "windows");
    CHECK(lw && lw->version == "0.3.0" && lw->assetUrl == "https://x/3.zip");
    auto ll = parse_release(list, "linux");
    CHECK(ll && ll->version == "0.2.0" && ll->assetUrl == "https://x/2.AppImage");
    auto none = parse_release("[]", "linux");
    CHECK(none && none->version.isEmpty());
    CHECK(source_archive_url("0.2.0", false) == "https://github.com/noalexan/PikminRecomp/archive/refs/tags/v0.2.0.tar.gz");

    // toolchain helpers
    CHECK(version_token("cmake version 3.31.6\n\nCMake suite") == "3.31.6");
    CHECK(version_token("Python 3.14.0") == "3.14.0");
    CHECK(!install_hint(Os::Fedora).isEmpty() && install_hint(Os::Windows).contains("pacman"));

    // ROM: bad inputs
    QTemporaryDir tmp;
    CHECK(!check_rom(tmp.path()).ok);                      // a directory
    write(tmp.filePath("short.iso"), QByteArray(100, 0));
    CHECK(!check_rom(tmp.filePath("short.iso")).ok);
    QByteArray rvz(0x1000, 0);
    rvz.replace(0, 4, "RVZ\x01");
    write(tmp.filePath("game.rvz"), rvz);
    auto rv = check_rom(tmp.filePath("game.rvz"));
    CHECK(!rv.ok && rv.message.contains("RVZ"));
    QByteArray ntsc(0x1000, 0);
    ntsc.replace(0, 6, "GPIE01");
    ntsc[0x1c] = char(0xC2); ntsc[0x1d] = char(0x33); ntsc[0x1e] = char(0x9F); ntsc[0x1f] = char(0x3D);
    write(tmp.filePath("us.iso"), ntsc);
    auto us = check_rom(tmp.filePath("us.iso"));
    CHECK(!us.ok && us.gameId == "GPIE01" && us.message.contains("NTSC"));

    // ROM: the real disc, if present
    const QString iso = QDir::homePath() + "/Downloads/GPIP01/game.iso";
    if (QFile::exists(iso)) {
        auto good = check_rom(iso);
        CHECK(good.ok && good.gameId == "GPIP01" && good.dolSha1 == kDolSha1 && good.fstSha1 == kFstSha1);
    } else {
        std::puts("note: real ISO not available, skipped");
    }
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
