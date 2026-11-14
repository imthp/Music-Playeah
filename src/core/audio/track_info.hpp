#pragma once

#include <cstdint>
#include <string>

namespace mp::core::audio {

// What the engine is asked to play. `id` is chosen by the caller (later: the library's track id)
// and comes back in events, so the engine never has to compare paths.
struct TrackRequest {
    std::uint64_t id = 0;
    std::string path;  // UTF-8
};

// Facts about the source file, filled in by the decoder when it opens a track.
struct TrackInfo {
    std::string codec;          // display name: "FLAC", "MP3", "AAC", "ALAC", "WAV", "AIFF", "DSD", "Opus", "Vorbis"
    int sampleRate = 0;         // source rate in Hz (for DSD: the DSD rate, e.g. 2'822'400)
    int bitDepth = 0;           // 0 for lossy codecs
    int channels = 0;
    int bitrateKbps = 0;        // 0 if unknown
    std::int64_t durationMs = 0;  // 0 if unknown
    bool lossless = false;
    bool dsd = false;
};

}  // namespace mp::core::audio
