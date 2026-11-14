#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/audio/frame_ring.hpp"

namespace mp::core::audio {

// Slider position (0..1) to amplitude. A cubic curve sounds roughly even across the slider;
// a linear one does almost nothing in the top half. 1.0 maps to exactly 1.0, so at full volume
// the samples pass through unchanged (keeps the door open for bit-perfect output, SPEC §5).
float volumeToGain(float volume) noexcept;

// The real-time half of playback: what the audio callback runs.
// It pulls frames from the ring, applies volume, and handles pause and flush with short ramps
// so nothing clicks. It never locks, allocates or waits (SPEC §3); everything it shares with
// other threads is an atomic. Kept separate from the miniaudio device so it can be unit-tested.
class Renderer {
public:
    // rampMs: how long a full 0 -> 1 gain change takes (pause, resume, seek fade-in, volume).
    // prerollMs: after a flush (new track, seek), wait until this much audio is buffered before
    // starting, so a slow first disk read can't cause a stutter right at the start.
    Renderer(FrameRing& ring, int sampleRate, double rampMs = 10.0, double prerollMs = 0.0);

    // ---- audio callback ----
    // Fills `frames` interleaved frames. Always fills the whole buffer (silence if needed).
    void render(float* out, std::size_t frames) noexcept;

    // ---- control, from any non-audio thread ----
    void setPaused(bool paused) noexcept { paused_.store(paused, std::memory_order_relaxed); }
    void setVolume(float volume) noexcept { targetGain_.store(volumeToGain(volume), std::memory_order_relaxed); }
    // True while more audio is on its way. An empty ring then counts as an underrun;
    // at the real end of the queue it does not.
    void setExpectingData(bool expecting) noexcept { expecting_.store(expecting, std::memory_order_relaxed); }
    std::uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }

    // ---- producer thread only ----
    // Asks the callback to drop everything before `ringPosition` (normally ring.writePosition()
    // at the moment of a seek or a new track) and fade back in from silence.
    void requestFlush(std::uint64_t ringPosition) noexcept;

private:
    FrameRing& ring_;
    std::size_t channels_;
    float rampStep_;  // gain change per frame
    std::size_t prerollFrames_;

    std::atomic<float> targetGain_{1.0f};
    std::atomic<bool> paused_{false};
    std::atomic<bool> expecting_{false};
    std::atomic<std::uint64_t> flushTarget_{0};
    std::atomic<std::uint32_t> flushSeq_{0};
    std::atomic<std::uint64_t> underruns_{0};

    // Touched only by the audio callback.
    std::uint32_t seenFlushSeq_ = 0;
    float gain_ = 0.0f;
    bool primed_ = false;   // has played something since the last flush
    bool waiting_ = true;   // waiting for the pre-roll after a flush

    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
};

}  // namespace mp::core::audio
