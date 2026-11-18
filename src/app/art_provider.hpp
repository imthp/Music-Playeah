#pragma once

#include <QQuickAsyncImageProvider>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <filesystem>

namespace mp::app {

// QML image provider for album covers (D9, SPEC §6): `image://art/<hash>/<size>`, size 256, 600 or 0 (full).
// <hash> is albums.art_hash. A request first looks for the thumbnail `art/<hash>_<size>.jpg` in the app data
// folder; if it isn't there, the cover is read from its source (embedded picture or cover file, found through
// the albums table), scaled once and cached. All of that runs on a small thread pool, never on the UI thread,
// so a scrolling grid doesn't stutter. Full size is never cached (it is the original, read on demand).
class ArtProvider : public QQuickAsyncImageProvider {
public:
    // `databaseFile`: the library database (read only here); `cacheFolder`: where thumbnails live.
    ArtProvider(const std::filesystem::path& databaseFile, const QString& cacheFolder);
    ~ArtProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    // `art/` under the app data folder (QStandardPaths::AppLocalDataLocation).
    static QString defaultCacheFolder();

private:
    std::filesystem::path databaseFile_;
    QString cacheFolder_;
    QThreadPool pool_;
    std::atomic<int> nextPriority_{0};  // each request outranks the ones before it (newest first)
};

}  // namespace mp::app
