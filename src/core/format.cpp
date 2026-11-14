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

}  // namespace mp::core
