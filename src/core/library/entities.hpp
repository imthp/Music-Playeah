#pragma once

#include <cstdint>

#include "core/library/database.hpp"

namespace mp::core::library {

struct ResolveStats {
    std::uint64_t tracks = 0;  // tracks whose artist/album ids were (re)derived
    std::uint64_t albumsCreated = 0;
    std::uint64_t artistsCreated = 0;
};

// Second stage of a scan: derives `artists` and `albums` rows (and the ids on the tracks) from
// the raw tags of every track with `resolved = 0`, then removes albums/artists nothing refers to.
// Runs in one transaction on the caller's connection.
//
// Rules (SPEC §6):
//  - artists are unique on normalizeText(name); performers, album artists and composers share the table.
//  - albums are unique on normalized album artist + album title. A track without an album tag gets no album.
//  - A track without an ALBUMARTIST tag uses its artist. If the tracks of one album in one folder have
//    different artists and none has an album artist, the album is "Various Artists" (a compilation).
ResolveStats resolveEntities(Database& db);

}  // namespace mp::core::library
