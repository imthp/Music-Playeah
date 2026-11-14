#include "core/audio/frame_ring.hpp"

#include <algorithm>
#include <cstring>

namespace mp::core::audio {

FrameRing::FrameRing(std::size_t capacityFrames, std::size_t channels)
    : channels_(channels == 0 ? 1 : channels),
      capacity_(capacityFrames == 0 ? 1 : capacityFrames),
      data_(std::make_unique<float[]>(capacity_ * channels_)) {}

std::size_t FrameRing::freeFrames() const noexcept {
    const auto r = read_.load(std::memory_order_acquire);
    const auto w = write_.load(std::memory_order_relaxed);
    return capacity_ - static_cast<std::size_t>(w - r);
}

std::size_t FrameRing::availableFrames() const noexcept {
    const auto w = write_.load(std::memory_order_acquire);
    const auto r = read_.load(std::memory_order_relaxed);
    return static_cast<std::size_t>(w - r);
}

std::size_t FrameRing::write(const float* frames, std::size_t count) noexcept {
    const auto w = write_.load(std::memory_order_relaxed);  // only we change it
    const auto r = read_.load(std::memory_order_acquire);   // see how far the consumer got
    const std::size_t free = capacity_ - static_cast<std::size_t>(w - r);
    const std::size_t n = std::min(count, free);
    if (n == 0) {
        return 0;
    }

    const std::size_t start = static_cast<std::size_t>(w % capacity_);
    const std::size_t first = std::min(n, capacity_ - start);  // up to the end of the array
    std::memcpy(data_.get() + start * channels_, frames, first * channels_ * sizeof(float));
    if (n > first) {  // wrap around to the beginning
        std::memcpy(data_.get(), frames + first * channels_, (n - first) * channels_ * sizeof(float));
    }

    // Release: the consumer must see the copied samples before it sees the new position.
    write_.store(w + n, std::memory_order_release);
    return n;
}

std::size_t FrameRing::read(float* out, std::size_t count) noexcept {
    const auto r = read_.load(std::memory_order_relaxed);
    const auto w = write_.load(std::memory_order_acquire);
    const std::size_t n = std::min(count, static_cast<std::size_t>(w - r));
    if (n == 0) {
        return 0;
    }

    const std::size_t start = static_cast<std::size_t>(r % capacity_);
    const std::size_t first = std::min(n, capacity_ - start);
    std::memcpy(out, data_.get() + start * channels_, first * channels_ * sizeof(float));
    if (n > first) {
        std::memcpy(out + first * channels_, data_.get(), (n - first) * channels_ * sizeof(float));
    }

    // Release: the producer may overwrite these slots only after we are done copying.
    read_.store(r + n, std::memory_order_release);
    return n;
}

void FrameRing::skipTo(std::uint64_t position) noexcept {
    const auto r = read_.load(std::memory_order_relaxed);
    const auto w = write_.load(std::memory_order_acquire);
    const auto target = std::min(position, w);  // never past what was written
    if (target > r) {
        read_.store(target, std::memory_order_release);
    }
}

}  // namespace mp::core::audio
