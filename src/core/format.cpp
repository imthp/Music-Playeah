#include "core/format.hpp"

#include <format>

namespace mp::core {

std::string formatDuration(std::int64_t milliseconds) {
    if (milliseconds < 0) {
        milliseconds = 0;
    }
    const std::int64_t totalSeconds = milliseconds / 1000;
    const std::int64_t hours = totalSeconds / 3600;
    const std::int64_t minutes = (totalSeconds % 3600) / 60;
    const std::int64_t seconds = totalSeconds % 60;

    if (hours > 0) {
        return std::format("{}:{:02}:{:02}", hours, minutes, seconds);
    }
    return std::format("{}:{:02}", minutes, seconds);
}

std::string formatKilohertz(int sampleRate) {
    if (sampleRate <= 0) {
        return "?";
    }
    // One decimal is enough for every real rate (44.1, 88.2, 176.4, 352.8).
    const int tenths = (sampleRate + 50) / 100;
    if (tenths % 10 == 0) {
        return std::format("{}", tenths / 10);
    }
    return std::format("{}.{}", tenths / 10, tenths % 10);
}

std::string formatBadge(const audio::TrackInfo& info) {
    if (info.codec.empty()) {
        return {};
    }
    if (info.dsd) {
        // DSD rates are multiples of 44.1 kHz: 2.8224 MHz = DSD64, 5.6448 MHz = DSD128...
        const int multiple = info.sampleRate / 44'100;
        return multiple > 0 ? std::format("DSD{}", multiple) : std::string("DSD");
    }
    if (info.lossless) {
        if (info.bitDepth > 0) {
            return std::format("{} {}/{}", info.codec, info.bitDepth, formatKilohertz(info.sampleRate));
        }
        return std::format("{} {}", info.codec, formatKilohertz(info.sampleRate));
    }
    if (info.bitrateKbps > 0) {
        return std::format("{} {}", info.codec, info.bitrateKbps);
    }
    return info.codec;
}

}  // namespace mp::core
