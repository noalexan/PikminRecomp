#include "app/launcher.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "core/paths.hpp"
#include "core/progress.hpp"
#include "core/release.hpp"
#include "core/rom.hpp"
#include "core/toolchain.hpp"
#include "core/version.hpp"

namespace launcher {
namespace {
constexpr const char* kLatestApi = "https://api.github.com/repos/noalexan/PikminRecomp/releases/latest";
#ifdef Q_OS_WIN
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

QString data(const QString& rel) { return data_dir() + "/" + rel; }

bool remove_tree(const QString& path) { return !QFileInfo::exists(path) || QDir(path).removeRecursively(); }
}  // namespace

Launcher::Launcher(QObject* parent) : QObject(parent) {
    QDir().mkpath(data_dir());
    // A previous self-update on Windows leaves the old executable behind.
    QFile::remove(QCoreApplication::applicationFilePath() + ".old");
    load_state();
}

// ---- state --------------------------------------------------------------------------------------

void Launcher::load_state() {
    QFile f(data("launcher.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    iso_ = o.value("iso").toString();
    built_version_ = o.value("builtVersion").toString();
}

void Launcher::save_state() {
    QJsonObject o;
    o["iso"] = iso_;
    o["builtVersion"] = built_version_;
    QFile f(data("launcher.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(o).toJson());
}

QString Launcher::source_dir() const {
    // Development: build a local checkout instead of downloading the tag's source.
    const QString dev = qEnvironmentVariable("PIKMIN_LAUNCHER_SOURCE");
    return dev.isEmpty() ? data("src") : dev;
}

QString Launcher::game_exe() const { return kWindows ? data("game/pikmin.exe") : data("game/bin/pikmin"); }

QStringList Launcher::tool_paths() const {
    if (!kWindows) return {};
    const QString msys = qEnvironmentVariable("PIKMIN_MSYS2", "C:/msys64");
    return {msys + "/ucrt64/bin", msys + "/usr/bin"};
}

QProcessEnvironment Launcher::build_env() const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList extra = tool_paths();
    if (!extra.isEmpty()) env.insert("PATH", extra.join(';') + ";" + env.value("PATH"));
    return env;
}

void Launcher::preview(const QString& state) {
    latest_ = "0.2.0";
    update_available_ = true;
    update_text_ = "Update 0.2.0 available";
    toolchain_ok_ = true;
    toolchain_text_ = "gcc 16.2 · cmake 3.31 · ninja 1.12 · python 3.14 · git 2.51";
    rom_ok_ = true;
    iso_ = "/home/player/Games/Pikmin (Europe).iso";
    rom_text_ = "Pikmin · PAL (GPIP01) · verified";
    if (state == "building") {
        busy_ = true;
        progress_ = 0.62;
        step_text_ = "Recompiling the game… 2814 / 4520";
        game_state_ = "none";
        game_text_ = "Building version " + version();
    } else {
        game_state_ = "built";
        game_text_ = "Built from version " + version();
    }
    emit changed();
}

// ---- checks -------------------------------------------------------------------------------------

void Launcher::refresh() {
    check_toolchain();
    check_rom();
    check_game();
    check_update();
}

void Launcher::check_toolchain() {
    struct Need {
        QString program, min;
    };
    const QList<Need> needs = {{kWindows ? "g++" : "g++", "12"}, {"cmake", "3.25"}, {"ninja", ""},
                               {kWindows ? "python" : "python3", "3.11"}, {"git", ""}};
    QStringList ok, missing;
    for (const Need& n : needs) {
        const Tool t = probe_tool(n.program, n.min, tool_paths());
        if (!t.found) missing << n.program;
        else if (t.tooOld) missing << n.program + " " + t.version + " (needs " + n.min + "+)";
        else ok << n.program + " " + t.version;
    }
    toolchain_ok_ = missing.isEmpty();
    toolchain_text_ = toolchain_ok_ ? ok.join(" · ") : "Missing: " + missing.join(", ");
    toolchain_hint_ = toolchain_ok_ ? QString() : install_hint(detect_os());
    if (!toolchain_ok_ && kWindows && !QFileInfo::exists(tool_paths().first()))
        toolchain_text_ = "MSYS2 isn't installed (https://www.msys2.org). Install it, then run in its UCRT64 shell:";
    emit changed();
}

void Launcher::check_rom() {
    if (iso_.isEmpty()) {
        rom_ok_ = false;
        rom_text_ = "Choose your Pikmin disc image (PAL, .iso)";
    } else if (!QFileInfo::exists(iso_)) {
        rom_ok_ = false;
        rom_text_ = "The disc image moved or was deleted: choose it again";
    } else {
        const RomResult r = launcher::check_rom(iso_);
        rom_ok_ = r.ok;
        rom_text_ = r.message;
    }
    emit changed();
}

void Launcher::check_game() {
    if (!QFileInfo::exists(game_exe())) {
        game_state_ = "none";
        game_text_ = "Not built yet";
    } else if (built_version_ != version()) {
        game_state_ = "outdated";
        game_text_ = "Built from version " + built_version_ + ": rebuild for " + version();
    } else {
        game_state_ = "built";
        game_text_ = "Built from version " + version();
    }
    emit changed();
}

void Launcher::check_update() {
    QNetworkRequest req{QUrl(kLatestApi)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "PikminRecompLauncher");  // GitHub requires one
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply* r = net_.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const auto rel = r->error() == QNetworkReply::NoError ? parse_release(r->readAll(), kWindows ? "windows" : "linux")
                                                               : std::nullopt;
        if (!rel) {
            update_available_ = false;
            update_text_ = "Couldn't check for updates";
        } else {
            latest_ = rel->version;
            release_url_ = rel->assetUrl;
            release_sha_ = rel->sha256;
            release_name_ = rel->assetName;
            release_size_ = rel->assetSize;
            update_available_ = compare_versions(rel->version, version()) > 0 && !rel->assetUrl.isEmpty();
            update_text_ = update_available_ ? "Update " + rel->version + " available" : "Up to date";
        }
        emit changed();
    });
}

void Launcher::chooseIso(const QUrl& url) {
    iso_ = url.isLocalFile() ? url.toLocalFile() : url.toString();
    save_state();
    check_rom();
}

void Launcher::copyToClipboard(const QString& text) {
    if (auto* cb = QGuiApplication::clipboard()) cb->setText(text);
}

// ---- running steps ------------------------------------------------------------------------------

void Launcher::append_log(const QString& text) {
    log_ += text;
    if (log_.size() > 200000) log_ = log_.right(150000);  // keep the window responsive
    emit changed();
}

void Launcher::run_steps(std::vector<Step> steps) {
    steps_ = std::move(steps);
    step_ = 0;
    done_weight_ = 0;
    total_weight_ = 0;
    for (const Step& s : steps_) total_weight_ += s.weight;
    cancelled_ = false;
    busy_ = true;
    progress_ = 0;
    log_.clear();
    emit changed();
    steps_[0].run();
}

void Launcher::next() {
    if (cancelled_) return;
    done_weight_ += steps_[step_].weight;
    progress_ = done_weight_ / total_weight_;
    if (++step_ >= steps_.size()) {
        busy_ = false;
        step_text_.clear();
        emit changed();
        return;
    }
    step_text_ = steps_[step_].label;
    append_log("\n== " + step_text_ + "\n");
    steps_[step_].run();
}

void Launcher::fail(const QString& why) {
    busy_ = false;
    step_text_ = why;
    append_log("\n!! " + why + "\n");
    check_game();
    emit changed();
}

void Launcher::run_process(const QString& program, const QStringList& args, const QString& cwd) {
    proc_ = new QProcess(this);
    proc_->setProcessEnvironment(build_env());
    proc_->setWorkingDirectory(cwd);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    const double base = done_weight_, w = steps_[step_].weight;
    const QString label = steps_[step_].label;
    connect(proc_, &QProcess::readyRead, this, [this, base, w, label] {
        while (proc_ && proc_->canReadLine()) {
            const QString line = QString::fromLocal8Bit(proc_->readLine());
            if (auto p = parse_ninja_progress(line); p && p->second > 0) {
                progress_ = (base + w * p->first / p->second) / total_weight_;
                step_text_ = label + " " + QString::number(p->first) + " / " + QString::number(p->second);
            }
            append_log(line);
        }
    });
    connect(proc_, &QProcess::finished, this, [this, program](int code, QProcess::ExitStatus st) {
        proc_->deleteLater();
        proc_ = nullptr;
        if (cancelled_) return;
        if (st != QProcess::NormalExit || code != 0) fail(QFileInfo(program).fileName() + " failed (exit " + QString::number(code) + ")");
        else next();
    });
    connect(proc_, &QProcess::errorOccurred, this, [this, program](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && proc_) {
            proc_->deleteLater();
            proc_ = nullptr;
            fail("Couldn't start " + program);
        }
    });
    append_log("$ " + program + " " + args.join(' ') + "\n");
    proc_->start(program, args);
}

void Launcher::download(const QUrl& url, const QString& to, std::function<void()> done) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "PikminRecompLauncher");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* file = new QFile(to, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete file;
        fail("Can't write " + to);
        return;
    }
    reply_ = net_.get(req);
    const double base = done_weight_, w = steps_[step_].weight;
    connect(reply_, &QNetworkReply::readyRead, this, [this, file] { file->write(reply_->readAll()); });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this, base, w](qint64 got, qint64 total) {
        if (total > 0) progress_ = (base + w * double(got) / double(total)) / total_weight_;
        emit changed();
    });
    connect(reply_, &QNetworkReply::finished, this, [this, file, url, done] {
        file->write(reply_->readAll());
        file->close();
        file->deleteLater();
        const bool ok = reply_->error() == QNetworkReply::NoError;
        const QString err = reply_->errorString();
        reply_->deleteLater();
        reply_ = nullptr;
        if (cancelled_) return;
        if (!ok) fail("Download failed: " + err + " (" + url.toString() + ")");
        else done();
    });
}

void Launcher::cancel() {
    cancelled_ = true;
    if (proc_) proc_->kill();
    if (reply_) reply_->abort();
    busy_ = false;
    step_text_ = "Cancelled";
    append_log("\n!! Cancelled\n");
    check_game();
}

// ---- the game build -----------------------------------------------------------------------------

void Launcher::build() {
    if (busy_ || !rom_ok_ || !toolchain_ok_) return;
    const QString ver = version(), src = source_dir(), build = data("build"), stage = data("game-new");
    const QString python = kWindows ? "python" : "python3";
    const bool dev = !qEnvironmentVariable("PIKMIN_LAUNCHER_SOURCE").isEmpty();
    std::vector<Step> steps;

    if (!dev) {
        steps.push_back({"Downloading the game source", 5, [this, ver] {
                             QFile v(data("src/VERSION.launcher"));
                             if (v.open(QIODevice::ReadOnly) && QString::fromUtf8(v.readAll()).trimmed() == ver) {
                                 append_log("source " + ver + " already present\n");
                                 next();
                                 return;
                             }
                             download(QUrl(source_archive_url(ver, kWindows)), data(kWindows ? "src.zip" : "src.tar.gz"),
                                      [this] { next(); });
                         }});
        steps.push_back({"Unpacking the source", 2, [this, ver] {
                             QFile v(data("src/VERSION.launcher"));
                             if (v.open(QIODevice::ReadOnly) && QString::fromUtf8(v.readAll()).trimmed() == ver) {
                                 next();
                                 return;
                             }
                             remove_tree(data("src-new"));
                             QDir().mkpath(data("src-new"));
                             // bsdtar on Windows 10+ reads zip; GNU tar on Linux.
                             run_process("tar", {"-xf", data(kWindows ? "src.zip" : "src.tar.gz"), "-C", data("src-new"),
                                                 "--strip-components=1"},
                                         data_dir());
                         }});
        steps.push_back({"Installing the source", 1, [this, ver] {
                             if (QFileInfo::exists(data("src-new"))) {
                                 // Keep the extracted disc (orig/) across versions.
                                 if (QFileInfo::exists(data("src/orig"))) QDir().rename(data("src/orig"), data("src-new/orig"));
                                 remove_tree(data("src"));
                                 if (!QDir().rename(data("src-new"), data("src"))) {
                                     fail("Can't replace the source folder");
                                     return;
                                 }
                                 QFile v(data("src/VERSION.launcher"));
                                 if (v.open(QIODevice::WriteOnly)) v.write(ver.toUtf8());
                             }
                             next();
                         }});
    }
    steps.push_back({"Extracting your disc", 8, [this, src, python] {
                         QFile dol(src + "/orig/GPIP01/sys/main.dol");
                         if (dol.open(QIODevice::ReadOnly) &&
                             QCryptographicHash::hash(dol.readAll(), QCryptographicHash::Sha1).toHex() == kDolSha1 &&
                             QFileInfo::exists(src + "/orig/GPIP01/sys/paths.txt")) {
                             append_log("disc already extracted\n");
                             next();
                             return;
                         }
                         run_process(python, {src + "/tools/extract_gcm.py", iso_, src + "/orig/GPIP01"}, src);
                     }});
    steps.push_back({"Configuring", 7, [this, src, build, stage] {
                         run_process("cmake", {"-S", src, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                                               "-DCMAKE_INSTALL_PREFIX=" + stage},
                                     src);
                     }});
    steps.push_back({"Recompiling the game", 75, [this, build, src] {
                         run_process("cmake", {"--build", build, "--target", "pikmin"}, src);
                     }});
    steps.push_back({"Installing", 2, [this, build, stage, src] {
                         remove_tree(stage);
                         run_process("cmake", {"--install", build}, src);
                     }});
    steps.push_back({"Finishing", 1, [this, stage] {
                         // The old game stays until the new one is complete.
                         remove_tree(data("game-old"));
                         if (QFileInfo::exists(data("game")) && !QDir().rename(data("game"), data("game-old"))) {
                             fail("Can't replace the installed game (is it running?)");
                             return;
                         }
                         if (!QDir().rename(stage, data("game"))) {
                             QDir().rename(data("game-old"), data("game"));
                             fail("Can't install the new game");
                             return;
                         }
                         remove_tree(data("game-old"));
                         built_version_ = version();
                         save_state();
                         check_game();
                         next();
                     }});
    step_text_ = steps.front().label;
    run_steps(std::move(steps));
}

void Launcher::play() {
    if (!QFileInfo::exists(game_exe())) return;
    QProcess::startDetached(game_exe(), {}, QFileInfo(game_exe()).absolutePath());
}

// ---- self-update --------------------------------------------------------------------------------

void Launcher::update() {
    if (busy_ || !update_available_ || release_url_.isEmpty()) return;
    const QString tmp = data(release_name_ + ".part");
    std::vector<Step> steps;
    steps.push_back({"Downloading the new launcher", 9, [this, tmp] { download(QUrl(release_url_), tmp, [this] { next(); }); }});
    steps.push_back({"Installing the new launcher", 1, [this, tmp] {
                         QFile f(tmp);
                         if (!f.open(QIODevice::ReadOnly) || (release_size_ && f.size() != release_size_)) {
                             fail("The download is incomplete");
                             return;
                         }
                         const QByteArray bytes = f.readAll();
                         f.close();
                         if (!release_sha_.isEmpty() &&
                             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != release_sha_.toLatin1()) {
                             fail("The download doesn't match its checksum");
                             return;
                         }
                         QString target;
                         if (kWindows) {
                             // Unpack next to the running launcher, moving the running .exe aside.
                             const QString dir = QCoreApplication::applicationDirPath(), exe = QCoreApplication::applicationFilePath();
                             QFile::remove(exe + ".old");
                             QFile::rename(exe, exe + ".old");
                             QProcess tar;
                             tar.start("tar", {"-xf", tmp, "-C", dir, "--strip-components=1"});
                             if (!tar.waitForFinished(120000) || tar.exitCode() != 0) {
                                 QFile::rename(exe + ".old", exe);
                                 fail("Couldn't unpack the update");
                                 return;
                             }
                             target = exe;
                         } else {
                             target = qEnvironmentVariable("APPIMAGE");  // the AppImage file, not its read-only mount
                             if (target.isEmpty()) {
                                 fail("Self-update works from the AppImage; download it from the GitHub release page");
                                 return;
                             }
                             QFile::remove(target + ".new");
                             if (!QFile::rename(tmp, target + ".new")) {
                                 QFile::copy(tmp, target + ".new");
                             }
                             QFile::setPermissions(target + ".new", QFile::permissions(target) | QFileDevice::ExeOwner);
                             QFile::remove(target);
                             if (!QFile::rename(target + ".new", target)) {
                                 fail("Couldn't replace the launcher");
                                 return;
                             }
                         }
                         QFile::remove(tmp);
                         QProcess::startDetached(target, {});
                         QCoreApplication::quit();
                     }});
    step_text_ = steps.front().label;
    run_steps(std::move(steps));
}

}  // namespace launcher
