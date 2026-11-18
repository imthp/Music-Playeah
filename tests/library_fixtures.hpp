#pragma once

// Test helpers for the library tests: a temp folder that cleans up after itself, tiny real WAV
// files, and tagging through TagLib (so the tests exercise the same reader the scanner uses).
#include <catch2/catch_test_macros.hpp>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/library/scanner.hpp"
#include "core/text.hpp"

namespace mp::test {

namespace fs = std::filesystem;

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("mp_lib_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

struct TagSpec {
    std::string title;
    std::string artist;
    std::string albumArtist;
    std::string album;
    std::string composer;
    std::string genre;
    std::string work;
    std::string movementName;
    std::string movementNumber;  // as written in the tag: "2" or "2/4"
    std::string track;           // "3" or "3/12"
    std::string disc;            // "1" or "1/2"
    std::string date;            // "2019" or "2019-05-03"
    std::string gainTrack;       // "-6.54 dB"
    std::string gainAlbum;
    std::vector<std::string> performers;  // PERFORMER values, "Name (instrument)"
    std::string conductor;
};

template <typename T>
void putLe(std::ofstream& f, T v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

// A silent 16-bit stereo 44.1 kHz WAV of `frames` frames (4,410 = 100 ms).
inline void writeSilentWav(const fs::path& path, std::uint32_t frames = 4410) {
    // Windows' plain file APIs stop at ~260 characters; the long-path tests need the extended form.
    const fs::path target = core::longPathSafe(path);
    fs::create_directories(target.parent_path());
    std::ofstream f(target, std::ios::binary);
    const std::uint32_t dataBytes = frames * 4;
    f.write("RIFF", 4);
    putLe<std::uint32_t>(f, 36 + dataBytes);
    f.write("WAVEfmt ", 8);
    putLe<std::uint32_t>(f, 16);
    putLe<std::uint16_t>(f, 1);
    putLe<std::uint16_t>(f, 2);
    putLe<std::uint32_t>(f, 44100);
    putLe<std::uint32_t>(f, 44100 * 4);
    putLe<std::uint16_t>(f, 4);
    putLe<std::uint16_t>(f, 16);
    f.write("data", 4);
    putLe<std::uint32_t>(f, dataBytes);
    const std::vector<char> zeros(dataBytes, 0);
    f.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
}

inline void tagFile(const fs::path& path, const TagSpec& t) {
    const fs::path openable = core::longPathSafe(path);
    TagLib::FileRef ref(openable.c_str());
    REQUIRE(!ref.isNull());
    TagLib::PropertyMap props = ref.file()->properties();
    auto set = [&](const char* key, const std::string& value) {
        if (!value.empty()) {
            props.replace(key, TagLib::StringList(TagLib::String(value, TagLib::String::UTF8)));
        }
    };
    set("TITLE", t.title);
    set("ARTIST", t.artist);
    set("ALBUMARTIST", t.albumArtist);
    set("ALBUM", t.album);
    set("COMPOSER", t.composer);
    set("GENRE", t.genre);
    set("WORK", t.work);
    set("MOVEMENTNAME", t.movementName);
    set("MOVEMENTNUMBER", t.movementNumber);
    set("TRACKNUMBER", t.track);
    set("DISCNUMBER", t.disc);
    set("DATE", t.date);
    set("REPLAYGAIN_TRACK_GAIN", t.gainTrack);
    set("REPLAYGAIN_ALBUM_GAIN", t.gainAlbum);
    set("CONDUCTOR", t.conductor);
    if (!t.performers.empty()) {
        TagLib::StringList list;
        for (const std::string& p : t.performers) {
            list.append(TagLib::String(p, TagLib::String::UTF8));
        }
        props.replace("PERFORMER", list);
    }
    ref.file()->setProperties(props);
    REQUIRE(ref.save());
}

inline fs::path makeTrack(const fs::path& path, const TagSpec& tags, std::uint32_t frames = 4410) {
    writeSilentWav(path, frames);
    tagFile(path, tags);
    return path;
}

// Pushes a file's mtime into the future so a rescan surely sees it as changed, whatever the
// file system's timestamp resolution.
inline void touchLater(const fs::path& path, int seconds = 60) {
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(seconds));
}

// Runs a scan to the end. Returns the summary; `progressCalls` (optional) counts callbacks.
inline core::library::ScanSummary scanAndWait(core::library::Scanner& scanner, std::atomic<int>* progressCalls = nullptr) {
    core::library::ScanSummary result;
    std::atomic<bool> finished{false};
    const bool started = scanner.start(
        [progressCalls](const core::library::ScanProgress&) {
            if (progressCalls != nullptr) {
                ++*progressCalls;
            }
        },
        [&](const core::library::ScanSummary& s) {
            result = s;
            finished = true;
        });
    REQUIRE(started);
    scanner.wait();
    REQUIRE(finished);
    return result;
}

}  // namespace mp::test
