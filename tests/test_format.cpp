#include <catch2/catch_test_macros.hpp>

#include "core/format.hpp"

using mp::core::formatDuration;

TEST_CASE("formatDuration shows minutes and seconds", "[format]") {
    CHECK(formatDuration(0) == "0:00");
    CHECK(formatDuration(7'000) == "0:07");
    CHECK(formatDuration(187'000) == "3:07");
    CHECK(formatDuration(599'000) == "9:59");
}

TEST_CASE("formatDuration adds hours only when needed", "[format]") {
    CHECK(formatDuration(3'599'000) == "59:59");
    CHECK(formatDuration(3'600'000) == "1:00:00");
    CHECK(formatDuration(3'723'000) == "1:02:03");
}

TEST_CASE("formatDuration truncates milliseconds and clamps negatives", "[format]") {
    CHECK(formatDuration(1'999) == "0:01");
    CHECK(formatDuration(-5'000) == "0:00");
}
