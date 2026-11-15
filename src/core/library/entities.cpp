#include "core/library/entities.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/text.hpp"

namespace mp::core::library {

namespace {

constexpr const char* kVariousArtists = "Various Artists";
constexpr char kKeySeparator = '\x1F';

struct Row {
    std::int64_t id = 0;
    std::string dir;  // folder of the file, for compilation detection
    std::string artist;
    std::string albumArtist;
    std::string composer;
    std::string album;
    int year = 0;
};

std::string parentDir(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace

ResolveStats resolveEntities(Database& db) {
    ResolveStats stats;
    Transaction tx(db);

    // A track without an album artist is grouped with the others of its album name. If one of them
    // changed, all of them must be looked at again (a new track by another artist turns the album
    // into a compilation).
    db.exec(
        "UPDATE tracks SET resolved = 0 "
        "WHERE resolved = 1 AND album_artist = '' AND album <> '' "
        "AND album IN (SELECT album FROM tracks WHERE resolved = 0 AND album_artist = '' AND album <> '')");

    std::vector<Row> rows;
    {
        Statement s = db.prepare(
            "SELECT id, path, artist, album_artist, composer, album, year FROM tracks WHERE resolved = 0");
        while (s.step()) {
            Row r;
            r.id = s.getInt(0);
            r.dir = parentDir(s.getText(1));
            r.artist = s.getText(2);
            r.albumArtist = s.getText(3);
            r.composer = s.getText(4);
            r.album = s.getText(5);
            r.year = static_cast<int>(s.getInt(6));
            rows.push_back(std::move(r));
        }
    }

    // Compilation detection: (folder, album) groups without any album artist and with >1 distinct artist.
    std::map<std::pair<std::string, std::string>, std::set<std::string>> artistsOfGroup;
    for (const Row& r : rows) {
        if (r.albumArtist.empty() && !r.album.empty()) {
            artistsOfGroup[{r.dir, normalizeText(r.album)}].insert(normalizeText(r.artist));
        }
    }

    std::unordered_map<std::string, std::int64_t> artistIds;
    std::unordered_map<std::string, std::int64_t> albumIds;
    std::set<std::int64_t> touchedAlbums;

    Statement findArtist = db.prepare("SELECT id FROM artists WHERE name_key = ?1");
    Statement insertArtist = db.prepare("INSERT INTO artists(name, name_key, sort_name) VALUES (?1, ?2, ?3)");
    auto artistId = [&](const std::string& name) -> std::optional<std::int64_t> {
        const std::string key = normalizeText(name);
        if (key.empty()) {
            return std::nullopt;
        }
        if (const auto it = artistIds.find(key); it != artistIds.end()) {
            return it->second;
        }
        findArtist.bind(1, key);
        std::int64_t id = 0;
        if (findArtist.step()) {
            id = findArtist.getInt(0);
        }
        findArtist.reset();
        if (id == 0) {
            insertArtist.bind(1, trimText(name)).bind(2, key).bind(3, sortName(name));
            insertArtist.run();
            id = db.lastInsertRowId();
            ++stats.artistsCreated;
        }
        artistIds.emplace(key, id);
        return id;
    };

    Statement findAlbum = db.prepare("SELECT id FROM albums WHERE sort_key = ?1");
    Statement insertAlbum = db.prepare(
        "INSERT INTO albums(title, album_artist, album_artist_id, year, sort_key) VALUES (?1, ?2, ?3, ?4, ?5)");
    auto albumId = [&](const std::string& albumArtist, const std::string& title, int year) -> std::int64_t {
        const std::string key = normalizeText(albumArtist) + kKeySeparator + normalizeText(title);
        if (const auto it = albumIds.find(key); it != albumIds.end()) {
            return it->second;
        }
        findAlbum.bind(1, key);
        std::int64_t id = 0;
        if (findAlbum.step()) {
            id = findAlbum.getInt(0);
        }
        findAlbum.reset();
        if (id == 0) {
            const std::optional<std::int64_t> artist = artistId(albumArtist);
            insertAlbum.bind(1, trimText(title)).bind(2, trimText(albumArtist));
            if (artist) {
                insertAlbum.bind(3, *artist);
            } else {
                insertAlbum.bind(3, nullptr);
            }
            insertAlbum.bind(4, year).bind(5, key);
            insertAlbum.run();
            id = db.lastInsertRowId();
            ++stats.albumsCreated;
        }
        albumIds.emplace(key, id);
        return id;
    };

    Statement update = db.prepare(
        "UPDATE tracks SET artist_id = ?1, composer_id = ?2, album_id = ?3, resolved = 1 WHERE id = ?4");
    for (const Row& r : rows) {
        const std::optional<std::int64_t> artist = artistId(r.artist);
        const std::optional<std::int64_t> composer = artistId(r.composer);

        std::optional<std::int64_t> album;
        if (!r.album.empty()) {
            std::string albumArtist = r.albumArtist;
            if (albumArtist.empty()) {
                const auto& artists = artistsOfGroup[{r.dir, normalizeText(r.album)}];
                albumArtist = artists.size() > 1 ? kVariousArtists : r.artist;
            }
            album = albumId(albumArtist, r.album, r.year);
            touchedAlbums.insert(*album);
        }

        artist ? update.bind(1, *artist) : update.bind(1, nullptr);
        composer ? update.bind(2, *composer) : update.bind(2, nullptr);
        album ? update.bind(3, *album) : update.bind(3, nullptr);
        update.bind(4, r.id);
        update.run();
        ++stats.tracks;
    }

    // An album's year is its earliest year among its tracks.
    Statement updateYear = db.prepare(
        "UPDATE albums SET year = COALESCE((SELECT MIN(year) FROM tracks WHERE album_id = albums.id AND year > 0), 0) "
        "WHERE id = ?1");
    for (const std::int64_t id : touchedAlbums) {
        updateYear.bind(1, id);
        updateYear.run();
    }

    // Retagged files leave empty albums and unused artists behind. Albums/artists the user added
    // data to (tags, photos, bios, credits, works) are kept.
    db.exec(
        "DELETE FROM albums WHERE NOT EXISTS (SELECT 1 FROM tracks WHERE album_id = albums.id)");
    db.exec(
        "DELETE FROM artists WHERE "
        "NOT EXISTS (SELECT 1 FROM tracks WHERE artist_id = artists.id) AND "
        "NOT EXISTS (SELECT 1 FROM tracks WHERE composer_id = artists.id) AND "
        "NOT EXISTS (SELECT 1 FROM albums WHERE album_artist_id = artists.id) AND "
        "NOT EXISTS (SELECT 1 FROM credits WHERE artist_id = artists.id) AND "
        "NOT EXISTS (SELECT 1 FROM works WHERE composer_id = artists.id) AND "
        "NOT EXISTS (SELECT 1 FROM artist_info WHERE artist_id = artists.id) AND "
        "NOT EXISTS (SELECT 1 FROM image_overrides WHERE kind = 'artist' AND ref_id = artists.id)");

    tx.commit();
    return stats;
}

}  // namespace mp::core::library
