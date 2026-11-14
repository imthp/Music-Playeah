#include "core/audio/engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "core/audio/decoder.hpp"
#include "core/audio/frame_ring.hpp"
#include "core/audio/output.hpp"
#include "core/audio/renderer.hpp"

namespace mp::core::audio {

namespace {

constexpr double kRingSeconds = 1.5;           // decoded audio kept ahead of the sound card
constexpr double kPrerollMs = 250.0;           // buffered before starting after a play or seek
constexpr std::size_t kChunkFrames = 4096;      // decode in pieces of this size
constexpr std::size_t kMinFreeFrames = 1024;    // don't wake up to write tiny bits
constexpr std::int64_t kPreopenMs = 10'000;     // open the next track this long before the end
constexpr auto kPollPlaying = std::chrono::milliseconds(5);   // keeps the ring topped up
constexpr auto kPollPaused = std::chrono::milliseconds(20);   // status still settles after the fade-out
constexpr auto kPollStopped = std::chrono::milliseconds(100);

struct Command {
    enum class Kind { Play, SetNext, Pause, Resume, Stop, Seek, Quit };
    Kind kind;
    std::optional<TrackRequest> track;
    std::int64_t ms = 0;
};

}  // namespace

struct Engine::Impl {
    EngineListener* listener = nullptr;

    // ---- shared, guarded ----
    std::mutex commandMutex;
    std::condition_variable commandCv;
    std::deque<Command> commands;

    mutable std::mutex statusMutex;
    EngineStatus status;

    // ---- set up once in start() ----
    AudioOutput output;
    std::unique_ptr<FrameRing> ring;
    std::unique_ptr<Renderer> renderer;
    std::thread thread;
    int rate = 0;
    int channels = 0;
    std::atomic<float> volume{1.0f};

    // ---- engine thread only ----
    struct Current {
        TrackRequest request;
        std::unique_ptr<Decoder> decoder;
        std::uint64_t serial = 0;
    };
    // Where in the ring a track (or a seek inside it) begins. The audible track is the last
    // boundary at or before the ring's read position.
    struct Boundary {
        std::uint64_t ringPos = 0;
        TrackRequest request;
        TrackInfo info;
        std::int64_t startFrame = 0;  // track time at ringPos, in output frames
        std::uint64_t serial = 0;     // one per track start; a seek keeps it
    };

    std::optional<Current> current;            // being decoded
    std::optional<TrackRequest> nextRequest;
    std::unique_ptr<Decoder> nextDecoder;      // pre-opened
    std::deque<Boundary> boundaries;
    PlaybackState state = PlaybackState::Stopped;
    bool decodingDone = true;
    std::uint64_t serialCounter = 0;
    std::uint64_t announcedSerial = 0;
    std::vector<float> scratch;

    void post(Command c) {
        {
            std::lock_guard lock(commandMutex);
            commands.push_back(std::move(c));
        }
        commandCv.notify_one();
    }

    void run() {
        for (;;) {
            std::deque<Command> batch;
            {
                std::unique_lock lock(commandMutex);
                const auto wait = state == PlaybackState::Playing  ? kPollPlaying
                                  : state == PlaybackState::Paused ? kPollPaused
                                                                   : kPollStopped;
                commandCv.wait_for(lock, wait, [this] { return !commands.empty(); });
                batch.swap(commands);
            }
            for (Command& c : batch) {
                if (c.kind == Command::Kind::Quit) {
                    return;
                }
                handle(c);
            }
            if (state != PlaybackState::Stopped) {
                fill();
            }
            updateStatus();
        }
    }

    // ---------------------------------------------------------------- commands

    void handle(Command& c) {
        switch (c.kind) {
            case Command::Kind::Play: doPlay(std::move(*c.track)); break;
            case Command::Kind::SetNext: doSetNext(std::move(c.track)); break;
            case Command::Kind::Pause:
                if (state == PlaybackState::Playing) {
                    renderer->setPaused(true);
                    setState(PlaybackState::Paused);
                }
                break;
            case Command::Kind::Resume:
                if (state == PlaybackState::Paused) {
                    renderer->setPaused(false);
                    setState(PlaybackState::Playing);
                }
                break;
            case Command::Kind::Stop: doStop(); break;
            case Command::Kind::Seek: doSeek(c.ms); break;
            case Command::Kind::Quit: break;
        }
    }

    void doPlay(TrackRequest track) {
        clearTracks();
        // "More audio is coming" must be set before the flush, or the callback could skip the
        // pre-roll and start on the first small chunk.
        renderer->setExpectingData(true);
        const std::uint64_t pos = ring->writePosition();
        renderer->requestFlush(pos);  // everything already in the ring is dropped

        std::string error;
        auto decoder = Decoder::open(track.path, rate, channels, error);
        if (!decoder) {
            if (listener) listener->onError(track.id, error);
            renderer->setExpectingData(false);
            setState(PlaybackState::Stopped);
            return;
        }
        const std::uint64_t serial = ++serialCounter;
        boundaries.push_back({pos, track, decoder->info(), 0, serial});
        current = Current{std::move(track), std::move(decoder), serial};
        decodingDone = false;
        renderer->setExpectingData(true);
        renderer->setPaused(false);
        setState(PlaybackState::Playing);
    }

    void doSetNext(std::optional<TrackRequest> track) {
        const auto same = [](const std::optional<TrackRequest>& a, const std::optional<TrackRequest>& b) {
            return a.has_value() == b.has_value() && (!a || (a->id == b->id && a->path == b->path));
        };
        if (same(nextRequest, track)) {
            return;  // keep a pre-opened decoder
        }
        nextDecoder.reset();
        nextRequest = std::move(track);
        if (nextRequest && !current && state != PlaybackState::Stopped) {
            // The current track already finished decoding but is still draining from the ring:
            // there's still time to join the new next track on without a gap.
            decodingDone = false;
            renderer->setExpectingData(true);
        }
    }

    void doStop() {
        clearTracks();
        renderer->requestFlush(ring->writePosition());
        renderer->setExpectingData(false);
        renderer->setPaused(false);
        announcedSerial = 0;
        setState(PlaybackState::Stopped);
    }

    void doSeek(std::int64_t ms) {
        if (state == PlaybackState::Stopped || boundaries.empty()) {
            return;
        }
        pruneBoundaries();
        const Boundary audible = boundaries.front();

        // The decoder may already be working on the next track while the end of this one is
        // still in the ring. Then reopen the audible track and put the other one back as "next".
        if (!current || current->serial != audible.serial) {
            if (current) {
                nextRequest = current->request;
            }
            nextDecoder.reset();
            std::string error;
            auto decoder = Decoder::open(audible.request.path, rate, channels, error);
            if (!decoder) {
                if (listener) listener->onError(audible.request.id, error);
                return;
            }
            current = Current{audible.request, std::move(decoder), audible.serial};
        }

        const std::int64_t duration = current->decoder->info().durationMs;
        ms = std::max<std::int64_t>(0, duration > 0 ? std::min(ms, duration) : ms);
        if (!current->decoder->seek(ms)) {
            return;
        }
        renderer->setExpectingData(true);  // before the flush, see doPlay
        const std::uint64_t pos = ring->writePosition();
        renderer->requestFlush(pos);
        boundaries.clear();
        boundaries.push_back({pos, audible.request, audible.info, ms * rate / 1000, audible.serial});
        decodingDone = false;
    }

    void clearTracks() {
        current.reset();
        nextRequest.reset();
        nextDecoder.reset();
        boundaries.clear();
        decodingDone = true;
    }

    void setState(PlaybackState s) {
        if (s != state) {
            state = s;
            if (listener) listener->onStateChanged(s);
        }
    }

    // ---------------------------------------------------------------- decoding

    void fill() {
        for (;;) {
            const std::size_t free = ring->freeFrames();
            if (free < kMinFreeFrames) {
                return;
            }
            if (!current && !advance()) {
                if (!decodingDone) {
                    decodingDone = true;
                    renderer->setExpectingData(false);  // the ring may now run dry legitimately
                }
                return;
            }

            const std::size_t want = std::min(free, kChunkFrames);
            const std::size_t got = current->decoder->read(scratch.data(), want);
            if (got > 0) {
                ring->write(scratch.data(), got);  // always fits: got <= free
            }
            if (current->decoder->finished()) {
                current.reset();  // next loop moves on to the next track, gaplessly
                continue;
            }

            if (nextRequest && !nextDecoder) {
                const std::int64_t left = current->decoder->remainingMs();
                if (left >= 0 && left < kPreopenMs) {
                    preopenNext();
                }
            }
        }
    }

    void preopenNext() {
        std::string error;
        nextDecoder = Decoder::open(nextRequest->path, rate, channels, error);
        if (!nextDecoder) {
            const std::uint64_t id = nextRequest->id;
            nextRequest.reset();
            if (listener) listener->onError(id, error);
        }
    }

    // Makes the next track current. Its first frame goes right after the last one written.
    bool advance() {
        while (nextRequest) {
            TrackRequest request = std::move(*nextRequest);
            nextRequest.reset();
            std::unique_ptr<Decoder> decoder = std::move(nextDecoder);
            if (!decoder) {
                std::string error;
                decoder = Decoder::open(request.path, rate, channels, error);
                if (!decoder) {
                    if (listener) listener->onError(request.id, error);
                    continue;
                }
            }
            const std::uint64_t serial = ++serialCounter;
            boundaries.push_back({ring->writePosition(), request, decoder->info(), 0, serial});
            current = Current{std::move(request), std::move(decoder), serial};
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- status

    void pruneBoundaries() {
        const std::uint64_t readPos = ring->readPosition();
        while (boundaries.size() >= 2 && boundaries[1].ringPos <= readPos) {
            boundaries.pop_front();
        }
    }

    void updateStatus() {
        pruneBoundaries();
        const std::uint64_t readPos = ring->readPosition();

        bool endOfQueue = false;
        if (state == PlaybackState::Playing && decodingDone && !current && readPos >= ring->writePosition()) {
            endOfQueue = true;
            boundaries.clear();
            announcedSerial = 0;
            setState(PlaybackState::Stopped);
        }

        EngineStatus s;
        s.state = state;
        s.volume = volume.load(std::memory_order_relaxed);
        s.underruns = renderer->underruns();
        s.outputRate = rate;

        std::optional<std::uint64_t> started;
        if (!boundaries.empty()) {
            const Boundary& b = boundaries.front();
            const std::int64_t frames =
                b.startFrame + static_cast<std::int64_t>(readPos > b.ringPos ? readPos - b.ringPos : 0);
            s.hasTrack = true;
            s.trackId = b.request.id;
            s.info = b.info;
            s.durationMs = b.info.durationMs;
            s.positionMs = frames * 1000 / rate;
            if (s.durationMs > 0) {
                s.positionMs = std::min(s.positionMs, s.durationMs);
            }
            // Announce only once the track's audio has actually reached the read position.
            if (b.serial != announcedSerial && readPos >= b.ringPos) {
                announcedSerial = b.serial;
                started = b.request.id;
            }
        }

        {
            std::lock_guard lock(statusMutex);
            status = s;
        }
        if (started && listener) listener->onTrackStarted(*started);
        if (endOfQueue && listener) listener->onEndOfQueue();
    }
};

// -------------------------------------------------------------------- public API

Engine::Engine(EngineListener* listener) : d_(std::make_unique<Impl>()) { d_->listener = listener; }

Engine::~Engine() {
    if (d_->thread.joinable()) {
        d_->post({Command::Kind::Quit, std::nullopt, 0});
        d_->thread.join();
    }
    d_->output.close();  // callback stops before the ring and renderer go away
}

bool Engine::start(std::string& error, bool nullDevice) {
    if (d_->thread.joinable()) {
        return true;
    }
    if (!d_->output.open(error, nullDevice)) {
        return false;
    }
    d_->rate = d_->output.sampleRate();
    d_->channels = d_->output.channels();
    d_->ring = std::make_unique<FrameRing>(static_cast<std::size_t>(d_->rate * kRingSeconds),
                                           static_cast<std::size_t>(d_->channels));
    d_->renderer = std::make_unique<Renderer>(*d_->ring, d_->rate, 10.0, kPrerollMs);
    d_->renderer->setVolume(d_->volume.load());
    d_->scratch.resize(kChunkFrames * static_cast<std::size_t>(d_->channels));
    if (!d_->output.start(*d_->renderer, error)) {
        d_->output.close();
        return false;
    }
    d_->thread = std::thread([this] { d_->run(); });
    return true;
}

void Engine::play(TrackRequest track) { d_->post({Command::Kind::Play, std::move(track), 0}); }
void Engine::setNext(std::optional<TrackRequest> track) { d_->post({Command::Kind::SetNext, std::move(track), 0}); }
void Engine::pause() { d_->post({Command::Kind::Pause, std::nullopt, 0}); }
void Engine::resume() { d_->post({Command::Kind::Resume, std::nullopt, 0}); }
void Engine::stop() { d_->post({Command::Kind::Stop, std::nullopt, 0}); }
void Engine::seek(std::int64_t ms) { d_->post({Command::Kind::Seek, std::nullopt, ms}); }

void Engine::setVolume(float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);
    d_->volume.store(volume);
    if (d_->renderer) {
        d_->renderer->setVolume(volume);  // atomic, takes effect in the next callback
    }
}

EngineStatus Engine::status() const {
    std::lock_guard lock(d_->statusMutex);
    return d_->status;
}

std::string Engine::deviceName() const { return d_->output.deviceName(); }
int Engine::outputRate() const { return d_->rate; }

}  // namespace mp::core::audio
