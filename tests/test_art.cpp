// Album art discovery (milestone 3b): embedded picture, cover files, hashing, rescans.
#include <catch2/catch_test_macros.hpp>

#include <taglib/attachedpictureframe.h>
#include <taglib/id3v2tag.h>
#include <taglib/wavfile.h>

#include <fstream>

#include "core/library/art.hpp"
#include "core/library/library.hpp"
#include "core/library/scanner.hpp"
#include "library_fixtures.hpp"

using namespace mp::core::library;
using namespace mp::test;

namespace {

// Not a decodable image, but it has a JPEG signature, which is all the discovery code looks at.
Bytes fakeJpeg(std::uint8_t seed) {
    Bytes b = {0xFF, 0xD8, 0xFF, 0xE0, seed, 1, 2, 3, 4, 5, 6, 7, 8};
    return b;
}

void embed(const fs::path& file, const Bytes& image, TagLib::ID3v2::AttachedPictureFrame::Type type =
                                                          TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
    TagLib::RIFF::WAV::File w(file.c_str());
    auto* frame = new TagLib::ID3v2::AttachedPictureFrame();
    frame->setMimeType("image/jpeg");
    frame->setType(type);
    frame->setPicture(TagLib::ByteVector(reinterpret_cast<const char*>(image.data()), static_cast<unsigned>(image.size())));
    w.ID3v2Tag()->addFrame(frame);
    REQUIRE(w.save());
}

void writeFile(const fs::path& path, const Bytes& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

TagSpec spec(const std::string& album, int no) {
    TagSpec t;
    t.title = "Track " + std::to_string(no);
    t.artist = "Artist";
    t.album = album;
    t.track = std::to_string(no);
    return t;
}

struct Env {
    TempDir dir;
    fs::path music = dir.path() / "music";
    Library lib;
    Scanner scanner;
    Env() : lib(dir.path() / "app" / "library.db"), scanner(lib.dbFile()) {
        fs::create_directories(music);
        REQUIRE(lib.addFolder(music).ok);
    }
    std::pair<std::string, std::string> art(const std::string& album) {
        Statement s = lib.db().prepare("SELECT COALESCE(art_hash, ''), COALESCE(art_source, '') FROM albums WHERE title = ?1");
        s.bind(1, album);
        REQUIRE(s.step());
        return {s.getText(0), s.getText(1)};
    }
};

}  // namespace

TEST_CASE("art: helpers", "[art]") {
    CHECK(hashImage(fakeJpeg(1)) == hashImage(fakeJpeg(1)));
    CHECK(hashImage(fakeJpeg(1)) != hashImage(fakeJpeg(2)));
    CHECK(hashImage(fakeJpeg(1)).size() == 16);
    CHECK(hashImage({}) == "cbf29ce484222325");  // FNV-1a offset basis: pins the algorithm (cache keys on disk)
    CHECK(imageExtension(fakeJpeg(0)) == "jpg");
    CHECK(imageExtension({0x89, 'P', 'N', 'G', 0, 0}) == "png");
    CHECK(imageExtension({1, 2, 3}).empty());
}

TEST_CASE("art: embedded picture wins, then cover files, else none", "[art][scan]") {
    Env env;
    // A: embedded picture.
    makeTrack(env.music / "A/1.wav", spec("Embedded", 1));
    embed(env.music / "A/1.wav", fakeJpeg(1));
    // B: no embedded picture, cover.jpg beside the files (upper case name: Windows is case-insensitive, others not).
    makeTrack(env.music / "B/1.wav", spec("CoverFile", 1));
    writeFile(env.music / "B/Cover.JPG", fakeJpeg(2));
    // C: both: the embedded picture is preferred.
    makeTrack(env.music / "C/1.wav", spec("Both", 1));
    embed(env.music / "C/1.wav", fakeJpeg(3));
    writeFile(env.music / "C/cover.jpg", fakeJpeg(4));
    // D: nothing.
    makeTrack(env.music / "D/1.wav", spec("Bare", 1));
    writeFile(env.music / "D/notes.txt", {'h', 'i'});

    const ScanSummary summary = scanAndWait(env.scanner);
    REQUIRE(summary.fatalError.empty());

    const auto [hashA, srcA] = env.art("Embedded");
    CHECK(hashA == hashImage(fakeJpeg(1)));
    CHECK(srcA.rfind("embedded:", 0) == 0);
    CHECK(env.art("CoverFile").first == hashImage(fakeJpeg(2)));
    CHECK(env.art("CoverFile").second.rfind("file:", 0) == 0);
    CHECK(env.art("Both").first == hashImage(fakeJpeg(3)));
    CHECK(env.art("Bare").first.empty());
    CHECK(env.art("Bare").second.empty());
    CHECK(env.lib.db().scalarInt("SELECT COUNT(*) FROM albums WHERE art_checked = 0") == 0);

    // The stored source can be loaded again and has the same bytes.
    CHECK(loadArtSource(srcA) == fakeJpeg(1));
    CHECK(loadArtSource(env.art("CoverFile").second) == fakeJpeg(2));
    CHECK(loadArtSource("embedded:/does/not/exist.flac").empty());
}

TEST_CASE("art: a front cover is preferred over other picture types", "[art]") {
    TempDir dir;
    const fs::path f = dir.path() / "t.wav";
    makeTrack(f, spec("X", 1));
    embed(f, fakeJpeg(9), TagLib::ID3v2::AttachedPictureFrame::BackCover);
    embed(f, fakeJpeg(7), TagLib::ID3v2::AttachedPictureFrame::FrontCover);
    CHECK(readEmbeddedPicture(f) == fakeJpeg(7));
}

TEST_CASE("art: the same image on two albums has one hash; rescan only rechecks changed albums", "[art][scan]") {
    Env env;
    for (const char* album : {"One", "Two"}) {
        const fs::path file = env.music / album / "1.wav";
        makeTrack(file, spec(album, 1));
        embed(file, fakeJpeg(5));
    }
    REQUIRE(scanAndWait(env.scanner).fatalError.empty());
    CHECK(env.art("One").first == env.art("Two").first);
    CHECK(env.lib.db().scalarInt("SELECT COUNT(DISTINCT art_hash) FROM albums") == 1);

    // Unchanged rescan: art is not looked at again (art_checked stays 1)...
    env.lib.db().exec("UPDATE albums SET art_source = 'marker' WHERE title = 'One'");
    REQUIRE(scanAndWait(env.scanner).fatalError.empty());
    CHECK(env.art("One").second == "marker");

    // ...but a changed file resets its album, which then gets its (new) cover.
    const fs::path changed = env.music / "One/1.wav";
    {
        TagLib::RIFF::WAV::File w(changed.c_str());
        w.ID3v2Tag()->removeFrames("APIC");
        REQUIRE(w.save());
    }
    embed(changed, fakeJpeg(6));
    touchLater(changed);
    REQUIRE(scanAndWait(env.scanner).fatalError.empty());
    CHECK(env.art("One").first == hashImage(fakeJpeg(6)));
    CHECK(env.art("Two").first == hashImage(fakeJpeg(5)));
}
