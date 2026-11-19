#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/library/database.hpp"

namespace mp::core::library {

// ---- Likes (SPEC §8b3, D42, D48) ---------------------------------------------------------------------
//
// One table for every kind of thing a person can like. Player-only data: never written to files.
//   track     tracks.id       (ids are never reused, D28)
//   album     albums.id
//   artist    artists.id      performers and album artists
//   composer  artists.id      of the composer's canonical spelling (D49); an artist and a composer can be
//                             the same person, and are liked separately
//   work      works.id
// ids of albums, artists and works can be reused by SQLite after a row is deleted, so a scan calls
// pruneLikes() to drop the likes of rows that are gone (a heart must never jump to another item).

enum class LikeKind { Track, Album, Artist, Composer, Work };

// "track", "album", "artist", "composer", "work": what is stored in likes.kind and what QML passes around.
const char* likeKindName(LikeKind kind);
std::optional<LikeKind> likeKindFromName(std::string_view name);

struct LikeEntry {
    LikeKind kind = LikeKind::Track;
    std::int64_t refId = 0;
    std::int64_t likedAt = 0;  // Unix seconds
};

class Likes {
public:
    explicit Likes(Database& db) : db_(db) {}

    // Likes or unlikes. Idempotent: liking twice keeps the first liked_at. Returns whether anything changed.
    bool set(LikeKind kind, std::int64_t refId, bool liked, std::int64_t nowSeconds);

    // Every like, newest first (ties: by kind, then id, so the order is stable).
    std::vector<LikeEntry> all();

private:
    Database& db_;
};

// Deletes likes whose album, artist/composer or work no longer exists. Tracks are never deleted (D28).
// Called by the scan's entity and work passes inside their transactions.
void pruneLikes(Database& db);

// A liked work moves to the work it was merged into (D39), unless that one is liked already.
void moveWorkLikes(Database& db, std::int64_t fromWorkId, std::int64_t toWorkId);

}  // namespace mp::core::library
