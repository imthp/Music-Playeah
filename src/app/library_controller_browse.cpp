// Milestone 4b: the part of LibraryController behind the Liked, Search, Artist and Composer pages.
// Likes live in memory (the table is small) and are written through; people, works and search are queries over
// the same database connection as the rest of the controller. Everything runs on the UI thread.

#include "library_controller.hpp"

#include <QDateTime>

#include <algorithm>
#include <exception>
#include <iterator>
#include <set>

#include "core/library/likes.hpp"
#include "core/library/work_identity.hpp"
#include "core/library/work_tree.hpp"
#include "core/text.hpp"
#include "library_util.hpp"

namespace mp::app {

using core::library::LikeKind;
using core::library::SearchKind;

namespace {

// Albums a person appears on without leading them: a track of the album names them as artist or credits them.
// ?1 is the artists row id.
constexpr const char* kAppearsOn =
    "COALESCE(a.album_artist_id, 0) <> ?1 AND a.id IN ("
    "SELECT t.album_id FROM tracks t WHERE t.artist_id = ?1 AND t.missing_since IS NULL UNION "
    "SELECT t.album_id FROM credits c JOIN tracks t ON t.id = c.track_id WHERE c.artist_id = ?1 AND t.missing_since IS NULL)";

// Tracks an artist plays on: as track artist or credited. Written as IN (... UNION ...) so both lookups use their
// index (an OR across the two would scan every track). ?1 is the artists row id.
constexpr const char* kArtistTracks =
    "t.id IN (SELECT id FROM tracks WHERE artist_id = ?1 UNION SELECT track_id FROM credits WHERE artist_id = ?1)";

// isLiked() runs for every heart on screen whenever any like changes: compare QStrings, no conversion.
std::optional<LikeKind> likeKindOf(const QString& kind) {
    if (kind == QLatin1String("track")) return LikeKind::Track;
    if (kind == QLatin1String("album")) return LikeKind::Album;
    if (kind == QLatin1String("artist")) return LikeKind::Artist;
    if (kind == QLatin1String("composer")) return LikeKind::Composer;
    if (kind == QLatin1String("work")) return LikeKind::Work;
    return std::nullopt;
}

constexpr const char* kSearchKindNames[] = {"song", "album", "artist", "composer", "work"};

std::optional<SearchKind> searchKindFromName(const QString& name) {
    for (std::size_t i = 0; i < std::size(kSearchKindNames); ++i) {
        if (name == QLatin1String(kSearchKindNames[i])) {
            return static_cast<SearchKind>(i);
        }
    }
    return std::nullopt;
}

QVariantMap searchItemMap(const core::library::SearchItem& item, int score) {
    return map({{"kind", QString::fromLatin1(kSearchKindNames[static_cast<std::size_t>(item.kind)])},
                {"id", static_cast<qlonglong>(item.id)},
                {"title", fromUtf8(item.title)},
                {"subtitle", fromUtf8(item.subtitle)},
                {"detail", fromUtf8(item.detail)},
                {"albumId", static_cast<qlonglong>(item.albumId)},
                {"artHash", fromUtf8(item.artHash)},
                {"durationMs", static_cast<qlonglong>(item.durationMs)},
                {"count1", item.count1},
                {"count2", item.count2},
                {"score", score}});
}

// "1,2,3" for an SQL IN list. Numbers only, so nothing needs escaping.
QString idList(const std::set<std::int64_t>& ids) {
    QStringList parts;
    for (const std::int64_t id : ids) {
        parts << QString::number(static_cast<qlonglong>(id));
    }
    return parts.join(QLatin1Char(','));
}

// Newest like first. `rows` are maps with an "id".
QVariantList withLikedAt(const QList<QVariantMap>& rows, const QHash<qint64, qint64>& likes, LikeKind kind) {
    QList<QVariantMap> sorted = rows;
    for (QVariantMap& row : sorted) {
        row.insert(QStringLiteral("likedAt"), static_cast<qlonglong>(likes.value(static_cast<qint64>(row.value(QStringLiteral("id")).toLongLong()) * 8 + static_cast<int>(kind))));
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const QVariantMap& a, const QVariantMap& b) {
        return a.value(QStringLiteral("likedAt")).toLongLong() > b.value(QStringLiteral("likedAt")).toLongLong();
    });
    QVariantList out;
    for (const QVariantMap& row : sorted) {
        out << row;
    }
    return out;
}

// `rows` (track maps) regrouped so the groups follow `ids` (by the value of `field`); order inside a group is kept.
QVariantList groupedInOrder(const QVariantList& rows, const QString& field, const QList<qlonglong>& ids) {
    QHash<qlonglong, QVariantList> groups;
    for (const QVariant& row : rows) {
        groups[row.toMap().value(field).toLongLong()] << row;
    }
    QVariantList out;
    for (const qlonglong id : ids) {
        out << groups.take(id);  // take: an id given twice plays once
    }
    return out;
}

// Positive ids of a QML list, in order, and the same as "1,2,3" for an SQL IN list.
QList<qlonglong> positiveIds(const QVariantList& list, QString* sqlList) {
    QList<qlonglong> ids;
    QStringList parts;
    for (const QVariant& v : list) {
        const qlonglong id = v.toLongLong();
        if (id > 0) {
            ids << id;
            parts << QString::number(id);
        }
    }
    *sqlList = parts.join(QLatin1Char(','));
    return ids;
}

QList<QVariantMap> toMaps(const QVariantList& list) {
    QList<QVariantMap> out;
    for (const QVariant& v : list) {
        out << v.toMap();
    }
    return out;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ likes

void LibraryController::reloadLikes() {
    likes_.clear();
    if (likesStore_) {
        try {
            for (const core::library::LikeEntry& e : likesStore_->all()) {
                likes_.insert(likeKey(e.kind, e.refId), e.likedAt);
            }
        } catch (const std::exception& e) {
            setError(QString::fromUtf8(e.what()));
        }
    }
    ++likesRevision_;
    emit likesChanged();
}

bool LibraryController::isLiked(const QString& kind, qlonglong id) const {
    const auto k = likeKindOf(kind);
    return k && likes_.contains(likeKey(*k, id));
}

bool LibraryController::setLiked(const QString& kind, qlonglong id, bool liked) {
    const auto k = likeKindOf(kind);
    if (!k || id <= 0 || !likesStore_) {
        return false;
    }
    const qint64 key = likeKey(*k, id);
    const bool was = likes_.contains(key);
    if (was == liked) {
        return liked;
    }
    const qint64 oldTime = likes_.value(key);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // Optimistic (D42): the heart changes first, the write follows (well under a millisecond in WAL mode).
    if (liked) {
        likes_.insert(key, now);
    } else {
        likes_.remove(key);
    }
    ++likesRevision_;
    emit likesChanged();
    try {
        likesStore_->set(*k, id, liked, now);
    } catch (const std::exception& e) {
        if (was) {
            likes_.insert(key, oldTime);
        } else {
            likes_.remove(key);
        }
        ++likesRevision_;
        emit likesChanged();
        setError(tr("Could not save the like: %1").arg(QString::fromUtf8(e.what())));
        return was;
    }
    return liked;
}

bool LibraryController::toggleLike(const QString& kind, qlonglong id) { return setLiked(kind, id, !isLiked(kind, id)); }

QVariantMap LibraryController::likedCounts() {
    QVariantMap out;
    if (!library_) {
        return out;
    }
    try {
        auto& db = library_->db();
        const std::string existing = "EXISTS (SELECT 1 FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL)";
        out.insert(QStringLiteral("track"), static_cast<qlonglong>(db.scalarInt(
            "SELECT COUNT(*) FROM likes l JOIN tracks t ON t.id = l.ref_id WHERE l.kind = 'track' AND t.missing_since IS NULL")));
        out.insert(QStringLiteral("album"), static_cast<qlonglong>(db.scalarInt(
            "SELECT COUNT(*) FROM likes l JOIN albums a ON a.id = l.ref_id WHERE l.kind = 'album' AND " + existing)));
        out.insert(QStringLiteral("artist"), static_cast<qlonglong>(db.scalarInt(
            "SELECT COUNT(*) FROM likes l JOIN artists ar ON ar.id = l.ref_id WHERE l.kind = 'artist' AND "
            "(EXISTS (SELECT 1 FROM tracks t WHERE t.artist_id = ar.id AND t.missing_since IS NULL) OR "
            " EXISTS (SELECT 1 FROM albums a WHERE a.album_artist_id = ar.id AND " + existing + "))")));
        out.insert(QStringLiteral("work"), static_cast<qlonglong>(db.scalarInt(
            "SELECT COUNT(*) FROM likes l JOIN works w ON w.id = l.ref_id WHERE l.kind = 'work' AND "
            "EXISTS (SELECT 1 FROM tracks t WHERE t.work_id = w.id AND t.missing_since IS NULL)")));
        // Composers are people, not rows: count the liked ones among the composers that exist.
        qlonglong likedComposers = 0;
        for (const auto& c : composers()) {
            if (likes_.contains(likeKey(LikeKind::Composer, c.id))) {
                ++likedComposers;
            }
        }
        out.insert(QStringLiteral("composer"), likedComposers);
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

QVariantList LibraryController::liked(const QString& kindName) {
    QVariantList out;
    const auto kind = likeKindOf(kindName);
    if (!library_ || !kind) {
        return out;
    }
    try {
        auto& db = library_->db();
        switch (*kind) {
            case LikeKind::Track:
                out = withLikedAt(toMaps(trackRows(QStringLiteral("t.id IN (SELECT ref_id FROM likes WHERE kind = 'track') AND ?1 = ?1 AND ?2 = ?2"),
                                                    0, 0, QStringLiteral("t.id"))),
                                   likes_, *kind);
                break;
            case LikeKind::Album:
                out = withLikedAt(toMaps(albumRows(QStringLiteral("a.id IN (SELECT ref_id FROM likes WHERE kind = 'album') AND ?1 = ?1"), 0)),
                                   likes_, *kind);
                break;
            case LikeKind::Artist: {
                QList<QVariantMap> rows;
                auto s = db.prepare(
                    "SELECT ar.id, ar.name, "
                    "(SELECT COUNT(*) FROM albums a WHERE a.album_artist_id = ar.id AND EXISTS "
                    "   (SELECT 1 FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL)) AS led, "
                    "(SELECT COUNT(*) FROM tracks t WHERE t.artist_id = ar.id AND t.missing_since IS NULL) AS tracks "
                    "FROM artists ar WHERE ar.id IN (SELECT ref_id FROM likes WHERE kind = 'artist') AND (led > 0 OR tracks > 0)");
                while (s.step()) {
                    rows << map({{"id", static_cast<qlonglong>(s.getInt(0))},
                                 {"name", fromUtf8(s.getText(1))},
                                 {"albums", static_cast<int>(s.getInt(2))},
                                 {"tracks", static_cast<int>(s.getInt(3))}});
                }
                out = withLikedAt(rows, likes_, *kind);
                break;
            }
            case LikeKind::Composer: {
                QList<QVariantMap> rows;
                for (const auto& c : composers()) {
                    if (likes_.contains(likeKey(LikeKind::Composer, c.id))) {
                        rows << map({{"id", static_cast<qlonglong>(c.id)},
                                     {"name", fromUtf8(c.name)},
                                     {"works", c.works},
                                     {"recordings", c.recordings}});
                    }
                }
                out = withLikedAt(rows, likes_, *kind);
                break;
            }
            case LikeKind::Work:
                out = withLikedAt(toMaps(worksWhere(QStringLiteral("w.id IN (SELECT ref_id FROM likes WHERE kind = 'work')"), QStringLiteral("w.sort_key"))),
                                   likes_, *kind);
                break;
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ tracks

QVariantList LibraryController::tracksByIds(const QVariantList& ids) {
    QString list;
    const QList<qlonglong> order = positiveIds(ids, &list);
    if (order.isEmpty()) {
        return {};
    }
    return groupedInOrder(trackRows(QStringLiteral("t.id IN (") + list + QStringLiteral(") AND ?1 = ?1 AND ?2 = ?2"), 0, 0, QStringLiteral("t.id")),
                          QStringLiteral("id"), order);
}

QVariantList LibraryController::tracksOfAlbums(const QVariantList& albumIds) {
    QString list;
    const QList<qlonglong> order = positiveIds(albumIds, &list);
    if (order.isEmpty()) {
        return {};
    }
    return groupedInOrder(trackRows(QStringLiteral("t.album_id IN (") + list + QStringLiteral(") AND ?1 = ?1 AND ?2 = ?2"), 0, 0,
                                    QStringLiteral("t.disc_no, t.track_no, t.path")),
                          QStringLiteral("albumId"), order);
}

QVariantList LibraryController::tracksOfWorks(const QVariantList& workIds) {
    QString list;
    const QList<qlonglong> order = positiveIds(workIds, &list);
    if (order.isEmpty()) {
        return {};
    }
    return groupedInOrder(
        trackRows(QStringLiteral("t.work_id IN (") + list +
                      QStringLiteral(") AND t.album_id = (SELECT r.album_id FROM tracks r JOIN albums ra ON ra.id = r.album_id "
                                     "WHERE r.work_id = t.work_id AND r.missing_since IS NULL ORDER BY ra.sort_key, ra.id LIMIT 1) "
                                     "AND ?1 = ?1 AND ?2 = ?2"),
                  0, 0, QStringLiteral("t.movement_no, t.disc_no, t.track_no, t.path")),
        QStringLiteral("workId"), order);
}

// ------------------------------------------------------------------------------------------------ people

std::optional<std::int64_t> LibraryController::artistIdByName(const QString& name) {
    if (!library_ || name.isEmpty()) {
        return std::nullopt;
    }
    try {
        auto s = library_->db().prepare("SELECT id FROM artists WHERE name_key = ?1");
        s.bind(1, core::normalizeText(name.toStdString()));
        if (s.step()) {
            return s.getInt(0);
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return std::nullopt;
}

const std::vector<core::library::ComposerSummary>& LibraryController::composers() {
    if (composersRevision_ != revision_) {
        composers_.clear();
        if (library_) {
            try {
                composers_ = core::library::loadComposers(library_->db());
            } catch (const std::exception& e) {
                setError(QString::fromUtf8(e.what()));
            }
        }
        composersRevision_ = revision_;
    }
    return composers_;
}

const core::library::ComposerSummary* LibraryController::composerOfArtist(const std::vector<core::library::ComposerSummary>& all,
                                                                          std::int64_t artistId) const {
    for (const auto& c : all) {
        if (c.artistIds.count(artistId) != 0) {
            return &c;
        }
    }
    return nullptr;
}

QVariantMap LibraryController::person(const QString& kind, const QString& name) {
    QVariantMap out = map({{"kind", kind}, {"id", 0}, {"name", name}});
    const auto id = artistIdByName(name);
    if (!library_ || !id) {
        return out;
    }
    try {
        auto& db = library_->db();
        if (kind == QLatin1String("composer")) {
            const auto& all = composers();
            const core::library::ComposerSummary* c = composerOfArtist(all, *id);
            if (c == nullptr) {
                return out;
            }
            qlonglong albums = 0;
            {
                auto s = db.prepare("SELECT COUNT(DISTINCT t.album_id) FROM tracks t WHERE t.missing_since IS NULL AND t.composer_id IN (" +
                                    idList(c->artistIds).toStdString() + ")");
                if (s.step()) {
                    albums = s.getInt(0);
                }
            }
            return map({{"kind", kind},
                        {"id", static_cast<qlonglong>(c->id)},
                        {"name", fromUtf8(c->name)},
                        {"works", c->works},
                        {"recordings", c->recordings},
                        {"albums", albums}});
        }
        auto count = [&](const std::string& sql) {
            auto s = db.prepare(sql);
            s.bind(1, *id);
            return s.step() ? static_cast<int>(s.getInt(0)) : 0;
        };
        const std::string hasTracks = "EXISTS (SELECT 1 FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL)";
        return map({{"kind", kind},
                    {"id", static_cast<qlonglong>(*id)},
                    {"name", name},
                    {"albums", count("SELECT COUNT(*) FROM albums a WHERE a.album_artist_id = ?1 AND " + hasTracks)},
                    {"appearances", count(std::string("SELECT COUNT(*) FROM albums a WHERE ") + kAppearsOn)},
                    {"tracks", count("SELECT COUNT(*) FROM tracks t WHERE t.artist_id = ?1 AND t.missing_since IS NULL")},
                    {"plays", count("SELECT COUNT(*) FROM plays p JOIN tracks t ON t.id = p.track_id WHERE t.artist_id = ?1")}});
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

QVariantList LibraryController::artistAlbums(qlonglong artistId, bool appearsOn) {
    return albumRows(appearsOn ? QString::fromLatin1(kAppearsOn) : QStringLiteral("a.album_artist_id = ?1"), artistId);
}

QVariantList LibraryController::artistTracks(qlonglong artistId, int limit) {
    return trackRows(QString::fromLatin1(kArtistTracks) + QStringLiteral(" AND ?2 = ?2"),
                     artistId, 0,
                     QStringLiteral("(SELECT COUNT(*) FROM plays p WHERE p.track_id = t.id) DESC, al.sort_key, t.disc_no, t.track_no, t.path"), limit);
}

// Works with existing tracks (id, title, catalog, composer, recordings, tracks), `where` over `w` and `ar` (composer).
QVariantList LibraryController::worksWhere(const QString& where, const QString& orderBy) {
    QVariantList out;
    if (!library_) {
        return out;
    }
    try {
        auto s = library_->db().prepare(
            "SELECT w.id, w.title, COALESCE(w.catalog_no, ''), COALESCE(ar.name, ''), "
            "(SELECT COUNT(DISTINCT t.album_id) FROM tracks t WHERE t.work_id = w.id AND t.missing_since IS NULL), "
            "(SELECT COUNT(*) FROM tracks t WHERE t.work_id = w.id AND t.missing_since IS NULL) "
            "FROM works w LEFT JOIN artists ar ON ar.id = w.composer_id "
            "WHERE (" + where.toStdString() + ") AND EXISTS (SELECT 1 FROM tracks t WHERE t.work_id = w.id AND t.missing_since IS NULL) "
            "ORDER BY " + orderBy.toStdString());
        while (s.step()) {
            out << map({{"id", static_cast<qlonglong>(s.getInt(0))},
                        {"title", fromUtf8(s.getText(1))},
                        {"catalog", fromUtf8(s.getText(2))},
                        {"composer", fromUtf8(s.getText(3))},
                        {"recordings", static_cast<int>(s.getInt(4))},
                        {"tracks", static_cast<int>(s.getInt(5))}});
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

QVariantList LibraryController::composerWorks(const QString& name) {
    QVariantList out;
    const auto id = artistIdByName(name);
    if (!library_ || !id) {
        return out;
    }
    try {
        const auto& all = composers();
        const core::library::ComposerSummary* c = composerOfArtist(all, *id);
        if (c == nullptr || c->works == 0) {
            return out;
        }
        std::vector<core::library::WorkEntry> mine;
        for (core::library::WorkEntry& w : workEntries("w.composer_id IN (" + idList(c->artistIds).toStdString() + ")")) {
            if (core::library::composerKeyOfWorkKey(w.workKey) == c->key) {
                mine.push_back(std::move(w));
            }
        }
        for (const core::library::ComposerNode& node : core::library::groupWorksByComposer(std::move(mine))) {
            for (const core::library::WorkFamilyNode& family : node.families) {
                // Form and instrument for the page's filter chips: from the work's title, else from its family's name
                // ("Partita in D minor" belongs to the "Violin Partita" family).
                const core::library::WorkIdentity fromFamily = core::library::identifyWork(family.label);
                QVariantList works;
                for (const core::library::WorkEntry& w : family.works) {
                    const core::library::WorkIdentity own = core::library::identifyWork(w.title);
                    works << map({{"id", static_cast<qlonglong>(w.id)},
                                  {"title", fromUtf8(w.title)},
                                  {"catalog", fromUtf8(w.catalog)},
                                  {"recordings", w.recordings},
                                  {"tracks", w.tracks},
                                  {"form", fromUtf8(own.form.empty() ? fromFamily.form : own.form)},
                                  {"instrument", fromUtf8(own.instrument.empty() ? fromFamily.instrument : own.instrument)}});
                }
                out << map({{"label", fromUtf8(family.label)}, {"catalogRange", fromUtf8(family.catalogRange)}, {"works", works}});
            }
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

QVariantList LibraryController::composerAlbums(const QString& name) {
    const auto id = artistIdByName(name);
    if (!library_ || !id) {
        return {};
    }
    try {
        const auto& all = composers();
        const core::library::ComposerSummary* c = composerOfArtist(all, *id);
        if (c == nullptr) {
            return {};
        }
        return albumRows(QStringLiteral("EXISTS (SELECT 1 FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL AND t.composer_id IN (") +
                             idList(c->artistIds) + QStringLiteral(")) AND ?1 = ?1"),
                         0);
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return {};
}

QVariantMap LibraryController::work(qlonglong workId) {
    const QVariantList rows = worksWhere(QStringLiteral("w.id = ") + QString::number(workId), QStringLiteral("w.id"));
    return rows.isEmpty() ? QVariantMap() : rows.first().toMap();
}

QVariantList LibraryController::workRecordings(qlonglong workId) {
    QVariantList out;
    if (!library_) {
        return out;
    }
    try {
        auto s = library_->db().prepare(
            "SELECT al.id, al.title, al.album_artist, COALESCE(o.image_hash, al.art_hash, ''), al.year, "
            "COALESCE(SUM(t.duration_ms), 0), COUNT(*), "
            "(SELECT COUNT(*) FROM plays p JOIN tracks pt ON pt.id = p.track_id WHERE pt.work_id = ?1 AND pt.album_id = al.id), "
            "COALESCE((SELECT MAX(p.started_at) FROM plays p JOIN tracks pt ON pt.id = p.track_id WHERE pt.work_id = ?1 AND pt.album_id = al.id), 0), "
            "COALESCE((SELECT group_concat(g.tag, ', ') FROM album_tags g WHERE g.album_id = al.id AND g.kind = 'instrument'), '') "
            "FROM tracks t JOIN albums al ON al.id = t.album_id "
            "LEFT JOIN image_overrides o ON o.kind = 'album' AND o.ref_id = al.id "
            "WHERE t.work_id = ?1 AND t.missing_since IS NULL GROUP BY al.id ORDER BY al.sort_key");
        s.bind(1, static_cast<std::int64_t>(workId));
        while (s.step()) {
            out << map({{"albumId", static_cast<qlonglong>(s.getInt(0))},
                        {"albumTitle", fromUtf8(s.getText(1))},
                        {"albumArtist", fromUtf8(s.getText(2))},
                        {"artHash", fromUtf8(s.getText(3))},
                        {"year", static_cast<int>(s.getInt(4))},
                        {"durationMs", static_cast<qlonglong>(s.getInt(5))},
                        {"tracks", static_cast<int>(s.getInt(6))},
                        {"plays", static_cast<int>(s.getInt(7))},
                        {"lastPlayed", static_cast<qlonglong>(s.getInt(8))},
                        {"instruments", fromUtf8(s.getText(9))}});
        }
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ search

const core::library::SearchIndex& LibraryController::searchIndex() {
    if (!searchIndex_ || searchIndexRevision_ != revision_) {
        searchIndex_ = std::make_unique<core::library::SearchIndex>();
        if (library_) {
            try {
                *searchIndex_ = core::library::loadSearchIndex(library_->db());
            } catch (const std::exception& e) {
                setError(QString::fromUtf8(e.what()));
            }
        }
        searchIndexRevision_ = revision_;
    }
    return *searchIndex_;
}

QVariantMap LibraryController::search(const QString& query, const QString& kind) {
    QVariantMap out;
    out.insert(QStringLiteral("query"), query);
    out.insert(QStringLiteral("top"), QVariant());
    QVariantMap totals;
    for (const char* name : kSearchKindNames) {
        out.insert(QString::fromLatin1(name), QVariantList());
        totals.insert(QString::fromLatin1(name), 0);
    }
    const std::string q = query.toStdString();
    const core::library::SearchIndex& index = searchIndex();

    if (const auto only = searchKindFromName(kind)) {
        QVariantList list;
        for (const auto& hit : index.search(q, *only, 100)) {
            list << searchItemMap(*hit.item, hit.score);
        }
        totals.insert(kind, static_cast<qlonglong>(list.size()));
        out.insert(kind, list);
    } else {
        const auto results = index.search(q, 6);
        for (std::size_t k = 0; k < core::library::kSearchKinds; ++k) {
            QVariantList list;
            for (const auto& hit : results.byKind[k]) {
                list << searchItemMap(*hit.item, hit.score);
            }
            const QString name = QString::fromLatin1(kSearchKindNames[k]);
            out.insert(name, list);
            totals.insert(name, static_cast<qlonglong>(results.totals[k]));
        }
        if (results.top) {
            out.insert(QStringLiteral("top"), searchItemMap(*results.top->item, results.top->score));
        }
    }
    out.insert(QStringLiteral("totals"), totals);
    return out;
}

}  // namespace mp::app
