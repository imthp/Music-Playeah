#pragma once

#include <cstdint>
#include <string>

namespace mp::core {

// Formats a playback time for display: "0:07", "3:07", "1:02:03".
// Negative values are treated as 0. Milliseconds are truncated, not rounded,
// so a progress label never runs ahead of the audio.
std::string formatDuration(std::int64_t milliseconds);

}  // namespace mp::core
