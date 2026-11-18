#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/library/database.hpp"

namespace mp::core::library {

// Album art discovery (SPEC §6 "Album art", D9). Qt-free: this finds *where* an album's cover is and gives it
// a content hash; decoding and the 256/600 px thumbnails are done by the app's image provider (it needs an
// image library). Order: user override (image_overrides, handled by whoever reads it) → embedded picture →
// cover.* / folder.* / front.* in the album's folder.

using Bytes = std::vector<std::uint8_t>;

// The front cover embedded in an audio file (FLAC/Ogg picture blocks, ID3 APIC, MP4 covr, ...), or empty.
// Prefers a picture typed "Front Cover", else the first picture. Safe to call from several threads.
Bytes readEmbeddedPicture(const std::filesystem::path& file);

// cover.* / folder.* / front.* (jpg, jpeg, png, bmp, webp; any letter case) in `folder`, or nullopt.
// In that name order when several exist.
std::optional<std::filesystem::path> findCoverFile(const std::filesystem::path& folder);

// 16 hex digits (FNV-1a, 64 bit) of the image bytes. A cache key, not a security measure: the same image
// on two albums gets the same hash and so the same thumbnail files.
std::string hashImage(const Bytes& bytes);

// Sources are stored in `albums.art_source` as "embedded:<track path>" or "file:<path>" (UTF-8, '/').
// Returns the image bytes, or empty if the source is gone or has no picture any more.
Bytes loadArtSource(const std::string& source);

// "png", "jpg", "gif", "webp", "bmp" from the first bytes; "" if not recognised.
std::string imageExtension(const Bytes& bytes);

struct ArtStats {
    std::uint64_t albumsChecked = 0;
    std::uint64_t withArt = 0;
};

// Fourth scan stage: for every album with art_checked = 0 looks for a cover and stores art_hash and
// art_source (both NULL when there is none). Looks at the first tracks of the album (disc, track order)
// for an embedded picture, then at the folder of the first track. `cancel` is polled between albums.
// Commits in batches on the caller's connection so a cancel keeps what was found.
ArtStats resolveArt(Database& db, const std::atomic<bool>* cancel = nullptr);

}  // namespace mp::core::library
