#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/audio/track_info.hpp"

namespace mp::core::audio {

// Decodes one file with FFmpeg and converts it to interleaved float32 at the output's
// sample rate and channel count (SPEC §5: everything is resampled to the device rate, so
// tracks with different rates still join without a gap).
// Used only from the decoder thread. FFmpeg types stay inside decoder.cpp.
class Decoder {
public:
    // Returns nullptr and fills `error` if the file can't be opened or has no audio stream.
    static std::unique_ptr<Decoder> open(const std::string& utf8Path, int outRate, int outChannels,
                                         std::string& error);
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    const TrackInfo& info() const noexcept;
    int outRate() const noexcept;
    int outChannels() const noexcept;

    // Writes up to maxFrames interleaved frames. Returns fewer only when the track has ended
    // (then finished() is true) or on a decode error that ends the track early.
    std::size_t read(float* out, std::size_t maxFrames);
    bool finished() const noexcept;

    // Jumps to `ms` from the start of the track. Sample-accurate for formats with exact
    // timestamps (FLAC, WAV, MP3 with a LAME header...). Returns false if FFmpeg refused.
    bool seek(std::int64_t ms);

    // Output frames produced since the start of the track (or since the seek target).
    std::int64_t positionFrames() const noexcept;
    // Milliseconds left, or -1 if the duration is unknown.
    std::int64_t remainingMs() const noexcept;

private:
    struct Impl;
    explicit Decoder(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> d_;
};

}  // namespace mp::core::audio
