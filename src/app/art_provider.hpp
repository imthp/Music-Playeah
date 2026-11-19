#pragma once

#include <QQuickAsyncImageProvider>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <filesystem>
#include <memory>

namespace mp::app {

// QML image provider for album covers (D9, SPEC §6): `image://art/<hash>/<size>`, size 256, 600, 1200 (Now Playing)
// or 0 (full). <hash> is albums.art_hash.
//
// Where a cover comes from, fastest first:
//  1. memory: the last ~200 MB of thumbnails decoded (Qt's own pixmap cache keeps only ~10 MB, about 40 grid
//     covers, so scrolling back used to decode them again from disk);
//  2. disk: the thumbnail `art/<hash>_<size>.jpg` in the app data folder;
//  3. the source (embedded picture or cover file, found through the albums table), decoded at reduced size,
//     then written to disk. `prewarm()` does this in the background after a scan, so the grid rarely waits on it.
// Requests run on a small thread pool, newest first, never on the UI thread. Full size is never cached.
class ArtProvider : public QQuickAsyncImageProvider {
public:
    // `databaseFile`: the library database (read only here); `cacheFolder`: where thumbnails live.
    ArtProvider(const std::filesystem::path& databaseFile, const QString& cacheFolder);
    ~ArtProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    // Makes the missing 256 and 600 px thumbnail files for these art hashes, in the background on two
    // low-priority threads (one source decode per cover for both sizes). Calling it again replaces the queue.
    void prewarm(const QStringList& hashes);

    // `art/` under the app data folder (QStandardPaths::AppLocalDataLocation).
    static QString defaultCacheFolder();

    struct Shared;  // cache, paths, stop flag: shared with the running jobs

private:
    std::shared_ptr<Shared> shared_;
    QThreadPool pool_;         // requests from QML
    QThreadPool prewarmPool_;  // background thumbnail generation
    std::atomic<int> nextPriority_{0};  // each request outranks the ones before it (newest first)
};

}  // namespace mp::app
