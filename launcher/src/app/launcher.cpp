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
#include <QStandardPaths>
#include <QThread>
#include <memory>

#include "core/paths.hpp"
#include "core/progress.hpp"
#include "core/release.hpp"
#include "core/rom.hpp"
#include "core/toolchain.hpp"
#include "core/update.hpp"
#include "core/version.hpp"

namespace launcher {
namespace {
// The release list, not /releases/latest: that one skips prereleases.
constexpr const char* kReleasesApi = "https://api.github.com/repos/noalexan/PikminRecomp/releases?per_page=20";
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
    // A previous self-update on Windows leaves the replaced files behind as *.old.
    if (kWindows) remove_old_files(QCoreApplication::applicationDirPath());
    load_state();
    ui_timer_.setSingleShot(true);
    ui_timer_.setInterval(100);
    connect(&ui_timer_, &QTimer::timeout, this, [this] {
        if (pending_changed_) emit changed();
        if (pending_log_) emit logChanged();
        pending_changed_ = pending_log_ = false;
    });
}

void Launcher::notify() {
    pending_changed_ = true;
    if (!ui_timer_.isActive()) ui_timer_.start();
}

void Launcher::notify_log() {
    pending_log_ = true;
    if (!ui_timer_.isActive()) ui_timer_.start();
}

void Launcher::in_background(std::function<void()> work, std::function<void()> done) {
    QThread* t = QThread::create(std::move(work));
    // Context object `this`: the lambda runs on the UI thread once the worker is done.
    connect(t, &QThread::finished, this, [t, done = std::move(done)] {
        t->deleteLater();
        done();
    });
    t->start();
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

QString Launcher::python_exe() const { return kWindows ? "python" : "python3"; }

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
    if (checking_toolchain_) return;
    checking_toolchain_ = true;
    if (toolchain_text_.isEmpty()) toolchain_text_ = "Checking…";
    emit changed();
    // Each probe runs `<tool> --version` (slow on Windows): off the UI thread.
    struct Result {
        QStringList ok, missing;
    };
    auto res = std::make_shared<Result>();
    const QStringList paths = tool_paths();
    in_background(
        [needs, res, paths] {
            for (const Need& n : needs) {
                const Tool t = probe_tool(n.program, n.min, paths);
                if (!t.found) res->missing << n.program;
                else if (t.tooOld) res->missing << n.program + " " + t.version + " (needs " + n.min + "+)";
                else res->ok << n.program + " " + t.version;
            }
        },
        [this, res] {
            checking_toolchain_ = false;
            toolchain_ok_ = res->missing.isEmpty();
            toolchain_text_ = toolchain_ok_ ? res->ok.join(" · ") : "Missing: " + res->missing.join(", ");
            toolchain_hint_ = toolchain_ok_ ? QString() : install_hint(detect_os());
            if (!toolchain_ok_ && kWindows && !QFileInfo::exists(tool_paths().first()))
                toolchain_text_ = "MSYS2 isn't installed (https://www.msys2.org). Install it, then run in its UCRT64 shell:";
            emit changed();
            emit toolchainChecked();
        });
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
    QNetworkRequest req{QUrl(kReleasesApi)};
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
    if (log_.size() > 96000) log_ = log_.right(64000);  // the tail is what matters; layout cost grows with size
    notify_log();
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
    emit logChanged();
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
        notify();
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
    // Resolve in the toolchain folders first (MSYS2 isn't necessarily on the launcher's own PATH).
    QString exe = QStandardPaths::findExecutable(program, tool_paths());
    if (exe.isEmpty()) exe = QStandardPaths::findExecutable(program);
    if (exe.isEmpty()) exe = program;
    append_log("$ " + exe + " " + args.join(' ') + "\n");
    proc_->start(exe, args);
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
        notify();
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
    const QString python = python_exe();
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
                             download(QUrl(source_archive_url(ver, false)), data("src.tar.gz"), [this] { next(); });
                         }});
        steps.push_back({"Unpacking the source", 2, [this, ver] {
                             QFile v(data("src/VERSION.launcher"));
                             if (v.open(QIODevice::ReadOnly) && QString::fromUtf8(v.readAll()).trimmed() == ver) {
                                 next();
                                 return;
                             }
                             // Python (needed for the build anyway) unpacks the same way everywhere; MSYS2's
                             // GNU tar would read "C:/..." as a remote host.
                             static const char* kUnpack =
                                 "import sys, tarfile\n"
                                 "src, dst = sys.argv[1], sys.argv[2]\n"
                                 "with tarfile.open(src) as t:\n"
                                 "    for m in t.getmembers():\n"
                                 "        parts = m.name.split('/', 1)\n"
                                 "        if len(parts) < 2 or not parts[1]: continue\n"
                                 "        m.name = parts[1]\n"
                                 "        try: t.extract(m, dst, filter='data')\n"
                                 "        except TypeError: t.extract(m, dst)\n";
                             in_background([] { remove_tree(data("src-new")); },
                                           [this] {
                                               if (cancelled_) return;
                                               QDir().mkpath(data("src-new"));
                                               run_process(python_exe(), {"-c", kUnpack, data("src.tar.gz"), data("src-new")},
                                                           data_dir());
                                           });
                         }});
        steps.push_back({"Installing the source", 1, [this, ver] {
                             if (!QFileInfo::exists(data("src-new"))) {
                                 next();
                                 return;
                             }
                             // Keep the extracted disc (orig/) across versions.
                             if (QFileInfo::exists(data("src/orig"))) QDir().rename(data("src/orig"), data("src-new/orig"));
                             in_background([] { remove_tree(data("src")); }, [this, ver] {
                                 if (cancelled_) return;
                                 if (!QDir().rename(data("src-new"), data("src"))) {
                                     fail("Can't replace the source folder");
                                     return;
                                 }
                                 QFile v(data("src/VERSION.launcher"));
                                 if (v.open(QIODevice::WriteOnly)) v.write(ver.toUtf8());
                                 next();
                             });
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
                         in_background([stage] { remove_tree(stage); }, [this, build, src] {
                             if (!cancelled_) run_process("cmake", {"--install", build}, src);
                         });
                     }});
    steps.push_back({"Finishing", 1, [this, stage] {
                         // The old game stays until the new one is complete.
                         in_background([] { remove_tree(data("game-old")); }, [this, stage] {
                             if (cancelled_) return;
                             if (QFileInfo::exists(data("game")) && !QDir().rename(data("game"), data("game-old"))) {
                                 fail("Can't replace the installed game (is it running?)");
                                 return;
                             }
                             if (!QDir().rename(stage, data("game"))) {
                                 QDir().rename(data("game-old"), data("game"));
                                 fail("Can't install the new game");
                                 return;
                             }
                             built_version_ = version();
                             save_state();
                             check_game();
                             in_background([] { remove_tree(data("game-old")); }, [this] { next(); });
                         });
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
                         // Size and SHA-256 off the UI thread (the download is tens of MB).
                         auto bad = std::make_shared<QString>();
                         in_background(
                             [tmp, bad, size = release_size_, sha = release_sha_] {
                                 QFile f(tmp);
                                 if (!f.open(QIODevice::ReadOnly) || (size && f.size() != size)) {
                                     *bad = "The download is incomplete";
                                     return;
                                 }
                                 QCryptographicHash h(QCryptographicHash::Sha256);
                                 h.addData(&f);
                                 if (!sha.isEmpty() && h.result().toHex() != sha.toLatin1())
                                     *bad = "The download doesn't match its checksum";
                             },
                             [this, tmp, bad] {
                                 if (cancelled_) return;
                                 if (!bad->isEmpty()) fail(*bad);
                                 else install_update(tmp);
                             });
                     }});
    step_text_ = steps.front().label;
    run_steps(std::move(steps));
}

void Launcher::install_update(const QString& tmp) {
    QString target;
    if (kWindows) {
        // The running launcher's .exe, DLLs and plugins can't be overwritten (only renamed): unpack
        // into a staging folder, then swap each file in, the replaced ones renamed to *.old.
        const QString dir = QCoreApplication::applicationDirPath(), exe = QCoreApplication::applicationFilePath();
        const QString staging = data("launcher-update");
        remove_tree(staging);
        QDir().mkpath(staging);
        // Windows' own bsdtar (reads zip), not MSYS2's GNU tar; asynchronous, output to the log.
        auto* tar = new QProcess(this);
        tar->setProcessChannelMode(QProcess::MergedChannels);
        connect(tar, &QProcess::finished, this, [this, tar, tmp, exe, dir, staging](int code, QProcess::ExitStatus st) {
            append_log(QString::fromLocal8Bit(tar->readAll()));
            tar->deleteLater();
            if (st != QProcess::NormalExit || code != 0) {
                fail("Couldn't unpack the update (tar exit " + QString::number(code) + ")");
                return;
            }
            auto err = std::make_shared<QString>();
            in_background([staging, dir, err] { swap_in_tree(staging, dir, err.get()); },
                          [this, tmp, exe, staging, err] {
                              remove_tree(staging);
                              if (!err->isEmpty()) {
                                  fail("Couldn't install the update: " + *err);
                                  return;
                              }
                              QFile::remove(tmp);
                              QProcess::startDetached(exe, {});
                              QCoreApplication::quit();
                          });
        });
        connect(tar, &QProcess::errorOccurred, this, [this, tar](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;
            tar->deleteLater();
            fail("Couldn't start tar.exe to unpack the update");
        });
        const QString tar_exe = qEnvironmentVariable("SystemRoot", "C:/Windows") + "/System32/tar.exe";
        append_log("$ " + tar_exe + " -xf " + tmp + " -C " + staging + " --strip-components=1\n");
        tar->start(tar_exe, {"-xf", tmp, "-C", staging, "--strip-components=1"});
        return;
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
}

}  // namespace launcher
