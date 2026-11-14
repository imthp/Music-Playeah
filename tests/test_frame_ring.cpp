#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "core/audio/frame_ring.hpp"

using mp::core::audio::FrameRing;

TEST_CASE("FrameRing writes and reads frames in order", "[ring]") {
    FrameRing ring(8, 2);
    const std::vector<float> in = {1, 2, 3, 4, 5, 6};  // 3 stereo frames
    REQUIRE(ring.write(in.data(), 3) == 3);
    CHECK(ring.availableFrames() == 3);
    CHECK(ring.freeFrames() == 5);

    std::vector<float> out(6);
    REQUIRE(ring.read(out.data(), 3) == 3);
    CHECK(out == in);
    CHECK(ring.availableFrames() == 0);
    CHECK(ring.readPosition() == 3);
    CHECK(ring.writePosition() == 3);
}

TEST_CASE("FrameRing refuses to overfill and wraps around", "[ring]") {
    FrameRing ring(4, 1);
    const std::vector<float> a = {1, 2, 3, 4, 5, 6};
    CHECK(ring.write(a.data(), 6) == 4);  // only 4 fit
    CHECK(ring.freeFrames() == 0);

    std::vector<float> out(4);
    CHECK(ring.read(out.data(), 3) == 3);
    CHECK(out[0] == 1);
    CHECK(out[2] == 3);

    const std::vector<float> b = {7, 8, 9};
    CHECK(ring.write(b.data(), 3) == 3);  // wraps past the end of the array
    CHECK(ring.read(out.data(), 4) == 4);
    CHECK(out == std::vector<float>{4, 7, 8, 9});
}

TEST_CASE("FrameRing skipTo drops old audio but never past what was written", "[ring]") {
    FrameRing ring(16, 1);
    std::vector<float> data(10, 1.0f);
    ring.write(data.data(), 10);
    ring.skipTo(6);
    CHECK(ring.readPosition() == 6);
    CHECK(ring.availableFrames() == 4);
    ring.skipTo(100);
    CHECK(ring.readPosition() == 10);
    ring.skipTo(3);  // going back does nothing
    CHECK(ring.readPosition() == 10);
}

TEST_CASE("FrameRing passes a long sequence between two threads intact", "[ring][threads]") {
    constexpr std::size_t kFrames = 2'000'000;
    FrameRing ring(1000, 2);

    std::thread producer([&] {
        std::vector<float> chunk(2 * 97);
        std::size_t next = 0;
        while (next < kFrames) {
            const std::size_t n = std::min<std::size_t>(97, kFrames - next);
            for (std::size_t i = 0; i < n; ++i) {
                chunk[2 * i] = static_cast<float>(next + i);
                chunk[2 * i + 1] = -static_cast<float>(next + i);
            }
            std::size_t done = 0;
            while (done < n) {
                done += ring.write(chunk.data() + 2 * done, n - done);
            }
            next += n;
        }
    });

    std::vector<float> buf(2 * 61);
    std::size_t expected = 0;
    bool ok = true;
    while (expected < kFrames) {
        const std::size_t got = ring.read(buf.data(), 61);
        for (std::size_t i = 0; i < got; ++i) {
            ok = ok && buf[2 * i] == static_cast<float>(expected) && buf[2 * i + 1] == -static_cast<float>(expected);
            ++expected;
        }
    }
    producer.join();
    CHECK(ok);
}
