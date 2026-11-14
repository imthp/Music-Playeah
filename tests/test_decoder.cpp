#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/audio/decoder.hpp"
#include "wav_writer.hpp"

using mp::core::audio::Decoder;
using namespace mp::test;
namespace fs = std::filesystem;

namespace {

std::vector<float> decodeAll(Decoder& d) {
    std::vector<float> all;
    std::vector<float> buf(1000 * 2);
    for (;;) {
        const std::size_t n = d.read(buf.data(), 1000);
        all.insert(all.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * 2));
        if (n < 1000) {
            CHECK(d.finished());
            return all;
        }
    }
}

bool matchesSignal(const std::vector<float>& decoded, std::size_t firstFrame) {
    for (std::size_t i = 0; i < decoded.size() / 2; ++i) {
        if (decoded[2 * i] != asFloat(sampleAt(firstFrame + i, 0)) ||
            decoded[2 * i + 1] != asFloat(sampleAt(firstFrame + i, 1))) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Decoder reads a WAV file sample-exactly at its own rate", "[decoder]") {
    constexpr std::size_t kFrames = 44'100 + 123;
    const auto path = writeWav("exact", 0, kFrames, 44'100);
    std::string error;
    auto d = Decoder::open(path.string(), 44'100, 2, error);
    REQUIRE(d);

    CHECK(d->info().codec == "WAV");
    CHECK(d->info().lossless);
    CHECK(d->info().bitDepth == 16);
    CHECK(d->info().sampleRate == 44'100);
    CHECK(d->info().channels == 2);
    CHECK(d->info().durationMs >= 1002);  // 1002.8 ms; FFmpeg rounds
    CHECK(d->info().durationMs <= 1003);

    const auto pcm = decodeAll(*d);
    CHECK(pcm.size() == kFrames * 2);
    CHECK(matchesSignal(pcm, 0));
    d.reset();
    fs::remove(path);
}

TEST_CASE("Two halves of a recording decode back to back with no gap or overlap", "[decoder][gapless]") {
    // This is the gapless guarantee at the decoder level: track A's last sample is followed
    // directly by track B's first one. The engine writes them into the ring in exactly this order.
    constexpr std::size_t kSplit = 30'011;
    constexpr std::size_t kTotal = 70'000;
    const auto a = writeWav("half_a", 0, kSplit, 44'100);
    const auto b = writeWav("half_b", kSplit, kTotal - kSplit, 44'100);

    std::string error;
    auto da = Decoder::open(a.string(), 44'100, 2, error);
    auto db = Decoder::open(b.string(), 44'100, 2, error);
    REQUIRE(da);
    REQUIRE(db);
    auto joined = decodeAll(*da);
    const auto second = decodeAll(*db);
    joined.insert(joined.end(), second.begin(), second.end());

    CHECK(joined.size() == kTotal * 2);
    CHECK(matchesSignal(joined, 0));
    da.reset();
    db.reset();
    fs::remove(a);
    fs::remove(b);
}

TEST_CASE("Resampling to the device rate keeps the length", "[decoder]") {
    constexpr std::size_t kFrames = 44'100 * 2;
    const auto path = writeWav("resample", 0, kFrames, 44'100);
    std::string error;
    auto d = Decoder::open(path.string(), 48'000, 2, error);
    REQUIRE(d);
    const auto pcm = decodeAll(*d);
    const auto frames = static_cast<long long>(pcm.size() / 2);
    CHECK(std::llabs(frames - 96'000) <= 2);  // 2 s at 48 kHz, give or take filter rounding
    d.reset();
    fs::remove(path);
}

TEST_CASE("Seek lands on the exact sample", "[decoder]") {
    constexpr std::size_t kFrames = 44'100 * 3;
    const auto path = writeWav("seek", 0, kFrames, 44'100);
    std::string error;
    auto d = Decoder::open(path.string(), 44'100, 2, error);
    REQUIRE(d);

    std::vector<float> buf(2 * 512);
    d->read(buf.data(), 512);
    REQUIRE(d->seek(1500));
    CHECK(d->positionFrames() == 66'150);
    REQUIRE(d->read(buf.data(), 512) == 512);
    CHECK(matchesSignal(buf, 66'150));

    REQUIRE(d->seek(0));  // and back to the start
    REQUIRE(d->read(buf.data(), 512) == 512);
    CHECK(matchesSignal(buf, 0));
    d.reset();
    fs::remove(path);
}

TEST_CASE("Decoder reports files it can't open", "[decoder]") {
    std::string error;
    auto d = Decoder::open((fs::temp_directory_path() / "mp_test_does_not_exist.flac").string(), 48'000, 2, error);
    CHECK_FALSE(d);
    CHECK_FALSE(error.empty());
}
