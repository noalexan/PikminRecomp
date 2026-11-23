// Pikmin Recomp launcher. --screenshot PATH [--preview ready|building|live] renders one frame offscreen
// (QT_QPA_PLATFORM=offscreen) for checking the look without network or builds.
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "app/launcher.hpp"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Pikmin Recomp Launcher");
    QGuiApplication::setApplicationVersion(PIKMIN_LAUNCHER_VERSION);
    QQuickStyle::setStyle("Basic");  // our own look on top of the plain controls
    QGuiApplication::setWindowIcon(QIcon(":/assets/pikminrecomp-launcher-256.png"));

    QString shot, preview, auto_iso;
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--screenshot") shot = args[i + 1];
        if (args[i] == "--preview") preview = args[i + 1];
        if (args[i] == "--auto-build") auto_iso = args[i + 1];  // tests: check, build, quit
    }

    launcher::Launcher l;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("launcher", &l);
    engine.loadFromModule("PikminLauncher", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    if (!shot.isEmpty()) {
        // --preview live: the real checks (toolchain, disc, update), captured after they settle.
        const bool live = preview == "live";
        if (live) QTimer::singleShot(0, &l, &launcher::Launcher::refresh);
        else l.preview(preview.isEmpty() ? "ready" : preview);
        auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QTimer::singleShot(live ? 6000 : 1200, &app, [win, shot] {
            win->grabWindow().save(shot);
            QGuiApplication::quit();
        });
    } else if (!auto_iso.isEmpty()) {
        QTimer::singleShot(0, &l, [&l, auto_iso] {
            // The toolchain check runs on a worker thread: build once it's done.
            QObject::connect(&l, &launcher::Launcher::toolchainChecked, &l, [&l] {
                l.build();
                if (!l.property("busy").toBool()) QGuiApplication::exit(2);  // checks failed
                QObject::connect(&l, &launcher::Launcher::changed, &l, [&l] {
                    if (!l.property("busy").toBool())
                        QGuiApplication::exit(l.property("gameState").toString() == "built" ? 0 : 1);
                });
            }, Qt::SingleShotConnection);
            l.chooseIso(QUrl::fromLocalFile(auto_iso));
            l.refresh();
        });
    } else {
        QTimer::singleShot(0, &l, &launcher::Launcher::refresh);
    }
    return app.exec();
}
