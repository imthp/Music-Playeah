#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mp::core::audio {

// Lock-free single-producer / single-consumer ring of interleaved float frames.
//   Producer: the decoder thread (write, freeFrames, writePosition).
//   Consumer: the audio callback (read, availableFrames, readPosition, skipTo).
// No locks and no allocation after construction, so the consumer side is safe to call
// from the real-time audio callback (SPEC §3).
//
// Positions are 64-bit frame counters that only ever grow. Besides indexing the ring, they
// work as a timeline: "ring frame N" names one moment of audio. The engine uses that to know
// where a track starts and which track is audible (see Engine::Boundary).
class FrameRing {
public:
    FrameRing(std::size_t capacityFrames, std::size_t channels);

    FrameRing(const FrameRing&) = delete;
    FrameRing& operator=(const FrameRing&) = delete;

    std::size_t channels() const noexcept { return channels_; }
    std::size_t capacityFrames() const noexcept { return capacity_; }

    // ---- producer side ----
    std::size_t freeFrames() const noexcept;
    // Copies up to `count` frames in; returns how many fit.
    std::size_t write(const float* frames, std::size_t count) noexcept;
    std::uint64_t writePosition() const noexcept { return write_.load(std::memory_order_acquire); }

    // ---- consumer side ----
    std::size_t availableFrames() const noexcept;
    // Copies up to `count` frames out; returns how many were available.
    std::size_t read(float* out, std::size_t count) noexcept;
    std::uint64_t readPosition() const noexcept { return read_.load(std::memory_order_acquire); }
    // Drops everything before `position` (a value the producer got from writePosition()).
    // This is how a seek flushes old audio: only the consumer may move the read position.
    void skipTo(std::uint64_t position) noexcept;

private:
    std::size_t channels_;
    std::size_t capacity_;  // in frames
    std::unique_ptr<float[]> data_;
    // Separate cache lines so the two threads don't keep invalidating each other's line.
    alignas(64) std::atomic<std::uint64_t> write_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
};

}  // namespace mp::core::audio
