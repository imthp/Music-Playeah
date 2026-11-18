#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <thread>

#include "core/library/library.hpp"
#include "core/library/scanner.hpp"
#include "library_fixtures.hpp"

using namespace mp::core::library;
using namespace mp::test;
using Catch::Matchers::WithinAbs;

namespace {

// A library with one watched folder `music/` in a temp dir, and a scanner for it.
struct Env {
    TempDir dir;
    fs::path music = dir.path() / "music";
    Library lib;
    Scanner scanner;

    explicit Env(ScanOptions options = {}) : lib(dir.path() / "app" / "library.db"), scanner(lib.dbFile(), options) {
        fs::create_directories(music);
        REQUIRE(lib.addFolder(music).ok);
    }

    ScanSummary scan() { return scanAndWait(scanner); }

    // Adds an album of `count` tracks by `artist` under music/<folder>.
    void album(const std::string& folder, const std::string& artist, const std::string& title, int count,
               const std::string& albumArtist = "") {
        for (int i = 1; i <= count; ++i) {
            TagSpec t;
            t.title = title + " track " + std::to_string(i);
            t.artist = artist;
            t.albumArtist = albumArtist;
            t.album = title;
            t.track = std::to_string(i) + "/" + std::to_string(count);
            t.date = "2001";
            makeTrack(music / folder / (std::to_string(i) + ".wav"), t, 4410 + static_cast<std::uint32_t>(i) * 441);
        }
    }

    std::int64_t trackId(const std::string& relativePath) {
        Statement s = lib.db().prepare("SELECT id FROM tracks WHERE path = ?1");
        s.bind(1, mp::core::genericUtf8(music / relativePath));
        return s.step() ? s.getInt(0) : -1;
    }
};

}  // namespace

TEST_CASE("First scan stores tracks, albums and artists", "[scan]") {
    Env env;
    env.album("Album A", "Artist A", "First Album", 3);
    env.album("Album B", "Artist B", "Second Album", 2);

    std::atomic<int> progressCalls{0};
    const ScanSummary summary = scanAndWait(env.scanner, &progressCalls);
    CHECK(summary.fatalError.empty());
    CHECK_FALSE(summary.cancelled);
    CHECK(summary.filesSeen == 5);
    CHECK(summary.added == 5);
    CHECK(summary.updated == 0);
    CHECK(summary.failed == 0);
    CHECK(summary.markedMissing == 0);
    CHECK(progressCalls > 0);

    const LibraryCounts c = env.lib.counts();
    CHECK(c.tracks == 5);
    CHECK(c.albums == 2);
    CHECK(c.artists == 2);
    CHECK(c.missing == 0);

    Statement s = env.lib.db().prepare(
        "SELECT title, artist, album, track_no, year, codec, sample_rate, bit_depth, channels, duration_ms "
        "FROM v_tracks WHERE path = ?1");
    s.bind(1, mp::core::genericUtf8(env.music / "Album A" / "2.wav"));
    REQUIRE(s.step());
    CHECK(s.getText(0) == "First Album track 2");
    CHECK(s.getText(1) == "Artist A");
    CHECK(s.getText(2) == "First Album");
    CHECK(s.getInt(3) == 2);
    CHECK(s.getInt(4) == 2001);
    CHECK(s.getText(5) == "WAV");
    CHECK(s.getInt(6) == 44100);
    CHECK(s.getInt(7) == 16);
    CHECK(s.getInt(8) == 2);
    CHECK(s.getInt(9) > 100);

    // Every track points at an album and an artist.
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM tracks WHERE album_id IS NULL OR artist_id IS NULL") == 0);
    CHECK(env.lib.db().scalarInt("SELECT year FROM albums WHERE title = 'First Album'") == 2001);
}

TEST_CASE("Every scanned field is stored in the right column", "[scan]") {
    Env env;
    TagSpec t;
    t.title = "Allemande";
    t.artist = "Performer";
    t.albumArtist = "Album Artist";
    t.album = "The Album";
    t.composer = "The Composer";
    t.genre = "Classical";
    t.work = "Suite No. 1";
    t.movementName = "Allemande";
    t.movementNumber = "2/4";
    t.track = "3/9";
    t.disc = "2/2";
    t.date = "1999-09-09";
    t.gainTrack = "-3.50 dB";
    t.gainAlbum = "-4.25 dB";
    makeTrack(env.music / "x.wav", t, 44100);
    REQUIRE(env.scan().added == 1);

    Statement s = env.lib.db().prepare(
        "SELECT title, artist, album_artist, composer, album, work_tag, movement_no, movement_total, movement_name, "
        "disc_no, track_no, year, genre, duration_ms, codec, sample_rate, bit_depth, channels, bitrate, rg_track_gain, "
        "rg_album_gain FROM v_tracks");
    REQUIRE(s.step());
    CHECK(s.getText(0) == "Allemande");
    CHECK(s.getText(1) == "Performer");
    CHECK(s.getText(2) == "Album Artist");
    CHECK(s.getText(3) == "The Composer");
    CHECK(s.getText(4) == "The Album");
    CHECK(s.getText(5) == "Suite No. 1");
    CHECK(s.getInt(6) == 2);
    CHECK(s.getInt(7) == 4);
    CHECK(s.getText(8) == "Allemande");
    CHECK(s.getInt(9) == 2);
    CHECK(s.getInt(10) == 3);
    CHECK(s.getInt(11) == 1999);
    CHECK(s.getText(12) == "Classical");
    CHECK(s.getInt(13) >= 990);
    CHECK(s.getText(14) == "WAV");
    CHECK(s.getInt(15) == 44100);
    CHECK(s.getInt(16) == 16);
    CHECK(s.getInt(17) == 2);
    CHECK(s.getInt(18) > 1000);
    CHECK_THAT(s.getDouble(19), WithinAbs(-3.5, 1e-9));
    CHECK_THAT(s.getDouble(20), WithinAbs(-4.25, 1e-9));

    // Performer, album artist and composer are three artists.
    CHECK(env.lib.counts().artists == 3);
}

TEST_CASE("A rescan with nothing changed reads nothing", "[scan]") {
    Env env;
    env.album("A", "Artist", "Album", 4);
    REQUIRE(env.scan().added == 4);

    const ScanSummary again = env.scan();
    CHECK(again.filesSeen == 4);
    CHECK(again.unchanged == 4);
    CHECK(again.added == 0);
    CHECK(again.updated == 0);
    CHECK(again.markedMissing == 0);
    CHECK(again.restored == 0);
    CHECK(env.lib.counts().tracks == 4);
}

TEST_CASE("A changed file is read again; overrides survive the rescan", "[scan][overrides]") {
    Env env;
    env.album("A", "Artist", "Album", 2);
    REQUIRE(env.scan().added == 2);

    const auto id = env.trackId("A/1.wav");
    REQUIRE(id > 0);
    env.lib.db().exec("INSERT INTO tag_overrides(track_id, field, value, source, updated_at) VALUES (" +
                      std::to_string(id) + ", 'title', 'My Correction', 'manual', 1)");

    TagSpec retagged;
    retagged.title = "Retagged In File";
    retagged.artist = "Artist";
    retagged.album = "Album";
    retagged.track = "1";
    tagFile(env.music / "A" / "1.wav", retagged);
    touchLater(env.music / "A" / "1.wav");

    const ScanSummary again = env.scan();
    CHECK(again.updated == 1);
    CHECK(again.unchanged == 1);
    CHECK(again.added == 0);

    CHECK(env.trackId("A/1.wav") == id);  // same row
    Statement raw = env.lib.db().prepare("SELECT title FROM tracks WHERE id = ?1");
    raw.bind(1, id);
    REQUIRE(raw.step());
    CHECK(raw.getText(0) == "Retagged In File");  // file tag updated...
    Statement eff = env.lib.db().prepare("SELECT title FROM v_tracks WHERE id = ?1");
    eff.bind(1, id);
    REQUIRE(eff.step());
    CHECK(eff.getText(0) == "My Correction");  // ...override still wins
}

TEST_CASE("A deleted file is marked missing, not deleted; its history stays", "[scan][missing]") {
    Env env;
    env.album("A", "Artist", "Album", 3);
    REQUIRE(env.scan().added == 3);
    const auto id = env.trackId("A/2.wav");
    env.lib.db().exec("INSERT INTO plays(track_id, started_at, played_ms, completed) VALUES (" + std::to_string(id) +
                      ", 100, 5000, 1)");

    fs::remove(env.music / "A" / "2.wav");
    const ScanSummary again = env.scan();
    CHECK(again.markedMissing == 1);
    CHECK(again.unchanged == 2);

    const LibraryCounts c = env.lib.counts();
    CHECK(c.tracks == 2);
    CHECK(c.missing == 1);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM tracks") == 3);  // row still there
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM plays WHERE track_id = " + std::to_string(id)) == 1);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM v_tracks WHERE missing_since IS NOT NULL") == 1);

    // Scanning again doesn't count it as newly missing.
    CHECK(env.scan().markedMissing == 0);
}

TEST_CASE("A file that comes back at the same path is restored", "[scan][missing]") {
    Env env;
    env.album("A", "Artist", "Album", 2);
    REQUIRE(env.scan().added == 2);

    const fs::path aside = env.dir.path() / "aside.wav";
    fs::rename(env.music / "A" / "1.wav", aside);  // rename keeps the mtime
    REQUIRE(env.scan().markedMissing == 1);
    CHECK(env.lib.counts().missing == 1);

    fs::rename(aside, env.music / "A" / "1.wav");
    const ScanSummary back = env.scan();
    CHECK(back.restored == 1);
    CHECK(back.added == 0);
    CHECK(env.lib.counts().missing == 0);
    CHECK(env.lib.counts().tracks == 2);
}

TEST_CASE("An unplugged or renamed folder marks its tracks missing, and they return with it", "[scan][missing]") {
    Env env;
    env.album("A", "Artist", "Album", 3);
    env.album("B", "Other", "Another", 2);
    REQUIRE(env.scan().added == 5);

    const fs::path renamed = env.dir.path() / "music_gone";
    fs::rename(env.music, renamed);
    const ScanSummary gone = env.scan();
    CHECK(gone.unavailableFolders.size() == 1);
    CHECK(gone.markedMissing == 5);
    CHECK(gone.added == 0);
    CHECK(env.lib.counts().tracks == 0);
    CHECK(env.lib.counts().missing == 5);
    CHECK(env.lib.counts().albums == 0);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM tracks") == 5);  // nothing deleted

    fs::rename(renamed, env.music);
    const ScanSummary back = env.scan();
    CHECK(back.unavailableFolders.empty());
    CHECK(back.restored == 5);
    CHECK(back.added == 0);
    CHECK(env.lib.counts().tracks == 5);
    CHECK(env.lib.counts().albums == 2);
}

TEST_CASE("A moved album folder keeps its track ids, plays and overrides", "[scan][move]") {
    Env env;
    env.album("Old Name", "Artist", "Album", 3);
    env.album("Stay", "Other", "Untouched", 2);
    REQUIRE(env.scan().added == 5);

    std::map<int, std::int64_t> idByTrack;
    for (int i = 1; i <= 3; ++i) {
        idByTrack[i] = env.trackId("Old Name/" + std::to_string(i) + ".wav");
        REQUIRE(idByTrack[i] > 0);
    }
    env.lib.db().exec("INSERT INTO plays(track_id, started_at, played_ms, completed) VALUES (" +
                      std::to_string(idByTrack[2]) + ", 100, 5000, 1)");
    env.lib.db().exec("INSERT INTO tag_overrides(track_id, field, value, source, updated_at) VALUES (" +
                      std::to_string(idByTrack[3]) + ", 'title', 'Kept', 'manual', 1)");

    fs::rename(env.music / "Old Name", env.music / "New Name");
    const ScanSummary moved = env.scan();
    CHECK(moved.moved == 3);
    CHECK(moved.markedMissing == 3);  // they were gone for a moment, then re-matched
    CHECK(moved.unchanged == 2);

    for (int i = 1; i <= 3; ++i) {
        CHECK(env.trackId("New Name/" + std::to_string(i) + ".wav") == idByTrack[i]);
    }
    CHECK(env.trackId("Old Name/1.wav") == -1);
    const LibraryCounts c = env.lib.counts();
    CHECK(c.tracks == 5);
    CHECK(c.missing == 0);
    CHECK(c.albums == 2);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM tracks") == 5);  // no duplicate rows
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM plays WHERE track_id = " + std::to_string(idByTrack[2])) == 1);
    Statement eff = env.lib.db().prepare("SELECT title FROM v_tracks WHERE id = ?1");
    eff.bind(1, idByTrack[3]);
    REQUIRE(eff.step());
    CHECK(eff.getText(0) == "Kept");
}

TEST_CASE("A whole library folder moved elsewhere is re-matched when the new folder is added", "[scan][move]") {
    Env env;
    env.album("A", "Artist", "Album", 3);
    REQUIRE(env.scan().added == 3);
    const auto firstId = env.trackId("A/1.wav");

    // The user removes the old watched folder (tracks go missing) and adds the new location.
    REQUIRE(env.lib.removeFolder(mp::core::genericUtf8(env.music)));
    CHECK(env.lib.counts().missing == 0);  // removed on purpose: not "missing"
    CHECK(env.lib.counts().unwatched == 3);
    const fs::path newMusic = env.dir.path() / "new_place";
    fs::rename(env.music, newMusic);
    REQUIRE(env.lib.addFolder(newMusic).ok);

    const ScanSummary s = env.scan();
    CHECK(s.moved == 3);
    CHECK(env.lib.counts().tracks == 3);
    CHECK(env.lib.counts().missing == 0);
    CHECK(env.lib.counts().unwatched == 0);
    Statement st = env.lib.db().prepare("SELECT path FROM tracks WHERE id = ?1");
    st.bind(1, firstId);
    REQUIRE(st.step());
    CHECK(st.getText(0) == mp::core::genericUtf8(newMusic / "A" / "1.wav"));
}

TEST_CASE("Two missing candidates are ambiguous: the new file stays a new track", "[scan][move]") {
    Env env;
    // Two albums with the same name + track numbers + durations, e.g. two pressings.
    TagSpec t;
    t.title = "Song";
    t.artist = "Artist";
    t.album = "Same Album";
    t.track = "1";
    makeTrack(env.music / "pressing1" / "1.wav", t);
    makeTrack(env.music / "pressing2" / "1.wav", t);
    REQUIRE(env.scan().added == 2);

    fs::remove_all(env.music / "pressing1");
    fs::remove_all(env.music / "pressing2");
    REQUIRE(env.scan().markedMissing == 2);

    makeTrack(env.music / "pressing3" / "1.wav", t);
    const ScanSummary s = env.scan();
    CHECK(s.moved == 0);
    CHECK(s.added == 1);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM tracks") == 3);
}

TEST_CASE("Tracks by different artists without an album artist form a compilation", "[scan][albums]") {
    Env env;
    for (int i = 1; i <= 3; ++i) {
        TagSpec t;
        t.title = "Song " + std::to_string(i);
        t.artist = "Artist " + std::to_string(i);
        t.album = "Now That's Music";
        t.track = std::to_string(i);
        makeTrack(env.music / "Comp" / (std::to_string(i) + ".wav"), t);
    }
    env.album("Solo", "Single Artist", "One Artist Album", 3);  // no album artist tag either
    REQUIRE(env.scan().added == 6);

    CHECK(env.lib.counts().albums == 2);
    Statement s = env.lib.db().prepare("SELECT album_artist FROM albums WHERE title = ?1");
    s.bind(1, "Now That's Music");
    REQUIRE(s.step());
    CHECK(s.getText(0) == "Various Artists");
    s.reset();
    s.bind(1, "One Artist Album");
    REQUIRE(s.step());
    CHECK(s.getText(0) == "Single Artist");
}

TEST_CASE("Adding a track by another artist turns a one-artist album into a compilation", "[scan][albums]") {
    Env env;
    env.album("Mix", "Artist 1", "Mixtape", 2);
    REQUIRE(env.scan().added == 2);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM albums WHERE album_artist = 'Artist 1'") == 1);

    TagSpec t;
    t.title = "Guest";
    t.artist = "Artist 2";
    t.album = "Mixtape";
    t.track = "3";
    makeTrack(env.music / "Mix" / "3.wav", t);
    REQUIRE(env.scan().added == 1);

    CHECK(env.lib.counts().albums == 1);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM albums WHERE album_artist = 'Various Artists'") == 1);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(DISTINCT album_id) FROM tracks") == 1);
}

TEST_CASE("Artist and album names that differ only in case or spacing are merged", "[scan][albums]") {
    Env env;
    TagSpec a;
    a.title = "One";
    a.artist = "BJÖRK";
    a.albumArtist = "BJÖRK";
    a.album = "Homogenic";
    a.track = "1";
    TagSpec b = a;
    b.title = "Two";
    b.artist = "Björk ";
    b.albumArtist = "Björk";
    b.album = "homogenic";
    b.track = "2";
    makeTrack(env.music / "1.wav", a);
    makeTrack(env.music / "2.wav", b);
    REQUIRE(env.scan().added == 2);
    CHECK(env.lib.counts().albums == 1);
    CHECK(env.lib.counts().artists == 1);
}

TEST_CASE("A track without an album tag gets no album; one without tags is still stored", "[scan][albums]") {
    Env env;
    writeSilentWav(env.music / "loose" / "just a file.wav");
    REQUIRE(env.scan().added == 1);
    CHECK(env.lib.counts().tracks == 1);
    CHECK(env.lib.counts().albums == 0);
    Statement s = env.lib.db().prepare("SELECT title, album_id, artist_id FROM v_tracks");
    REQUIRE(s.step());
    CHECK(s.getText(0) == "just a file");
    CHECK(s.isNull(1));
    CHECK(s.isNull(2));
}

TEST_CASE("Retagging an album leaves no empty album or unused artist behind", "[scan][albums]") {
    Env env;
    env.album("A", "Old Artist", "Old Title", 1);
    REQUIRE(env.scan().added == 1);

    TagSpec t;
    t.title = "Song";
    t.artist = "New Artist";
    t.album = "New Title";
    t.track = "1";
    tagFile(env.music / "A" / "1.wav", t);
    touchLater(env.music / "A" / "1.wav");
    REQUIRE(env.scan().updated == 1);

    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM albums") == 1);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM albums WHERE title = 'New Title'") == 1);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM artists") == 1);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM artists WHERE name = 'New Artist'") == 1);
}

TEST_CASE("A file that can't be read is reported and retried, and doesn't stop the scan", "[scan][errors]") {
    Env env;
    env.album("A", "Artist", "Album", 2);
    {
        std::ofstream(env.music / "A" / "broken.flac", std::ios::binary) << "not audio";
    }
    const ScanSummary first = env.scan();
    CHECK(first.added == 2);
    CHECK(first.failed == 1);
    REQUIRE(first.errors.size() == 1);
    CHECK(first.errors.front().find("broken.flac") != std::string::npos);
    CHECK(env.lib.counts().tracks == 2);

    CHECK(env.scan().failed == 1);  // not stored, so it is tried again
}

TEST_CASE("Non-audio files and other extensions are ignored", "[scan]") {
    Env env;
    env.album("A", "Artist", "Album", 1);
    for (const char* name : {"cover.jpg", "notes.txt", "album.cue", "A/folder.png"}) {
        fs::create_directories((env.music / name).parent_path());
        std::ofstream(env.music / name) << "x";
    }
    const ScanSummary s = env.scan();
    CHECK(s.filesSeen == 1);
    CHECK(s.failed == 0);
}

TEST_CASE("Folders and files with non-ASCII names scan fine", "[scan][unicode]") {
    Env env;
    TagSpec t;
    t.title = "Jóga";
    t.artist = "Björk";
    t.album = "Homogenic";
    t.track = "1";
    const auto folder = mp::core::pathFromUtf8("Bj\xC3\xB6rk \xE2\x80\x93 Homogenic");
    makeTrack(env.music / folder / mp::core::pathFromUtf8("01 J\xC3\xB3ga.wav"), t);
    REQUIRE(env.scan().added == 1);
    CHECK(env.scan().unchanged == 1);
    Statement s = env.lib.db().prepare("SELECT path FROM tracks");
    REQUIRE(s.step());
    CHECK(s.getText(0).find("Bj\xC3\xB6rk") != std::string::npos);
}

TEST_CASE("Removing a watched folder keeps its tracks as missing", "[scan][folders]") {
    Env env;
    env.album("A", "Artist", "Album", 2);
    REQUIRE(env.scan().added == 2);
    const auto id = env.trackId("A/1.wav");
    env.lib.db().exec("INSERT INTO plays(track_id, started_at, played_ms, completed) VALUES (" + std::to_string(id) +
                      ", 1, 1, 1)");

    REQUIRE(env.lib.removeFolder(mp::core::genericUtf8(env.music)));
    CHECK(env.lib.folders().empty());
    const LibraryCounts c = env.lib.counts();
    CHECK(c.tracks == 0);
    CHECK(c.missing == 0);
    CHECK(c.unwatched == 2);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM plays") == 1);

    // With no watched folders a scan does nothing and doesn't touch what is stored.
    const ScanSummary s = env.scan();
    CHECK(s.filesSeen == 0);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM tracks") == 2);
}

TEST_CASE("Cancelling stops the scan, keeps what was stored, and the next scan finishes the job", "[scan][cancel]") {
    ScanOptions options;
    options.workerThreads = 2;
    options.batchSize = 10;
    options.progressInterval = std::chrono::milliseconds(0);  // a callback on every step, so we can cancel mid-scan
    Env env(options);
    constexpr int kFiles = 300;
    for (int i = 0; i < kFiles; ++i) {
        TagSpec t;
        t.title = "T" + std::to_string(i);
        t.artist = "Artist";
        t.album = "Album " + std::to_string(i / 50);
        t.track = std::to_string(i % 50 + 1);
        makeTrack(env.music / ("d" + std::to_string(i / 50)) / (std::to_string(i) + ".wav"), t, 441);
    }

    ScanSummary cancelled;
    std::atomic<bool> finished{false};
    REQUIRE(env.scanner.start(
        [&](const ScanProgress& p) {
            if (p.filesFound >= 20) {
                env.scanner.cancel();
            }
        },
        [&](const ScanSummary& s) {
            cancelled = s;
            finished = true;
        }));
    env.scanner.wait();
    REQUIRE(finished);
    CHECK(cancelled.cancelled);
    CHECK(cancelled.fatalError.empty());
    CHECK(cancelled.markedMissing == 0);
    const auto stored = env.lib.counts().tracks;
    CHECK(stored < kFiles);
    // What was stored is consistent: every stored track already has its album.
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM tracks WHERE resolved = 0") == 0);

    const ScanSummary rest = env.scan();
    CHECK_FALSE(rest.cancelled);
    CHECK(rest.fatalError.empty());
    CHECK(env.lib.counts().tracks == kFiles);
    CHECK(env.lib.counts().albums == 6);
    CHECK(rest.added + static_cast<std::uint64_t>(stored) == kFiles);
}

TEST_CASE("Scanner can be started again after a scan, but not while one runs", "[scan]") {
    Env env;
    env.album("A", "Artist", "Album", 2);
    std::atomic<bool> release{false};
    std::atomic<bool> finished{false};
    REQUIRE(env.scanner.start(
        [&](const ScanProgress&) {
            while (!release) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        },
        [&](const ScanSummary&) { finished = true; }));
    CHECK(env.scanner.running());
    CHECK_FALSE(env.scanner.start({}, {}));
    release = true;
    env.scanner.wait();
    CHECK(finished);
    CHECK_FALSE(env.scanner.running());
    CHECK(env.scan().unchanged == 2);
}

TEST_CASE("Perf: scan and rescan of a big library", "[.perf]") {
    ScanOptions options;
    Env env(options);
    // MP_PERF_FILES=50000 for a library-sized run (SPEC §3: 50k tracks).
    const char* fromEnv = std::getenv("MP_PERF_FILES");
    const int kFiles = fromEnv != nullptr ? std::atoi(fromEnv) : 3000;
    for (int i = 0; i < kFiles; ++i) {
        TagSpec t;
        t.title = "T" + std::to_string(i);
        t.artist = "Artist " + std::to_string(i % 100);
        t.album = "Album " + std::to_string(i / 12);
        t.track = std::to_string(i % 12 + 1);
        makeTrack(env.music / ("d" + std::to_string(i / 12)) / (std::to_string(i) + ".wav"), t, 441);
    }
    const ScanSummary first = env.scan();
    WARN("first scan: " << first.elapsedMs << " ms for " << first.added << " files");
    const ScanSummary second = env.scan();
    WARN("rescan, nothing changed: " << second.elapsedMs << " ms for " << second.unchanged << " files");
    CHECK(first.added == static_cast<std::uint64_t>(kFiles));
    CHECK(second.unchanged == static_cast<std::uint64_t>(kFiles));
    const auto t0 = std::chrono::steady_clock::now();
    const LibraryCounts c = env.lib.counts();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    WARN("counts(): " << ms << " ms -> " << c.tracks << " tracks, " << c.albums << " albums, " << c.artists << " artists");
}
