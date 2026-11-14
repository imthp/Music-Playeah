#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/engine.hpp"
#include "wav_writer.hpp"

using namespace mp::core::audio;
using namespace std::chrono_literals;
using mp::test::writeWav;

namespace {

// Records engine events so the test can check their order.
struct Recorder : EngineListener {
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::string> events;

    void add(std::string e) {
        {
            std::lock_guard lock(m);
            events.push_back(std::move(e));
        }
        cv.notify_all();
    }
    void onTrackStarted(std::uint64_t id) override { add("start " + std::to_string(id)); }
    void onEndOfQueue() override { add("end"); }
    void onError(std::uint64_t id, const std::string&) override { add("error " + std::to_string(id)); }

    bool waitFor(const std::string& e, std::chrono::milliseconds timeout) {
        std::unique_lock lock(m);
        return cv.wait_for(lock, timeout, [&] { return std::find(events.begin(), events.end(), e) != events.end(); });
    }
    std::vector<std::string> snapshot() {
        std::lock_guard lock(m);
        return events;
    }
};

}  // namespace

// These run the whole engine against miniaudio's silent null device, in real time.

TEST_CASE("Engine plays two tracks back to back, then reports end of queue", "[engine]") {
    const auto a = writeWav("engine_a", 0, 44'100 / 2, 44'100);       // 0.5 s
    const auto b = writeWav("engine_b", 44'100 / 2, 44'100 / 2, 44'100);

    Recorder rec;
    {
        Engine engine(&rec);
        std::string error;
        REQUIRE(engine.start(error, /*nullDevice=*/true));
        engine.play({1, a.string()});
        engine.setNext(TrackRequest{2, b.string()});
        REQUIRE(rec.waitFor("end", 5s));

        CHECK(rec.snapshot() == std::vector<std::string>{"start 1", "start 2", "end"});
        const EngineStatus s = engine.status();
        CHECK(s.state == PlaybackState::Stopped);
        CHECK(s.underruns == 0);
    }
    std::filesystem::remove(a);
    std::filesystem::remove(b);
}

TEST_CASE("Engine seeks, pauses and resumes", "[engine]") {
    const auto path = writeWav("engine_long", 0, 44'100 * 4, 44'100);  // 4 s
    Recorder rec;
    {
        Engine engine(&rec);
        std::string error;
        REQUIRE(engine.start(error, true));
        engine.play({7, path.string()});
        REQUIRE(rec.waitFor("start 7", 2s));

        engine.seek(2500);
        std::this_thread::sleep_for(150ms);
        auto s = engine.status();
        CHECK(s.positionMs >= 2500);
        CHECK(s.positionMs < 2800);
        CHECK(s.durationMs == 4000);
        CHECK(s.info.codec == "WAV");

        engine.pause();
        std::this_thread::sleep_for(100ms);
        const auto pausedAt = engine.status().positionMs;
        std::this_thread::sleep_for(200ms);
        CHECK(engine.status().positionMs == pausedAt);
        CHECK(engine.status().state == PlaybackState::Paused);

        engine.resume();
        std::this_thread::sleep_for(200ms);
        CHECK(engine.status().positionMs > pausedAt);
        CHECK(engine.status().underruns == 0);
    }
    std::filesystem::remove(path);
}

TEST_CASE("Engine reports a track it can't open and moves on", "[engine]") {
    const auto good = writeWav("engine_good", 0, 44'100 / 4, 44'100);
    Recorder rec;
    {
        Engine engine(&rec);
        std::string error;
        REQUIRE(engine.start(error, true));
        engine.play({1, "this/file/does/not/exist.flac"});
        REQUIRE(rec.waitFor("error 1", 2s));

        engine.play({2, good.string()});
        engine.setNext(TrackRequest{3, "missing-too.mp3"});
        REQUIRE(rec.waitFor("end", 3s));
        // The next track is opened early (up to 10 s before it's due), so its error can come
        // before track 2 is even audible.
        const auto events = rec.snapshot();
        CHECK(events.front() == "error 1");
        CHECK(std::count(events.begin(), events.end(), "start 2") == 1);
        CHECK(std::count(events.begin(), events.end(), "error 3") == 1);
        CHECK(events.back() == "end");
    }
    std::filesystem::remove(good);
}
