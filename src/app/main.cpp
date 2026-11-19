#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDir>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>

#include "art_provider.hpp"
#include "core/text.hpp"
#include "platform/window_style.hpp"
#include "library_controller.hpp"
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
    // Album covers: image://art/<hash>/<size> (milestone 3b). Same database file as LibraryController uses.
    mp::app::ArtProvider* art = nullptr;  // owned by the engine
    {
        const QString dbFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                   .filePath(QStringLiteral("library.db"));
        art = new mp::app::ArtProvider(mp::core::pathFromUtf8(dbFile.toUtf8().toStdString()),
                                       mp::app::ArtProvider::defaultCacheFolder());
        engine.addImageProvider(QStringLiteral("art"), art);
    }
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

    // Thumbnails in the background (milestone 4a): a few seconds after start (the window and its first covers
    // come first), and again whenever a scan or a folder change bumps the library's revision. Covers that already
    // have their files are skipped, so after the first run this costs one file check per album.
    if (auto* library = engine.singletonInstance<mp::app::LibraryController*>("MusicPlayeah", "Library")) {
        auto warm = [art, library] { art->prewarm(library->artHashes()); };
        QTimer::singleShot(3000, library, warm);
        QObject::connect(library, &mp::app::LibraryController::revisionChanged, library, warm);
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
