#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QDebug>

#include "platform/window_style.hpp"
#include "player.hpp"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Music-Playeah");
    QGuiApplication::setOrganizationName("Music-Playeah");

    // Bundled Inter (OFL). Registered before QML loads so Theme.fontFamily resolves.
    for (const char* file : {"Inter-Regular", "Inter-Medium", "Inter-SemiBold", "Inter-Bold"}) {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/%1.ttf").arg(QLatin1StringView(file)));
        if (id < 0) {
            qWarning() << "Could not load font" << file;
        }
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("MusicPlayeah", "Main");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
        mp::app::platform::applyDarkTitleBar(window);
    }

    // Milestone 2 test aid: `music_playeah.exe "D:\Music\Some Album"` opens and plays that folder
    // (or a file, starting there). Handy as the debug command-line argument in VS / VS Code.
    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) {
        if (auto* player = engine.singletonInstance<mp::app::Player*>("MusicPlayeah", "Player")) {
            player->openPath(args.at(1));
        }
    }

    return app.exec();
}
