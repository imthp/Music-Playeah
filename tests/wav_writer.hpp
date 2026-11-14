#pragma once

// Test helper: writes small 16-bit stereo WAV files with a known signal.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace mp::test {

namespace fs = std::filesystem;

// A recognizable 16-bit stereo signal: every frame is different, left and right differ.
inline std::int16_t sampleAt(std::size_t frame, int channel) {
    const auto v = static_cast<int>((frame * 7 + static_cast<std::size_t>(channel) * 3001) % 60'000) - 30'000;
    return static_cast<std::int16_t>(v);
}

inline float asFloat(std::int16_t s) { return static_cast<float>(s) / 32768.0f; }

template <typename T>
void put(std::ofstream& f, T v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));  // WAV is little-endian, like x64
}

// Writes frames [first, first + count) of the test signal as a 16-bit stereo WAV file.
inline fs::path writeWav(const std::string& name, std::size_t first, std::size_t count, int rate) {
    const fs::path path = fs::temp_directory_path() / ("mp_test_" + name + ".wav");
    std::ofstream f(path, std::ios::binary);
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(count * 4);
    f.write("RIFF", 4);
    put<std::uint32_t>(f, 36 + dataBytes);
    f.write("WAVEfmt ", 8);
    put<std::uint32_t>(f, 16);
    put<std::uint16_t>(f, 1);  // PCM
    put<std::uint16_t>(f, 2);  // channels
    put<std::uint32_t>(f, static_cast<std::uint32_t>(rate));
    put<std::uint32_t>(f, static_cast<std::uint32_t>(rate * 4));
    put<std::uint16_t>(f, 4);   // block align
    put<std::uint16_t>(f, 16);  // bits
    f.write("data", 4);
    put<std::uint32_t>(f, dataBytes);
    for (std::size_t i = first; i < first + count; ++i) {
        put(f, sampleAt(i, 0));
        put(f, sampleAt(i, 1));
    }
    return path;
}

}  // namespace mp::test
