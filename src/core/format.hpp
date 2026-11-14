#pragma once

#include <cstdint>
#include <string>

#include "core/audio/track_info.hpp"

namespace mp::core {

// Formats a playback time for display: "0:07", "3:07", "1:02:03".
// Negative values are treated as 0. Milliseconds are truncated, not rounded,
// so a progress label never runs ahead of the audio.
std::string formatDuration(std::int64_t milliseconds);

// "44.1", "96", "88.2", "192": a sample rate in kHz without trailing zeros.
std::string formatKilohertz(int sampleRate);

// The format badge (SPEC §2): "FLAC 24/96", "WAV 16/44.1", "DSD64", "MP3 320", "AAC 256".
// Lossy formats show the bitrate when known, otherwise just the codec ("Opus").
std::string formatBadge(const audio::TrackInfo& info);

}  // namespace mp::core
