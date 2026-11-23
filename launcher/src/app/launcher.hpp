// The launcher's state machine, exposed to QML: update check / self-update, toolchain and ROM
// checks, the game build (download source -> extract disc -> configure -> build -> install -> swap)
// and Play.
#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <vector>

namespace launcher {

class Launcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString latestVersion MEMBER latest_ NOTIFY changed)
    Q_PROPERTY(bool updateAvailable MEMBER update_available_ NOTIFY changed)
    Q_PROPERTY(QString updateText MEMBER update_text_ NOTIFY changed)
    Q_PROPERTY(bool toolchainOk MEMBER toolchain_ok_ NOTIFY changed)
    Q_PROPERTY(QString toolchainText MEMBER toolchain_text_ NOTIFY changed)
    Q_PROPERTY(QString toolchainHint MEMBER toolchain_hint_ NOTIFY changed)
    Q_PROPERTY(bool romOk MEMBER rom_ok_ NOTIFY changed)
    Q_PROPERTY(QString romText MEMBER rom_text_ NOTIFY changed)
    Q_PROPERTY(QString isoPath MEMBER iso_ NOTIFY changed)
    Q_PROPERTY(QString gameState MEMBER game_state_ NOTIFY changed)  // none | built | outdated
    Q_PROPERTY(QString gameText MEMBER game_text_ NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy_ NOTIFY changed)
    Q_PROPERTY(double progress MEMBER progress_ NOTIFY changed)
    Q_PROPERTY(QString stepText MEMBER step_text_ NOTIFY changed)
    Q_PROPERTY(QString log MEMBER log_ NOTIFY logChanged)

public:
    explicit Launcher(QObject* parent = nullptr);
    QString version() const { return QStringLiteral(PIKMIN_LAUNCHER_VERSION); }

    Q_INVOKABLE void refresh();             // toolchain + ROM + game state + update check
    Q_INVOKABLE void chooseIso(const QUrl& url);
    Q_INVOKABLE void build();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void play();
    Q_INVOKABLE void update();
    Q_INVOKABLE void copyToClipboard(const QString& text);

    // Screenshot / preview mode: fixed sample state, no checks.
    void preview(const QString& state);

signals:
    void changed();
    void logChanged();  // separate: the log changes per output line, the rest rarely
    void toolchainChecked();  // the (asynchronous) toolchain check finished

private:
    struct Step {
        QString label;
        double weight;  // share of the progress bar
        std::function<void()> run;  // must end with next() or fail()
    };
    void load_state();
    void save_state();
    void check_toolchain();
    void check_rom();
    void check_game();
    void check_update();
    void append_log(const QString& text);
    // Coalesces UI updates (progress, log) to ~10 per second: a build prints thousands of lines,
    // and re-evaluating every binding per line froze the window.
    void notify();
    void notify_log();
    // Runs work on a worker thread, then done on the UI thread (slow file or process work).
    void in_background(std::function<void()> work, std::function<void()> done);
    void run_steps(std::vector<Step> steps);
    void next();
    void fail(const QString& why);
    void run_process(const QString& program, const QStringList& args, const QString& cwd);
    void download(const QUrl& url, const QString& to, std::function<void()> done);
    void install_update(const QString& tmp);  // replace the launcher with the verified download
    QString source_dir() const;
    QString game_exe() const;
    QStringList tool_paths() const;
    QString python_exe() const;
    QProcessEnvironment build_env() const;

    QNetworkAccessManager net_;
    QTimer ui_timer_;
    bool pending_changed_ = false, pending_log_ = false, checking_toolchain_ = false;
    QProcess* proc_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    std::vector<Step> steps_;
    size_t step_ = 0;
    double done_weight_ = 0, total_weight_ = 1;
    bool cancelled_ = false;
    QString built_version_, release_url_, release_sha_, release_name_;
    qint64 release_size_ = 0;

    QString latest_, update_text_ = "Checking for updates…";
    bool update_available_ = false;
    bool toolchain_ok_ = false, rom_ok_ = false, busy_ = false;
    QString toolchain_text_, toolchain_hint_, rom_text_ = "Choose your Pikmin disc image", iso_;
    QString game_state_ = "none", game_text_ = "Not built yet";
    double progress_ = 0;
    QString step_text_, log_;
};

}  // namespace launcher
