#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mp::core {

// Trims ASCII whitespace (and the no-break space) from both ends.
std::string trimText(std::string_view utf8);

// Folds a name for comparing: trims, collapses runs of whitespace to one space, and lower-cases
// ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic letters. "  The  BEATLES " == "the beatles".
// Not full Unicode case folding (that needs ICU); good enough to merge "Björk"/"BJÖRK" tags.
// Accents are kept: "Beyoncé" and "Beyonce" stay different.
std::string normalizeText(std::string_view utf8);

// Display-independent sort name: normalized, with a leading "the " dropped ("The Beatles" -> "beatles").
std::string sortName(std::string_view utf8);

// std::filesystem::path <-> UTF-8 std::string. The database and the engine store paths as UTF-8
// (D22: the same bytes on every OS); on Windows the native path is UTF-16, so always convert here.
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(std::string_view utf8);

// A path that can be opened by code that uses the plain Windows file APIs, which stop at 260
// characters (MAX_PATH): TagLib, FFmpeg. On Windows a long absolute path becomes "\\?\D:\..." (the
// "extended-length" form); every other path, and every path on other systems, is returned as is.
// Listing folders and reading sizes through std::filesystem worked on long paths in our scan, but
// creating folders did not (the tests need this form for that), so use it whenever in doubt.
std::filesystem::path longPathSafe(const std::filesystem::path& path);

// Same as path.generic_u8string() as std::string: always '/' as separator.
std::string genericUtf8(const std::filesystem::path& path);

}  // namespace mp::core
