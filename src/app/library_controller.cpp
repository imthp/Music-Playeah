#include "library_controller.hpp"

#include <QDir>
#include <QLocale>
#include <QMetaObject>
#include <QVariantMap>
#include <QStandardPaths>

#include <exception>
#include <initializer_list>

#include "core/library/work_tree.hpp"
#include "core/text.hpp"

namespace mp::app {

namespace {

std::filesystem::path toPath(const QString& text) { return core::pathFromUtf8(text.toUtf8().toStdString()); }

QString fromUtf8(const std::string& text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }

QString number(std::uint64_t n) { return QLocale().toString(static_cast<qulonglong>(n)); }

QString seconds(std::int64_t ms) { return QLocale().toString(static_cast<double>(ms) / 1000.0, 'f', 1) + QStringLiteral(" s"); }

}  // namespace

LibraryController::LibraryController(QObject* parent) : QObject(parent) {
    // %LOCALAPPDATA% on Windows (SPEC §6); never a hard-coded path (D22).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString file = QDir(dir).filePath(QStringLiteral("library.db"));
    databasePath_ = QDir::toNativeSeparators(file);
    try {
        library_ = std::make_unique<core::library::Library>(toPath(file));
        scanner_ = std::make_unique<core::library::Scanner>(library_->dbFile());
        refreshFolders();
        refreshCounts();
        // After a database upgrade (or an interrupted scan) some tracks still wait to be grouped into works.
        // A scan does that: unchanged files are only compared, so it is quick; no need to ask the user (D40).
        if (!folders_.isEmpty() && library_->db().scalarInt("SELECT EXISTS(SELECT 1 FROM tracks WHERE works_done = 0)") != 0) {
            QMetaObject::invokeMethod(this, &LibraryController::scan, Qt::QueuedConnection);
        }
    } catch (const std::exception& e) {
        library_.reset();
        errorText_ = tr("Could not open the library database: %1").arg(QString::fromUtf8(e.what()));
    }
}

LibraryController::~LibraryController() = default;

void LibraryController::setError(const QString& text) {
    if (errorText_ != text) {
        errorText_ = text;
        emit errorTextChanged();
    }
}

void LibraryController::refreshFolders() {
    folders_.clear();
    for (const auto& folder : library_->folders()) {
        folders_ << QDir::toNativeSeparators(fromUtf8(folder.path));
    }
    emit foldersChanged();
}

void LibraryController::refreshCounts() {
    const auto counts = library_->counts();
    trackCount_ = static_cast<int>(counts.tracks);
    albumCount_ = static_cast<int>(counts.albums);
    artistCount_ = static_cast<int>(counts.artists);
    missingCount_ = static_cast<int>(counts.missing);
    emit countsChanged();
    refreshWorksSummary();
}

void LibraryController::refreshWorksSummary() {
    auto& db = library_->db();
    const auto works = db.scalarInt("SELECT COUNT(DISTINCT work_id) FROM tracks WHERE work_id IS NOT NULL AND missing_since IS NULL");
    const auto inWorks = db.scalarInt("SELECT COUNT(*) FROM tracks WHERE work_id IS NOT NULL AND missing_since IS NULL");
    const auto missed = db.scalarInt(
        "SELECT COUNT(*) FROM v_tracks WHERE work_id IS NULL AND composer <> '' AND missing_since IS NULL");
    const auto single = db.scalarInt(
        "SELECT COUNT(*) FROM (SELECT work_id FROM tracks WHERE work_id IS NOT NULL AND missing_since IS NULL "
        "GROUP BY work_id, album_id HAVING COUNT(*) = 1)");
    const auto covers = db.scalarInt(
        "SELECT COUNT(*) FROM albums a WHERE a.art_hash IS NOT NULL AND EXISTS "
        "(SELECT 1 FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL)");
    worksSummary_ = tr("%1 works · %2 tracks in works · %3 one-movement recordings · %4 composer-tagged tracks without a work · %5 albums with a cover")
                        .arg(number(static_cast<std::uint64_t>(works)), number(static_cast<std::uint64_t>(inWorks)),
                             number(static_cast<std::uint64_t>(single)), number(static_cast<std::uint64_t>(missed)),
                             number(static_cast<std::uint64_t>(covers)));
    ++revision_;
    emit revisionChanged();
}

void LibraryController::addFolder(const QUrl& folder) {
    if (!library_) {
        return;
    }
    if (scanning_) {
        setError(tr("Wait for the running scan to finish first."));
        return;
    }
    try {
        const auto result = library_->addFolder(toPath(folder.toLocalFile()));
        if (!result.ok) {
            setError(fromUtf8(result.error));
            return;
        }
        setError({});
        refreshFolders();
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
        return;
    }
    scan();
}

void LibraryController::removeFolder(const QString& path) {
    if (!library_) {
        return;
    }
    if (scanning_) {
        setError(tr("Wait for the running scan to finish first."));
        return;
    }
    try {
        library_->removeFolder(QDir::fromNativeSeparators(path).toUtf8().toStdString());  // stored with '/' separators
        setError({});
        refreshFolders();
        refreshCounts();
        // After a database upgrade (or an interrupted scan) some tracks still wait to be grouped into works.
        // A scan does that: unchanged files are only compared, so it is quick; no need to ask the user (D40).
        if (!folders_.isEmpty() && library_->db().scalarInt("SELECT EXISTS(SELECT 1 FROM tracks WHERE works_done = 0)") != 0) {
            QMetaObject::invokeMethod(this, &LibraryController::scan, Qt::QueuedConnection);
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
}

void LibraryController::scan() {
    if (!library_ || scanning_) {
        return;
    }
    if (folders_.isEmpty()) {
        setError(tr("Add a music folder first."));
        return;
    }
    setError({});
    scanning_ = true;
    indeterminate_ = true;
    progress_ = 0;
    progressText_ = tr("Starting…");
    summaryText_.clear();
    problems_.clear();
    emit scanningChanged();
    emit progressChanged();
    emit summaryChanged();

    // Both callbacks run on scanner threads. The queued call is dropped if `this` is gone.
    const bool started = scanner_->start(
        [this](const core::library::ScanProgress& p) {
            QMetaObject::invokeMethod(this, [this, p] { handleProgress(p); }, Qt::QueuedConnection);
        },
        [this](const core::library::ScanSummary& s) {
            QMetaObject::invokeMethod(this, [this, s] { handleDone(s); }, Qt::QueuedConnection);
        });
    if (!started) {
        scanning_ = false;
        emit scanningChanged();
    }
}

void LibraryController::cancelScan() {
    if (scanner_ && scanning_) {
        scanner_->cancel();
        progressText_ = tr("Cancelling…");
        emit progressChanged();
    }
}

void LibraryController::handleProgress(const core::library::ScanProgress& p) {
    if (!scanning_) {
        return;  // a late update after the scan finished
    }
    using Phase = core::library::ScanProgress::Phase;
    if (p.phase == Phase::Finishing) {
        indeterminate_ = true;
        progressText_ = tr("Finishing: albums, works and covers…");
    } else if (!p.walkFinished) {
        // Still walking the folders: the total isn't known yet.
        indeterminate_ = true;
        progressText_ = tr("%1: %2 files found, %3 handled")
                            .arg(QDir::toNativeSeparators(fromUtf8(p.folder)), number(p.filesFound), number(p.filesDone));
    } else {
        indeterminate_ = false;
        progress_ = p.filesFound > 0 ? static_cast<qreal>(p.filesDone) / static_cast<qreal>(p.filesFound) : 1.0;
        progressText_ = tr("%1 of %2 files").arg(number(p.filesDone), number(p.filesFound));
    }
    emit progressChanged();
}

void LibraryController::handleDone(const core::library::ScanSummary& s) {
    scanning_ = false;
    indeterminate_ = false;
    progress_ = s.cancelled ? progress_ : 1.0;
    progressText_.clear();
    refreshCounts();

    const QString headline = s.cancelled ? tr("Scan cancelled after %1.").arg(seconds(s.elapsedMs))
                                         : tr("Scan finished in %1.").arg(seconds(s.elapsedMs));
    summaryText_ = tr("%1 %2 files: %3 new, %4 updated, %5 unchanged, %6 restored, %7 moved, %8 missing, %9 failed.")
                       .arg(headline, number(s.filesSeen), number(s.added), number(s.updated), number(s.unchanged),
                            number(s.restored), number(s.moved), number(s.markedMissing), number(s.failed));

    problems_.clear();
    if (!s.fatalError.empty()) {
        problems_ << tr("Scan stopped: %1").arg(fromUtf8(s.fatalError));
    }
    for (const std::string& folder : s.unavailableFolders) {
        problems_ << tr("Folder not available (unplugged or renamed?): %1")
                         .arg(QDir::toNativeSeparators(fromUtf8(folder)));
    }
    for (const std::string& error : s.errors) {
        problems_ << fromUtf8(error);
    }
    emit scanningChanged();
    emit progressChanged();
    emit summaryChanged();
}

// ------------------------------------------------------------------ milestone 3b test queries

namespace {

QVariantMap map(std::initializer_list<std::pair<const char*, QVariant>> items) {
    QVariantMap m;
    for (const auto& [key, value] : items) {
        m.insert(QString::fromLatin1(key), value);
    }
    return m;
}

}  // namespace

QVariantList LibraryController::albums() {
    QVariantList out;
    if (!library_) {
        return out;
    }
    try {
        auto s = library_->db().prepare(
            "SELECT a.id, a.title, a.album_artist, a.year, COALESCE(o.image_hash, a.art_hash, ''), "
            "(SELECT COUNT(*) FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL), "
            "(SELECT COUNT(DISTINCT t.work_id) FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL AND t.work_id IS NOT NULL), "
            "COALESCE((SELECT group_concat(g.tag, ', ') FROM album_tags g WHERE g.album_id = a.id AND g.kind = 'instrument'), '') "
            "FROM albums a LEFT JOIN image_overrides o ON o.kind = 'album' AND o.ref_id = a.id "
            "WHERE EXISTS (SELECT 1 FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL) "
            "ORDER BY a.sort_key");
        while (s.step()) {
            out << map({{"id", static_cast<qlonglong>(s.getInt(0))},
                        {"title", fromUtf8(s.getText(1))},
                        {"artist", fromUtf8(s.getText(2))},
                        {"year", static_cast<int>(s.getInt(3))},
                        {"artHash", fromUtf8(s.getText(4))},
                        {"tracks", static_cast<int>(s.getInt(5))},
                        {"works", static_cast<int>(s.getInt(6))},
                        {"instruments", fromUtf8(s.getText(7))}});
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

// Shared by albumTracks / workTracks / workMovements: `where` uses ?1 and ?2.
QVariantList LibraryController::trackRows(const QString& where, qlonglong a, qlonglong b, const QString& orderBy) {
    QVariantList out;
    if (!library_) {
        return out;
    }
    try {
        std::string sql =
            "SELECT t.id, t.path, t.title, t.artist, t.movement_no, t.movement_name, COALESCE(w.title, ''), "
            "t.duration_ms, t.codec, t.album_id, al.title, al.album_artist, COALESCE(o.image_hash, al.art_hash, ''), "
            "al.year, COALESCE(t.work_id, 0) "
            "FROM v_tracks t LEFT JOIN works w ON w.id = t.work_id LEFT JOIN albums al ON al.id = t.album_id "
            "LEFT JOIN image_overrides o ON o.kind = 'album' AND o.ref_id = t.album_id "
            "WHERE t.missing_since IS NULL AND ";
        sql += where.toStdString();
        sql += " ORDER BY ";
        sql += orderBy.toStdString();
        auto s = library_->db().prepare(sql);
        s.bind(1, static_cast<std::int64_t>(a));
        s.bind(2, static_cast<std::int64_t>(b));
        while (s.step()) {
            out << map({{"id", static_cast<qlonglong>(s.getInt(0))},
                        {"path", fromUtf8(s.getText(1))},
                        {"title", fromUtf8(s.getText(2))},
                        {"artist", fromUtf8(s.getText(3))},
                        {"movementNo", static_cast<int>(s.getInt(4))},
                        {"movementName", fromUtf8(s.getText(5))},
                        {"workTitle", fromUtf8(s.getText(6))},
                        {"durationMs", static_cast<qlonglong>(s.getInt(7))},
                        {"codec", fromUtf8(s.getText(8))},
                        {"albumId", static_cast<qlonglong>(s.getInt(9))},
                        {"albumTitle", fromUtf8(s.getText(10))},
                        {"albumArtist", fromUtf8(s.getText(11))},
                        {"albumArtHash", fromUtf8(s.getText(12))},
                        {"albumYear", static_cast<int>(s.getInt(13))},
                        {"workId", static_cast<qlonglong>(s.getInt(14))}});
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

QVariantList LibraryController::albumTracks(qlonglong albumId) {
    return trackRows(QStringLiteral("t.album_id = ?1 AND ?2 = ?2"), albumId, 0,
                     QStringLiteral("t.disc_no, t.track_no, t.path"));
}

QVariantList LibraryController::workTracks(qlonglong workId, qlonglong albumId) {
    return trackRows(QStringLiteral("t.work_id = ?1 AND t.album_id = ?2"), workId, albumId,
                     QStringLiteral("t.movement_no, t.disc_no, t.track_no, t.path"));
}

QVariantList LibraryController::workMovements(qlonglong workId) {
    return trackRows(QStringLiteral("t.work_id = ?1 AND ?2 = ?2"), workId, 0,
                     QStringLiteral("al.sort_key, t.movement_no, t.disc_no, t.track_no"));
}

// Rows of `works` that have existing tracks, in composer then title order.
std::vector<core::library::WorkEntry> LibraryController::workEntries() {
    std::vector<core::library::WorkEntry> out;
    if (!library_) {
        return out;
    }
    try {
        auto s = library_->db().prepare(
            "SELECT w.id, w.title, COALESCE(w.catalog_no, ''), COALESCE(c.name, ''), w.work_key, w.titles, "
            "w.number_key, w.family_key, w.family_label, "
            "(SELECT COUNT(DISTINCT t.album_id) FROM tracks t WHERE t.work_id = w.id AND t.missing_since IS NULL), "
            "(SELECT COUNT(*) FROM tracks t WHERE t.work_id = w.id AND t.missing_since IS NULL) "
            "FROM works w LEFT JOIN artists c ON c.id = w.composer_id "
            "WHERE EXISTS (SELECT 1 FROM tracks t WHERE t.work_id = w.id AND t.missing_since IS NULL) "
            "ORDER BY COALESCE(c.sort_name, ''), w.sort_key");
        while (s.step()) {
            core::library::WorkEntry w;
            w.id = s.getInt(0);
            w.title = s.getText(1);
            w.catalog = s.getText(2);
            w.composer = s.getText(3);
            w.workKey = s.getText(4);
            const QStringList titles = fromUtf8(s.getText(5)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString& t : titles) {
                w.titles.push_back(t.toStdString());
            }
            w.numberKey = s.getText(6);
            w.familyKnown = true;  // stored by the scan (D38)
            w.familyKey = s.getText(7);
            w.familyLabel = s.getText(8);
            w.recordings = static_cast<int>(s.getInt(9));
            w.tracks = static_cast<int>(s.getInt(10));
            out.push_back(std::move(w));
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

namespace {

QVariantMap workMap(const core::library::WorkEntry& w) {
    return map({{"id", static_cast<qlonglong>(w.id)},
                {"title", fromUtf8(w.title)},
                {"catalog", fromUtf8(w.catalog)},
                {"composer", fromUtf8(w.composer)},
                {"recordings", w.recordings},
                {"tracks", w.tracks}});
}

}  // namespace

QVariantList LibraryController::works() {
    QVariantList out;
    for (const core::library::WorkEntry& w : workEntries()) {
        out << workMap(w);
    }
    return out;
}

QVariantList LibraryController::worksByComposer() {
    QVariantList out;
    for (const core::library::ComposerNode& c : core::library::groupWorksByComposer(workEntries())) {
        QVariantList families;
        for (const core::library::WorkFamilyNode& f : c.families) {
            QVariantList works;
            for (const core::library::WorkEntry& w : f.works) {
                works << workMap(w);
            }
            families << map({{"label", fromUtf8(f.label)}, {"catalogRange", fromUtf8(f.catalogRange)}, {"works", works}});
        }
        out << map({{"name", fromUtf8(c.name)}, {"workCount", c.workCount}, {"families", families}});
    }
    return out;
}

QVariantList LibraryController::undetectedClassical() {
    QVariantList out;
    if (!library_) {
        return out;
    }
    try {
        auto s = library_->db().prepare(
            "SELECT a.id, a.title, a.album_artist, COUNT(*), MIN(t.composer), MIN(t.title) "
            "FROM v_tracks t JOIN albums a ON a.id = t.album_id "
            "WHERE t.missing_since IS NULL AND t.composer <> '' AND t.work_id IS NULL "
            "GROUP BY a.id ORDER BY a.sort_key LIMIT 500");
        while (s.step()) {
            out << map({{"albumId", static_cast<qlonglong>(s.getInt(0))},
                        {"albumTitle", fromUtf8(s.getText(1))},
                        {"albumArtist", fromUtf8(s.getText(2))},
                        {"tracks", static_cast<int>(s.getInt(3))},
                        {"composer", fromUtf8(s.getText(4))},
                        {"sampleTitle", fromUtf8(s.getText(5))}});
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

}  // namespace mp::app
