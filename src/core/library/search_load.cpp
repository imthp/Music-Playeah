#include "core/library/search_load.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace mp::core::library {

std::string composerKeyOfWorkKey(const std::string& workKey) {
    const std::size_t at = workKey.find('\x1F');
    return at == std::string::npos ? std::string() : workKey.substr(0, at);
}

namespace {

// Existing = at least one track of the album that is not missing.
constexpr const char* kAlbumHasTracks = "EXISTS (SELECT 1 FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL)";

void loadSongs(Database& db, SearchIndex& index) {
    Statement s = db.prepare(
        "SELECT t.id, t.title, t.artist, t.album, COALESCE(t.album_id, 0), t.duration_ms, "
        "COALESCE(o.image_hash, al.art_hash, ''), COALESCE(w.title, ''), t.composer, t.movement_name "
        "FROM v_tracks t LEFT JOIN albums al ON al.id = t.album_id "
        "LEFT JOIN image_overrides o ON o.kind = 'album' AND o.ref_id = t.album_id "
        "LEFT JOIN works w ON w.id = t.work_id WHERE t.missing_since IS NULL");
    while (s.step()) {
        SearchItem item;
        item.kind = SearchKind::Song;
        item.id = s.getInt(0);
        item.title = s.getText(1);
        item.subtitle = s.getText(2);
        item.detail = s.getText(3);
        item.albumId = s.getInt(4);
        item.durationMs = s.getInt(5);
        item.artHash = s.getText(6);
        item.extraText = s.getText(7) + " " + s.getText(8) + " " + s.getText(9);
        index.add(std::move(item));
    }
}

void loadAlbums(Database& db, SearchIndex& index) {
    Statement s = db.prepare(
        std::string("SELECT a.id, a.title, a.album_artist, COALESCE(o.image_hash, a.art_hash, ''), "
                    "(SELECT COUNT(*) FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL), "
                    "COALESCE((SELECT group_concat(c, ' ') FROM (SELECT DISTINCT v.composer AS c FROM v_tracks v "
                    "          WHERE v.album_id = a.id AND v.missing_since IS NULL AND v.composer <> '')), '') "
                    "FROM albums a LEFT JOIN image_overrides o ON o.kind = 'album' AND o.ref_id = a.id WHERE ") +
        kAlbumHasTracks);
    while (s.step()) {
        SearchItem item;
        item.kind = SearchKind::Album;
        item.id = s.getInt(0);
        item.albumId = item.id;
        item.title = s.getText(1);
        item.subtitle = s.getText(2);
        item.artHash = s.getText(3);
        item.count1 = static_cast<int>(s.getInt(4));
        item.extraText = s.getText(5);
        index.add(std::move(item));
    }
}

void loadArtists(Database& db, SearchIndex& index) {
    Statement s = db.prepare(
        "SELECT ar.id, ar.name, "
        "(SELECT COUNT(*) FROM albums a WHERE a.album_artist_id = ar.id AND EXISTS "
        "   (SELECT 1 FROM tracks t WHERE t.album_id = a.id AND t.missing_since IS NULL)) AS led, "
        "(SELECT COUNT(*) FROM tracks t WHERE t.artist_id = ar.id AND t.missing_since IS NULL) AS tracks "
        "FROM artists ar WHERE led > 0 OR tracks > 0");
    while (s.step()) {
        SearchItem item;
        item.kind = SearchKind::Artist;
        item.id = s.getInt(0);
        item.title = s.getText(1);
        item.count1 = static_cast<int>(s.getInt(2));
        item.count2 = static_cast<int>(s.getInt(3));
        index.add(std::move(item));
    }
}

void loadWorks(Database& db, SearchIndex& index) {
    Statement s = db.prepare(
        "SELECT w.id, w.title, COALESCE(w.catalog_no, ''), COALESCE(ar.name, ''), COUNT(DISTINCT t.album_id) "
        "FROM works w JOIN tracks t ON t.work_id = w.id AND t.missing_since IS NULL "
        "LEFT JOIN artists ar ON ar.id = w.composer_id GROUP BY w.id");
    while (s.step()) {
        SearchItem item;
        item.kind = SearchKind::Work;
        item.id = s.getInt(0);
        item.title = s.getText(1);
        item.detail = s.getText(2);
        item.subtitle = s.getText(3);
        item.count1 = static_cast<int>(s.getInt(4));
        index.add(std::move(item));
    }
}

void loadComposerItems(Database& db, SearchIndex& index) {
    for (const ComposerSummary& c : loadComposers(db)) {
        SearchItem item;
        item.kind = SearchKind::Composer;
        item.id = c.id;
        item.title = c.name;
        item.count1 = c.works;
        item.count2 = c.recordings;
        index.add(std::move(item));
    }
}

}  // namespace

std::vector<ComposerSummary> loadComposers(Database& db) {
    std::map<std::string, ComposerSummary> byKey;  // composer key -> person
    std::set<std::int64_t> withWorks;              // artist ids that own at least one work

    Statement s = db.prepare(
        "SELECT w.composer_id, ar.name, w.work_key, COUNT(DISTINCT t.album_id) "
        "FROM works w JOIN tracks t ON t.work_id = w.id AND t.missing_since IS NULL "
        "JOIN artists ar ON ar.id = w.composer_id GROUP BY w.id");
    while (s.step()) {
        const std::int64_t artistId = s.getInt(0);
        const std::string name = s.getText(1);
        std::string key = composerKeyOfWorkKey(s.getText(2));
        if (key.empty()) {
            key = "#" + std::to_string(artistId);  // no composer part: the artists row is the whole person
        }
        ComposerSummary& c = byKey[key];
        c.key = key;
        c.id = c.id == 0 ? artistId : std::min(c.id, artistId);
        if (name.size() > c.name.size()) {
            c.name = name;
        }
        ++c.works;
        c.recordings += static_cast<int>(s.getInt(3));
        c.artistIds.insert(artistId);
        withWorks.insert(artistId);
    }
    std::vector<ComposerSummary> out;
    out.reserve(byKey.size());
    for (auto& [key, c] : byKey) {
        out.push_back(std::move(c));
    }

    // Composer tags that no work belongs to (not detected, or not classical): the person still has a page.
    Statement tagged = db.prepare(
        "SELECT ar.id, ar.name FROM artists ar WHERE EXISTS "
        "(SELECT 1 FROM tracks t WHERE t.composer_id = ar.id AND t.missing_since IS NULL)");
    while (tagged.step()) {
        if (withWorks.count(tagged.getInt(0)) != 0) {
            continue;
        }
        ComposerSummary c;
        c.id = tagged.getInt(0);
        c.name = tagged.getText(1);
        c.artistIds.insert(c.id);
        out.push_back(std::move(c));
    }
    return out;
}

SearchIndex loadSearchIndex(Database& db) {
    SearchIndex index;
    loadSongs(db, index);
    loadAlbums(db, index);
    loadArtists(db, index);
    loadWorks(db, index);
    loadComposerItems(db, index);
    return index;
}

}  // namespace mp::core::library
