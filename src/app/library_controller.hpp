#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

#include "core/library/library.hpp"
#include "core/library/scanner.hpp"
#include "core/library/work_tree.hpp"

namespace mp::app {

// Milestone 3a test controller, exposed to QML as the singleton `Library`.
// Bridges the Qt-free core::library::Library (database, watched folders, counts) and Scanner
// (own threads) to QML. Scanner callbacks arrive on scanner threads and are re-posted to the UI
// thread; all database access from here happens on the UI thread (the scanner has its own connections).
// The real library models (albums, artists, songs) come in milestone 4.
class LibraryController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Library)
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool indeterminate READ indeterminate NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY countsChanged)
    Q_PROPERTY(int albumCount READ albumCount NOTIFY countsChanged)
    Q_PROPERTY(int artistCount READ artistCount NOTIFY countsChanged)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY countsChanged)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY summaryChanged)
    Q_PROPERTY(QStringList problems READ problems NOTIFY summaryChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    // Milestone 3b: bumps whenever the database content changed (scan finished, folder removed), so QML
    // bindings that read `revision` re-run their queries.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString worksSummary READ worksSummary NOTIFY revisionChanged)

public:
    explicit LibraryController(QObject* parent = nullptr);
    ~LibraryController() override;

    bool available() const { return library_ != nullptr; }
    QString databasePath() const { return databasePath_; }
    QStringList folders() const { return folders_; }
    bool scanning() const { return scanning_; }
    bool indeterminate() const { return indeterminate_; }
    qreal progress() const { return progress_; }
    QString progressText() const { return progressText_; }
    int trackCount() const { return trackCount_; }
    int albumCount() const { return albumCount_; }
    int artistCount() const { return artistCount_; }
    int missingCount() const { return missingCount_; }
    QString summaryText() const { return summaryText_; }
    QStringList problems() const { return problems_; }
    QString errorText() const { return errorText_; }
    int revision() const { return revision_; }
    QString worksSummary() const { return worksSummary_; }

    // Adds a music folder (from a FolderDialog) and scans it.
    Q_INVOKABLE void addFolder(const QUrl& folder);
    // Stops watching a folder (path as listed in `folders`). Its tracks are kept, marked missing.
    Q_INVOKABLE void removeFolder(const QString& path);
    // Scans all watched folders (incremental).
    Q_INVOKABLE void scan();
    Q_INVOKABLE void cancelScan();

    // ---- Milestone 3b test queries (UI thread). Plain lists of maps; the real models come in milestone 4.
    // Albums with at least one existing track: id, title, artist, year, artHash ("" = no cover; user
    // override first), tracks, works, instruments.
    Q_INVOKABLE QVariantList albums();
    // Milestone 4: one album for the Album page: id, title, artist, year, artHash, tracks, durationMs, works,
    // instruments, plays, genre (most common), composers (most tracks first), format ("FLAC · 24-bit / 96 kHz").
    // Empty map if there is no such album.
    Q_INVOKABLE QVariantMap album(qlonglong albumId);
    // Tracks of one album in disc/track order: id, path, title, artist, movementNo, movementName,
    // workTitle, workId, durationMs, codec, discNo, trackNo, composer, catalog, plays, workRecordings (albums that
    // have this work), format. `path` and `title` are what Player.playTracks() wants.
    Q_INVOKABLE QVariantList albumTracks(qlonglong albumId);
    // Works with existing tracks: id, title, catalog, composer, recordings (albums), tracks.
    Q_INVOKABLE QVariantList works();
    // The same works as a tree (D38): composers [{name, workCount, families}], each family {label, catalogRange, works}
    // where works are maps as in works(). A family with an empty label holds one work standing alone.
    Q_INVOKABLE QVariantList worksByComposer();
    // All movements of a work over all recordings: albumId, albumTitle, albumArtist, trackId, movementNo,
    // movementName, title, path, albumArtHash, albumYear, workId.
    Q_INVOKABLE QVariantList workMovements(qlonglong workId);
    // The tracks of one recording of a work (work + album), in movement order, for Player.playTracks().
    Q_INVOKABLE QVariantList workTracks(qlonglong workId, qlonglong albumId);
    // Albums with composer-tagged tracks that got no work: albumId, albumTitle, albumArtist, tracks, composer,
    // sampleTitle. The "wrong or missed" list that milestone 7 (metadata fixes) works from.
    Q_INVOKABLE QVariantList undetectedClassical();

signals:
    void foldersChanged();
    void scanningChanged();
    void progressChanged();
    void countsChanged();
    void summaryChanged();
    void errorTextChanged();
    void revisionChanged();

private:
    void handleProgress(const core::library::ScanProgress& progress);
    void handleDone(const core::library::ScanSummary& summary);
    void refreshFolders();
    void refreshCounts();
    void refreshWorksSummary();
    QVariantList trackRows(const QString& where, qlonglong a, qlonglong b, const QString& orderBy);
    std::vector<core::library::WorkEntry> workEntries();
    void setError(const QString& text);

    QString databasePath_;
    QStringList folders_;
    bool scanning_ = false;
    bool indeterminate_ = false;
    qreal progress_ = 0;
    QString progressText_;
    int trackCount_ = 0;
    int albumCount_ = 0;
    int artistCount_ = 0;
    int missingCount_ = 0;
    QString summaryText_;
    QStringList problems_;
    QString errorText_;
    int revision_ = 0;
    QString worksSummary_;

    // Order matters: the scanner (its threads) is destroyed before the library it reads from.
    std::unique_ptr<core::library::Library> library_;
    std::unique_ptr<core::library::Scanner> scanner_;
};

}  // namespace mp::app
