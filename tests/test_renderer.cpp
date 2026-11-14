#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "core/audio/frame_ring.hpp"
#include "core/audio/renderer.hpp"

using mp::core::audio::FrameRing;
using mp::core::audio::Renderer;
using mp::core::audio::volumeToGain;

namespace {
// Mono, 1000 Hz with a 10 ms ramp = 10 frames per full ramp: easy numbers.
constexpr int kRate = 1000;

void pushConstant(FrameRing& ring, std::size_t frames, float value = 1.0f) {
    std::vector<float> v(frames, value);
    ring.write(v.data(), frames);
}
}  // namespace

TEST_CASE("volumeToGain is cubic and exact at the ends", "[renderer]") {
    CHECK(volumeToGain(0.0f) == 0.0f);
    CHECK(volumeToGain(1.0f) == 1.0f);
    CHECK(volumeToGain(0.5f) == Catch::Approx(0.125f));
    CHECK(volumeToGain(2.0f) == 1.0f);
    CHECK(volumeToGain(-1.0f) == 0.0f);
}

TEST_CASE("Renderer fades in, then passes samples through unchanged", "[renderer]") {
    FrameRing ring(100, 1);
    Renderer r(ring, kRate);
    pushConstant(ring, 40);

    std::vector<float> out(20);
    r.render(out.data(), 20);
    CHECK(out[0] == Catch::Approx(0.1f));   // ramp starts from silence
    CHECK(out[9] == Catch::Approx(1.0f));
    CHECK(out[19] == 1.0f);                  // exactly unchanged at full volume
    CHECK(ring.readPosition() == 20);
}

TEST_CASE("Renderer pause fades out, then holds the read position", "[renderer]") {
    FrameRing ring(100, 1);
    Renderer r(ring, kRate);
    pushConstant(ring, 100);
    std::vector<float> out(20);
    r.render(out.data(), 20);  // now at full gain

    r.setPaused(true);
    r.render(out.data(), 20);
    CHECK(ring.readPosition() == 30);  // only the 10 fade-out frames were used
    CHECK(out[9] == Catch::Approx(0.0f).margin(1e-6));
    CHECK(out[15] == 0.0f);

    r.render(out.data(), 20);
    CHECK(ring.readPosition() == 30);  // paused: nothing consumed

    r.setPaused(false);
    r.render(out.data(), 20);
    CHECK(ring.readPosition() == 50);
    CHECK(out[0] == Catch::Approx(0.1f));  // fades back in
}

TEST_CASE("Renderer flush skips old audio and fades the new audio in", "[renderer]") {
    FrameRing ring(100, 1);
    Renderer r(ring, kRate);
    pushConstant(ring, 30, 1.0f);
    std::vector<float> out(10);
    r.render(out.data(), 10);

    const auto newStart = ring.writePosition();
    r.requestFlush(newStart);
    pushConstant(ring, 30, 0.5f);
    r.render(out.data(), 10);
    CHECK(ring.readPosition() == newStart + 10);
    CHECK(out[0] == Catch::Approx(0.05f));  // new audio (0.5) at gain 0.1
}

TEST_CASE("Renderer counts underruns only mid-stream", "[renderer]") {
    FrameRing ring(100, 1);
    Renderer r(ring, kRate);
    std::vector<float> out(10);

    r.setExpectingData(true);
    r.render(out.data(), 10);  // nothing played yet: start-up, not an underrun
    CHECK(r.underruns() == 0);

    pushConstant(ring, 5);
    r.render(out.data(), 10);  // ran dry after 5 frames
    CHECK(r.underruns() == 1);
    CHECK(out[7] == 0.0f);

    r.setExpectingData(false);  // end of queue: running dry is expected
    r.render(out.data(), 10);
    CHECK(r.underruns() == 1);
}

TEST_CASE("Renderer waits for the pre-roll after a flush before it starts", "[renderer]") {
    FrameRing ring(1000, 1);
    Renderer r(ring, kRate, 10.0, /*prerollMs=*/100.0);  // 100 frames at 1000 Hz
    std::vector<float> out(10);
    r.setExpectingData(true);
    r.requestFlush(ring.writePosition());

    pushConstant(ring, 50);  // a first small chunk, then the disk is slow
    r.render(out.data(), 10);
    CHECK(ring.readPosition() == 0);  // still waiting: nothing consumed
    CHECK(out[5] == 0.0f);
    CHECK(r.underruns() == 0);

    pushConstant(ring, 50);  // pre-roll reached
    r.render(out.data(), 10);
    CHECK(ring.readPosition() == 10);
}

TEST_CASE("Renderer skips the pre-roll when no more audio is coming", "[renderer]") {
    FrameRing ring(1000, 1);
    Renderer r(ring, kRate, 10.0, 100.0);
    std::vector<float> out(10);
    r.setExpectingData(false);  // e.g. a 30 ms track at the end of the queue
    pushConstant(ring, 30);
    r.render(out.data(), 10);
    CHECK(ring.readPosition() == 10);
}
