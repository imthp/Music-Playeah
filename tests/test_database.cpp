#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "core/library/database.hpp"
#include "core/library/library.hpp"
#include "library_fixtures.hpp"

using namespace mp::core::library;
using mp::test::TempDir;
namespace fs = std::filesystem;

namespace {

std::set<std::string> namesOf(Database& db, const char* type) {
    std::set<std::string> names;
    Statement s = db.prepare("SELECT name FROM sqlite_master WHERE type = ?1");
    s.bind(1, type);
    while (s.step()) {
        names.insert(s.getText(0));
    }
    return names;
}

void insertTrack(Database& db, const std::string& path, const std::string& title, int year = 1999) {
    Statement s = db.prepare(
        "INSERT INTO tracks(path, mtime, size, title, artist, year, added_at) VALUES (?1, 1, 1, ?2, 'Artist', ?3, 1)");
    s.bind(1, path).bind(2, title).bind(3, year);
    s.run();
}

}  // namespace

TEST_CASE("A new database gets the full schema (SPEC section 6)", "[db]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    REQUIRE(db.userVersion() == 0);
    db.migrate();
    CHECK(db.userVersion() == Database::latestVersion());

    const auto tables = namesOf(db, "table");
    for (const char* name : {"folders", "tracks", "albums", "artists", "works", "credits", "album_tags", "plays",
                             "playlists", "playlist_items", "scrobble_queue", "lastfm_cache", "waveforms",
                             "artist_info", "tag_overrides", "image_overrides", "likes"}) {
        CHECK(tables.count(name) == 1);
    }
    CHECK(namesOf(db, "view").count("v_tracks") == 1);
}

TEST_CASE("Migrating is repeatable and the database uses WAL", "[db]") {
    TempDir dir;
    const auto file = dir.path() / "lib.db";
    {
        Database db(file);
        db.migrate();
        db.migrate();  // no-op
        Statement s = db.prepare("PRAGMA journal_mode");
        REQUIRE(s.step());
        CHECK(s.getText(0) == "wal");
    }
    Database again(file);
    again.migrate();
    CHECK(again.userVersion() == Database::latestVersion());
}

TEST_CASE("A database from a newer app is refused, not downgraded", "[db]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.migrate();
    db.exec("PRAGMA user_version = 999");
    CHECK_THROWS_AS(db.migrate(), DatabaseError);
}

TEST_CASE("Statements bind text, numbers, NULL and optionals", "[db]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.exec("CREATE TABLE t(a TEXT, b INTEGER, c REAL, d TEXT)");
    Statement insert = db.prepare("INSERT INTO t VALUES (?1, ?2, ?3, ?4)");
    insert.bind(1, "B\xC3\xB6rk").bind(2, std::size_t{42}).bind(3, std::optional<double>{}).bind(4, nullptr);
    insert.run();
    insert.bind(1, std::string("x")).bind(2, -7).bind(3, std::optional<double>{-6.5}).bind(4, "y");
    insert.run();

    Statement select = db.prepare("SELECT a, b, c, d FROM t ORDER BY b");
    REQUIRE(select.step());
    CHECK(select.getText(0) == "x");
    CHECK(select.getInt(1) == -7);
    CHECK(select.getDouble(2) == -6.5);
    CHECK(select.getText(3) == "y");
    REQUIRE(select.step());
    CHECK(select.getText(0) == "B\xC3\xB6rk");
    CHECK(select.getInt(1) == 42);
    CHECK(select.isNull(2));
    CHECK(select.isNull(3));
    CHECK(select.getText(3).empty());
    CHECK_FALSE(select.step());
}

TEST_CASE("A transaction rolls back unless committed", "[db]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.exec("CREATE TABLE t(a INTEGER)");
    try {
        Transaction tx(db);
        db.exec("INSERT INTO t VALUES (1)");
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {
    }
    CHECK(db.scalarInt("SELECT COUNT(*) FROM t") == 0);
    {
        Transaction tx(db);
        db.exec("INSERT INTO t VALUES (1)");
        tx.commit();
    }
    CHECK(db.scalarInt("SELECT COUNT(*) FROM t") == 1);
}

TEST_CASE("SQL errors become DatabaseError", "[db]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    CHECK_THROWS_AS(db.exec("SELEKT 1"), DatabaseError);
    CHECK_THROWS_AS(db.prepare("SELECT * FROM nope"), DatabaseError);
}

TEST_CASE("Foreign keys are enforced", "[db]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.migrate();
    CHECK_THROWS_AS(db.exec("INSERT INTO plays(track_id, started_at, played_ms) VALUES (12345, 1, 1)"), DatabaseError);
}

TEST_CASE("v_tracks: an override wins over the file tag, and reverting restores it", "[db][overrides]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.migrate();
    insertTrack(db, "/m/a.flac", "File Title", 1999);
    const auto id = db.lastInsertRowId();

    auto effective = [&](const char* column) {
        Statement s = db.prepare(std::string("SELECT ") + column + " FROM v_tracks WHERE id = " + std::to_string(id));
        REQUIRE(s.step());
        return s.getText(0);
    };
    CHECK(effective("title") == "File Title");
    CHECK(effective("year") == "1999");

    db.exec("INSERT INTO tag_overrides(track_id, field, value, source, updated_at) VALUES (" + std::to_string(id) +
            ", 'title', 'Corrected', 'manual', 1), (" + std::to_string(id) + ", 'year', '2005', 'musicbrainz', 1)");
    CHECK(effective("title") == "Corrected");
    CHECK(effective("year") == "2005");
    CHECK(effective("artist") == "Artist");  // untouched fields still come from the file

    // The raw file tags are not changed by overrides.
    CHECK(db.scalarInt("SELECT COUNT(*) FROM tracks WHERE title = 'File Title'") == 1);

    db.exec("DELETE FROM tag_overrides WHERE field = 'title'");
    CHECK(effective("title") == "File Title");
    CHECK(effective("year") == "2005");
}

TEST_CASE("Library adds, lists and removes folders and refuses overlaps", "[db][folders]") {
    TempDir dir;
    fs::create_directories(dir.path() / "music" / "jazz");
    fs::create_directories(dir.path() / "other");
    Library lib(dir.path() / "app" / "lib.db");  // parent folder is created

    const auto added = lib.addFolder(dir.path() / "music");
    REQUIRE(added.ok);
    CHECK(added.path.find('\\') == std::string::npos);
    CHECK(lib.folders().size() == 1);

    CHECK_FALSE(lib.addFolder(dir.path() / "music").ok);                  // same
    CHECK_FALSE(lib.addFolder(dir.path() / "music" / "jazz").ok);         // inside
    CHECK_FALSE(lib.addFolder(dir.path() / "nope").ok);                   // does not exist
    CHECK(lib.addFolder(dir.path() / "other").ok);
    CHECK(lib.folders().size() == 2);

    CHECK(lib.removeFolder(added.path));
    CHECK_FALSE(lib.removeFolder(added.path));
    CHECK(lib.folders().size() == 1);
}

TEST_CASE("pathRangeUnder selects exactly the paths below a folder", "[db][folders]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.exec("CREATE TABLE p(path TEXT)");
    for (const char* p : {"/m/a/1.flac", "/m/a/sub/2.flac", "/m/ab/3.flac", "/m/a_/4.flac", "/m/a%/5.flac", "/m/a"}) {
        Statement s = db.prepare("INSERT INTO p VALUES (?1)");
        s.bind(1, p);
        s.run();
    }
    const PathRange r = pathRangeUnder("/m/a");
    Statement s = db.prepare("SELECT COUNT(*) FROM p WHERE path >= ?1 AND path < ?2");
    s.bind(1, r.low).bind(2, r.high);
    REQUIRE(s.step());
    CHECK(s.getInt(0) == 2);  // only /m/a/1.flac and /m/a/sub/2.flac
}
