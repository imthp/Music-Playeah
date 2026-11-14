#include <catch2/catch_test_macros.hpp>

#include "core/format.hpp"

using mp::core::formatDuration;

TEST_CASE("formatDuration shows minutes and seconds", "[format]") {
    CHECK(formatDuration(0) == "0:00");
    CHECK(formatDuration(7'000) == "0:07");
    CHECK(formatDuration(187'000) == "3:07");
    CHECK(formatDuration(599'000) == "9:59");
}

TEST_CASE("formatDuration adds hours only when needed", "[format]") {
    CHECK(formatDuration(3'599'000) == "59:59");
    CHECK(formatDuration(3'600'000) == "1:00:00");
    CHECK(formatDuration(3'723'000) == "1:02:03");
}

TEST_CASE("formatDuration truncates milliseconds and clamps negatives", "[format]") {
    CHECK(formatDuration(1'999) == "0:01");
    CHECK(formatDuration(-5'000) == "0:00");
}

using mp::core::formatBadge;
using mp::core::formatKilohertz;
using mp::core::audio::TrackInfo;

TEST_CASE("formatKilohertz drops trailing zeros", "[format]") {
    CHECK(formatKilohertz(44'100) == "44.1");
    CHECK(formatKilohertz(48'000) == "48");
    CHECK(formatKilohertz(88'200) == "88.2");
    CHECK(formatKilohertz(96'000) == "96");
    CHECK(formatKilohertz(352'800) == "352.8");
}

TEST_CASE("formatBadge shows depth/rate for lossless, bitrate for lossy", "[format]") {
    TrackInfo flac{.codec = "FLAC", .sampleRate = 96'000, .bitDepth = 24, .channels = 2, .lossless = true};
    CHECK(formatBadge(flac) == "FLAC 24/96");

    TrackInfo wav{.codec = "WAV", .sampleRate = 44'100, .bitDepth = 16, .channels = 2, .lossless = true};
    CHECK(formatBadge(wav) == "WAV 16/44.1");

    TrackInfo mp3{.codec = "MP3", .sampleRate = 44'100, .channels = 2, .bitrateKbps = 320};
    CHECK(formatBadge(mp3) == "MP3 320");

    TrackInfo opus{.codec = "Opus", .sampleRate = 48'000, .channels = 2};
    CHECK(formatBadge(opus) == "Opus");

    TrackInfo dsd{.codec = "DSD", .sampleRate = 2'822'400, .channels = 2, .lossless = true, .dsd = true};
    CHECK(formatBadge(dsd) == "DSD64");

    CHECK(formatBadge(TrackInfo{}).empty());
}
