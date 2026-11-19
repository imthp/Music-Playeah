#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>
#include <vector>

#include "core/library/library.hpp"
#include "core/library/likes.hpp"
#include "core/library/scanner.hpp"
#include "core/library/search.hpp"
#include "core/library/search_load.hpp"
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
    // Milestone 4b: bumps whenever a like is added or removed (or the likes were reloaded after a scan), so QML
    // bindings that read `likesRevision` re-ask isLiked(). See LikeState.qml.
    Q_PROPERTY(int likesRevision READ likesRevision NOTIFY likesChanged)

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
    int likesRevision() const { return likesRevision_; }

    // Adds a music folder (from a FolderDialog) and scans it.
    Q_INVOKABLE void addFolder(const QUrl& folder);
    // Stops watching a folder (path as listed in `folders`). Its tracks are kept, marked missing.
    Q_INVOKABLE void removeFolder(const QString& path);
    // Scans all watched folders (incremental).
    Q_INVOKABLE void scan();
    Q_INVOKABLE void cancelScan();

    // ---- Milestone 3b test queries (UI thread). Plain lists of maps; the real models come in milestone 4.
    // Albums with at least one existing track: id, title, artist, year, artHash ("" = no cover; user
    // override first), tracks, works, instruments, hiRes (any track 24-bit+, above 48 kHz or DSD),
    // composers (newline-separated).
    Q_INVOKABLE QVariantList albums();
    // Milestone 4: one album for the Album page: id, title, artist, year, artHash, tracks, durationMs, works,
    // instruments, plays, genre (most common), composers (most tracks first), format ("FLAC · 24-bit / 96 kHz").
    // Empty map if there is no such album.
    Q_INVOKABLE QVariantMap album(qlonglong albumId);
    // Every cover hash of albums with existing tracks (override first), in album sort order: what the art
    // provider pre-makes thumbnails for after a scan.
    QStringList artHashes();
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

    // ---- Milestone 4b (library_controller_browse.cpp) -------------------------------------------------------
    // Likes (SPEC §8b3, D42). `kind` is "track", "album", "artist", "composer" or "work"; `id` the matching row id
    // (for a composer: `person().id`, D49). The heart changes at once (optimistic) and the write follows; if the
    // write fails the heart goes back and errorText says why.
    Q_INVOKABLE bool isLiked(const QString& kind, qlonglong id) const;
    Q_INVOKABLE bool setLiked(const QString& kind, qlonglong id, bool liked);  // returns the state afterwards
    Q_INVOKABLE bool toggleLike(const QString& kind, qlonglong id);            // returns the state afterwards
    // How many items of each kind are liked (existing items only): {track, album, artist, composer, work}.
    Q_INVOKABLE QVariantMap likedCounts();
    // The liked items of one kind, newest like first, each with `likedAt` (Unix seconds).
    // track: maps as albumTracks() · album: maps as albums() · artist: {id, name, albums, tracks} ·
    // composer: {id, name, works, recordings} · work: maps as works().
    Q_INVOKABLE QVariantList liked(const QString& kind);

    // Tracks by id, in the order given (for playing search results). Maps as albumTracks().
    Q_INVOKABLE QVariantList tracksByIds(const QVariantList& ids);
    // Whole albums back to back, in the order given (Liked "Play all"). One query.
    Q_INVOKABLE QVariantList tracksOfAlbums(const QVariantList& albumIds);
    // For each work, in the order given, its movements from the first recording you own (album order, as
    // workRecordings()). One query (Composer "Play works", Liked works).
    Q_INVOKABLE QVariantList tracksOfWorks(const QVariantList& workIds);

    // A person page's header. kind "artist": {kind, id (0 = unknown name), name, albums (led), appearances, tracks,
    // plays}. kind "composer": {kind, id, name (longest spelling), works, recordings, albums}. `name` may be any
    // spelling the tags use (D49). An unknown name gives id 0.
    Q_INVOKABLE QVariantMap person(const QString& kind, const QString& name);
    // Albums the artist leads (album artist) / appears on (performs on a track of an album led by someone else).
    Q_INVOKABLE QVariantList artistAlbums(qlonglong artistId, bool appearsOn);
    // The artist's tracks, most played first (then album order); limit 0 = all. Maps as albumTracks().
    Q_INVOKABLE QVariantList artistTracks(qlonglong artistId, int limit);
    // A composer's works as families (D38): [{label, catalogRange, works: [{id, title, catalog, recordings, tracks,
    // form, instrument}]}], in the order of Browse → Works. `form` / `instrument` ("suite", "cello"; "" = unknown)
    // feed the page's filter chips.
    Q_INVOKABLE QVariantList composerWorks(const QString& name);
    // Albums with tracks tagged with this composer: the fallback for a composer without detected works.
    Q_INVOKABLE QVariantList composerAlbums(const QString& name);
    // Every recording (album) of a work you own: albumId, albumTitle, albumArtist, artHash, year, durationMs, tracks.
    // Albums holding a work: {albumId, albumTitle, albumArtist, artHash, year, durationMs, tracks, plays, lastPlayed
    // (Unix seconds, 0 = never), instruments ("guitar, lute")}, in album order.
    Q_INVOKABLE QVariantList workRecordings(qlonglong workId);
    // One work for the Work page: {id, title, catalog, composer, recordings, tracks}; empty map if it has no tracks.
    Q_INVOKABLE QVariantMap work(qlonglong workId);

    // Search (SPEC §10). `kind`: "" = all kinds, 6 hits each; "song", "album", "artist", "composer" or "work" = that
    // kind only, up to 100 hits. Result: {query, top (map or null), song, album, artist, composer, work (lists of
    // {kind, id, title, subtitle, detail, albumId, artHash, durationMs, count1, count2}), totals {kind: matches}}.
    // The index is built from the database on the first search after a change; a search takes a few milliseconds.
    Q_INVOKABLE QVariantMap search(const QString& query, const QString& kind = QString());

signals:
    void foldersChanged();
    void scanningChanged();
    void progressChanged();
    void countsChanged();
    void summaryChanged();
    void errorTextChanged();
    void revisionChanged();
    void likesChanged();

private:
    void handleProgress(const core::library::ScanProgress& progress);
    void handleDone(const core::library::ScanSummary& summary);
    void refreshFolders();
    void refreshCounts();
    void refreshWorksSummary();
    QVariantList trackRows(const QString& where, qlonglong a, qlonglong b, const QString& orderBy, int limit = 0);
    QVariantList albumRows(const QString& where, qlonglong a);
    // 4b
    void reloadLikes();
    static qint64 likeKey(core::library::LikeKind kind, qlonglong id) { return id * 8 + static_cast<int>(kind); }
    std::optional<std::int64_t> artistIdByName(const QString& name);
    // The composer (D49) whose spelling group contains this artists row, or nullptr.
    const core::library::ComposerSummary* composerOfArtist(const std::vector<core::library::ComposerSummary>& all, std::int64_t artistId) const;
    QVariantList worksWhere(const QString& where, const QString& orderBy);
    const core::library::SearchIndex& searchIndex();
    std::vector<core::library::WorkEntry> workEntries(const std::string& where = {});
    // Composers as people (D49), loaded once per library revision: person(), composerWorks(), likes and counts all
    // need them, and loading groups every work.
    const std::vector<core::library::ComposerSummary>& composers();
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
    int likesRevision_ = 0;
    QHash<qint64, qint64> likes_;  // likeKey(kind, id) -> liked_at; the whole table, it stays small
    std::unique_ptr<core::library::SearchIndex> searchIndex_;
    int searchIndexRevision_ = -1;  // `revision_` the index was built at
    std::vector<core::library::ComposerSummary> composers_;
    int composersRevision_ = -1;

    // Order matters: the scanner (its threads) is destroyed before the library it reads from.
    std::unique_ptr<core::library::Library> library_;
    std::unique_ptr<core::library::Scanner> scanner_;
    std::unique_ptr<core::library::Likes> likesStore_;  // after library_: uses its connection
};

}  // namespace mp::app
