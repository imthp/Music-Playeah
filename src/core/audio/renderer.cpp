#include "core/audio/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mp::core::audio {

float volumeToGain(float volume) noexcept {
    const float v = std::clamp(volume, 0.0f, 1.0f);
    return v * v * v;
}

Renderer::Renderer(FrameRing& ring, int sampleRate, double rampMs, double prerollMs)
    : ring_(ring),
      channels_(ring.channels()),
      rampStep_(static_cast<float>(1.0 / std::max(1.0, rampMs * 0.001 * std::max(sampleRate, 1)))),
      prerollFrames_(std::min(ring.capacityFrames(),
                              static_cast<std::size_t>(std::max(0.0, prerollMs) * 0.001 * std::max(sampleRate, 1)))) {}

void Renderer::requestFlush(std::uint64_t ringPosition) noexcept {
    // Target first, then the sequence number with release, so the callback that sees the new
    // sequence also sees a target at least this new.
    flushTarget_.store(ringPosition, std::memory_order_release);
    flushSeq_.fetch_add(1, std::memory_order_release);
}

void Renderer::render(float* out, std::size_t frames) noexcept {
    const std::uint32_t seq = flushSeq_.load(std::memory_order_acquire);
    if (seq != seenFlushSeq_) {
        seenFlushSeq_ = seq;
        ring_.skipTo(flushTarget_.load(std::memory_order_acquire));
        gain_ = 0.0f;  // fade in after a seek or track change
        primed_ = false;
        waiting_ = true;
    }

    // Pre-roll: stay silent until enough audio is buffered, or until no more is coming
    // (a track shorter than the pre-roll, or the end of the queue).
    if (waiting_) {
        if (ring_.availableFrames() >= prerollFrames_ || !expecting_.load(std::memory_order_relaxed)) {
            waiting_ = false;
        } else {
            std::memset(out, 0, frames * channels_ * sizeof(float));
            return;
        }
    }

    const bool paused = paused_.load(std::memory_order_relaxed);
    const float target = paused ? 0.0f : targetGain_.load(std::memory_order_relaxed);

    // While paused, consume only the few frames needed to fade out, then hold position.
    std::size_t wanted = frames;
    if (paused) {
        const auto fadeFrames = static_cast<std::size_t>(std::ceil(gain_ / rampStep_));
        wanted = std::min(frames, fadeFrames);
    }

    const std::size_t got = wanted > 0 ? ring_.read(out, wanted) : 0;
    if (got < frames) {
        std::memset(out + got * channels_, 0, (frames - got) * channels_ * sizeof(float));
    }
    if (got > 0) {
        primed_ = true;
    }
    if (got < wanted && !paused && primed_ && expecting_.load(std::memory_order_relaxed)) {
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }

    // Per-frame gain ramp toward the target. Multiplying by exactly 1.0f leaves samples unchanged.
    for (std::size_t f = 0; f < got; ++f) {
        gain_ += std::clamp(target - gain_, -rampStep_, rampStep_);
        float* frame = out + f * channels_;
        for (std::size_t c = 0; c < channels_; ++c) {
            frame[c] *= gain_;
        }
    }
    // Snap float leftovers to zero so a paused stream holds its position exactly.
    if ((got == 0 && paused) || (target == 0.0f && gain_ < 1e-6f)) {
        gain_ = 0.0f;
    }
}

}  // namespace mp::core::audio
