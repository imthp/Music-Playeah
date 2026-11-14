#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "core/audio/track_info.hpp"

namespace mp::core::audio {

enum class PlaybackState { Stopped, Playing, Paused };

// A consistent picture of playback, safe to read from any thread (Engine::status()).
struct EngineStatus {
    PlaybackState state = PlaybackState::Stopped;
    bool hasTrack = false;
    std::uint64_t trackId = 0;   // the track you can hear right now
    std::int64_t positionMs = 0;
    std::int64_t durationMs = 0;
    TrackInfo info;
    float volume = 1.0f;
    std::uint64_t underruns = 0;  // times the audio callback found the ring empty mid-track
    int outputRate = 0;
};

// Events from the engine. Called on the engine's own thread, never the audio callback.
// Keep handlers short; the Qt layer just re-posts them to the UI thread.
class EngineListener {
public:
    virtual ~EngineListener() = default;
    // A track became audible: after play(), or when playback crossed into the "next" track.
    virtual void onTrackStarted(std::uint64_t /*trackId*/) {}
    virtual void onStateChanged(PlaybackState /*state*/) {}
    // The last queued track finished playing. The engine is now Stopped.
    virtual void onEndOfQueue() {}
    // A track couldn't be opened. Playback continues with whatever is next, if anything.
    virtual void onError(std::uint64_t /*trackId*/, const std::string& /*message*/) {}
};

// Playback engine (SPEC §4, §5). Three threads take part:
//   caller (UI)   -> commands go into a queue; nothing here blocks on audio.
//   engine thread -> runs the commands, decodes with FFmpeg into the ring buffer,
//                    pre-opens the next track ~10 s early, keeps the status up to date.
//   audio thread  -> miniaudio's callback; only reads the ring (see Renderer).
//
// Gapless: the engine plays one track at a time but knows the next one (setNext). When the
// current track ends, the next one's audio is written straight after it in the same ring, so
// the sound card never sees a gap. The caller keeps the playlist; the engine only needs one
// track of look-ahead.
class Engine {
public:
    explicit Engine(EngineListener* listener = nullptr);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Opens the default audio device and starts the engine thread.
    // nullDevice: run against a silent virtual device (tests, machines without sound).
    bool start(std::string& error, bool nullDevice = false);

    // All of these return immediately; the engine thread carries them out in order.
    void play(TrackRequest track);                    // drop everything, play this now
    void setNext(std::optional<TrackRequest> track);  // what follows the current track (gapless)
    void pause();
    void resume();
    void stop();
    void seek(std::int64_t ms);                       // within the audible track
    void setVolume(float volume);                     // 0..1 slider position (cubic curve)

    EngineStatus status() const;
    std::string deviceName() const;
    int outputRate() const;  // the device's rate; 0 before start()

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mp::core::audio
