#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fstream>

#include "core/library/tag_reader.hpp"
#include "library_fixtures.hpp"

using namespace mp::core::library;
using namespace mp::test;
using Catch::Matchers::WithinAbs;

TEST_CASE("Parsers for tag values", "[tags]") {
    CHECK(parseLeadingInt("3/12") == 3);
    CHECK(parseLeadingInt(" 07") == 7);
    CHECK(parseLeadingInt("A") == 0);
    CHECK(parseLeadingInt("") == 0);
    CHECK(parseTotalAfterSlash("3/12") == 12);
    CHECK(parseTotalAfterSlash("3") == 0);

    CHECK(parseYear("2019") == 2019);
    CHECK(parseYear("2019-05-03") == 2019);
    CHECK(parseYear("May 1974") == 1974);
    CHECK(parseYear("12345") == 0);  // not a year: five digits
    CHECK(parseYear("") == 0);

    REQUIRE(parseGainDb("-6.54 dB").has_value());
    CHECK_THAT(*parseGainDb("-6.54 dB"), WithinAbs(-6.54, 1e-9));
    CHECK_THAT(*parseGainDb("+1.2dB"), WithinAbs(1.2, 1e-9));
    CHECK_THAT(*parseGainDb("0"), WithinAbs(0.0, 1e-9));
    CHECK_FALSE(parseGainDb("loud").has_value());
    CHECK_FALSE(parseGainDb("").has_value());
}

TEST_CASE("isLibraryFile accepts the SPEC formats, case-insensitively", "[tags]") {
    for (const char* name : {"a.flac", "a.FLAC", "a.mp3", "a.m4a", "a.dsf", "a.dff", "a.wav", "a.aif", "a.aiff",
                             "a.ogg", "a.oga", "a.opus", "dir/Song.Mp3"}) {
        CHECK(isLibraryFile(name));
    }
    for (const char* name : {"a.jpg", "a.txt", "a.cue", "a", "flac", "a.flac.bak"}) {
        CHECK_FALSE(isLibraryFile(name));
    }
}

TEST_CASE("readTags reads every field M3a needs", "[tags]") {
    TempDir dir;
    TagSpec spec;
    spec.title = "Allemande";
    spec.artist = "Göran Söllscher";
    spec.albumArtist = "Göran Söllscher";
    spec.album = "Bach: Lute Suites";
    spec.composer = "Johann Sebastian Bach";
    spec.genre = "Classical";
    spec.work = "Suite in E-flat major, BWV 998";
    spec.movementName = "Allemande";
    spec.movementNumber = "2/4";
    spec.track = "3/12";
    spec.disc = "1/2";
    spec.date = "1989-05-03";
    spec.gainTrack = "-6.54 dB";
    spec.gainAlbum = "-7.10 dB";
    const auto file = makeTrack(dir.path() / "bach.wav", spec, 44100);  // one second

    FileTags t;
    std::string error;
    REQUIRE(readTags(file, t, error));
    CHECK(t.title == "Allemande");
    CHECK(t.artist == "Göran Söllscher");
    CHECK(t.albumArtist == "Göran Söllscher");
    CHECK(t.album == "Bach: Lute Suites");
    CHECK(t.composer == "Johann Sebastian Bach");
    CHECK(t.genre == "Classical");
    CHECK(t.workTag == "Suite in E-flat major, BWV 998");
    CHECK(t.movementName == "Allemande");
    CHECK(t.movementNo == 2);
    CHECK(t.movementTotal == 4);
    CHECK(t.trackNo == 3);
    CHECK(t.discNo == 1);
    CHECK(t.year == 1989);
    REQUIRE(t.rgTrackGain.has_value());
    REQUIRE(t.rgAlbumGain.has_value());
    CHECK_THAT(*t.rgTrackGain, WithinAbs(-6.54, 1e-9));
    CHECK_THAT(*t.rgAlbumGain, WithinAbs(-7.10, 1e-9));

    CHECK(t.codec == "WAV");
    CHECK(t.sampleRate == 44100);
    CHECK(t.bitDepth == 16);
    CHECK(t.channels == 2);
    CHECK(t.durationMs >= 990);
    CHECK(t.durationMs <= 1010);
}

TEST_CASE("readTags: an untagged file gets its file name as title", "[tags]") {
    TempDir dir;
    const auto file = dir.path() / "08 - Untitled Jam.wav";
    writeSilentWav(file);
    FileTags t;
    std::string error;
    REQUIRE(readTags(file, t, error));
    CHECK(t.title == "08 - Untitled Jam");
    CHECK(t.artist.empty());
    CHECK(t.album.empty());
    CHECK(t.trackNo == 0);
    CHECK_FALSE(t.rgTrackGain.has_value());
}

TEST_CASE("readTags handles non-ASCII file names", "[tags]") {
    TempDir dir;
    const auto file = dir.path() / mp::core::pathFromUtf8("Bj\xC3\xB6rk \xE2\x80\x93 J\xC3\xB3ga.wav");
    TagSpec spec;
    spec.title = "Jóga";
    makeTrack(file, spec);
    FileTags t;
    std::string error;
    REQUIRE(readTags(file, t, error));
    CHECK(t.title == "Jóga");
}

TEST_CASE("readTags reports a corrupt file instead of throwing", "[tags]") {
    TempDir dir;
    const auto file = dir.path() / "broken.flac";
    std::ofstream(file, std::ios::binary) << "this is not a flac file";
    FileTags t;
    std::string error;
    CHECK_FALSE(readTags(file, t, error));
    CHECK_FALSE(error.empty());

    CHECK_FALSE(readTags(dir.path() / "missing.flac", t, error));
}

TEST_CASE("readTags opens files whose full path is over 260 characters (Windows MAX_PATH)", "[tags][longpath]") {
    TempDir dir;
    // Three folders of ~90 characters plus a long file name: about 300 characters in total.
    const std::string folder(90, 'a');
    const auto file = dir.path() / (folder + "1") / (folder + "2") / (folder + "3") /
                      "01 A rather long track title (Arr. Somebody, Somebody Else, And Another One).wav";
    REQUIRE(file.native().size() > 260);
    TagSpec spec;
    spec.title = "Long";
    makeTrack(file, spec);

    FileTags t;
    std::string error;
    const bool ok = readTags(file, t, error);
    INFO(error);
    REQUIRE(ok);
    CHECK(t.title == "Long");

    // Best-effort cleanup: remove_all on such a deep tree can fail on Windows without the long form.
    std::error_code ec;
    fs::remove(mp::core::longPathSafe(file), ec);
    for (auto dirPath = file.parent_path(); dirPath != dir.path(); dirPath = dirPath.parent_path()) {
        fs::remove(mp::core::longPathSafe(dirPath), ec);
    }
}

TEST_CASE("longPathSafe leaves normal paths alone", "[tags][longpath]") {
    CHECK(mp::core::longPathSafe("a/b/c.flac") == std::filesystem::path("a/b/c.flac"));
    CHECK(mp::core::longPathSafe("") == std::filesystem::path(""));
}
