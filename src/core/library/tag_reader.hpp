#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace mp::core::library {

// Everything the scanner stores for one file (milestone 3a). Strings are UTF-8, trimmed; "" / 0
// mean "the file has no such tag". Several values in one tag are joined with "; ".
// Grouping movements into works is milestone 3b: `workTag` and the movement fields are raw here.
struct FileTags {
    std::string title;  // never empty: falls back to the file name without extension
    std::string artist;
    std::string albumArtist;
    std::string album;
    std::string composer;
    std::string genre;
    std::string workTag;       // WORK / ©wrk
    std::string movementName;  // MOVEMENTNAME / MVNM / ©mvn
    int year = 0;
    int discNo = 0;
    int trackNo = 0;
    int movementNo = 0;        // MOVEMENT / MOVEMENTNUMBER / MVIN / ©mvi
    int movementTotal = 0;     // MOVEMENTTOTAL / MOVEMENTCOUNT / ©mvc, or the "/n" of "2/4"
    std::optional<double> rgTrackGain;  // dB
    std::optional<double> rgAlbumGain;

    // Audio properties. Codec uses the names of audio::TrackInfo::codec.
    std::string codec;
    int sampleRate = 0;  // DSD: the DSD rate (2'822'400 for DSD64)
    int bitDepth = 0;    // 0 for lossy codecs, 1 for DSD
    int channels = 0;
    int bitrateKbps = 0;
    std::int64_t durationMs = 0;
};

// True for the extensions the library scans (SPEC §2): flac mp3 m4a dsf dff wav aif aiff ogg oga opus.
bool isLibraryFile(const std::filesystem::path& file);

// Reads tags and audio properties with TagLib. Returns false and fills `error` if the file
// can't be parsed (corrupt, or a format this TagLib build does not know: DSD needs TagLib 2).
// Safe to call from several threads on different files.
bool readTags(const std::filesystem::path& file, FileTags& out, std::string& error);

// Helpers exposed for tests.
int parseLeadingInt(const std::string& text);                   // "3/12" -> 3, "A" -> 0
int parseTotalAfterSlash(const std::string& text);              // "3/12" -> 12, "3" -> 0
std::optional<double> parseGainDb(const std::string& text);     // "-6.54 dB" -> -6.54
int parseYear(const std::string& text);                         // "2019-05-03" -> 2019

}  // namespace mp::core::library
