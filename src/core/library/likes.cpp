#include "core/library/likes.hpp"

#include <cstddef>
#include <iterator>
#include <string>

namespace mp::core::library {

const char* likeKindName(LikeKind kind) {
    // A table, not a switch: MSVC /W4 reports the fallback after an exhaustive switch as unreachable (C4702).
    static constexpr const char* kNames[] = {"track", "album", "artist", "composer", "work"};
    const auto i = static_cast<std::size_t>(kind);
    return i < std::size(kNames) ? kNames[i] : "track";
}

std::optional<LikeKind> likeKindFromName(std::string_view name) {
    for (const LikeKind kind : {LikeKind::Track, LikeKind::Album, LikeKind::Artist, LikeKind::Composer, LikeKind::Work}) {
        if (name == likeKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

bool Likes::set(LikeKind kind, std::int64_t refId, bool liked, std::int64_t nowSeconds) {
    if (liked) {
        Statement s = db_.prepare("INSERT OR IGNORE INTO likes(kind, ref_id, liked_at) VALUES (?1, ?2, ?3)");
        s.bind(1, likeKindName(kind)).bind(2, refId).bind(3, nowSeconds);
        s.run();
    } else {
        Statement s = db_.prepare("DELETE FROM likes WHERE kind = ?1 AND ref_id = ?2");
        s.bind(1, likeKindName(kind)).bind(2, refId);
        s.run();
    }
    return db_.changes() > 0;
}

std::vector<LikeEntry> Likes::all() {
    std::vector<LikeEntry> out;
    Statement s = db_.prepare("SELECT kind, ref_id, liked_at FROM likes ORDER BY liked_at DESC, kind, ref_id");
    while (s.step()) {
        if (const auto kind = likeKindFromName(s.getText(0))) {
            out.push_back({*kind, s.getInt(1), s.getInt(2)});
        }
    }
    return out;
}

void pruneLikes(Database& db) {
    db.exec(
        "DELETE FROM likes WHERE "
        "(kind = 'album' AND ref_id NOT IN (SELECT id FROM albums)) OR "
        "(kind IN ('artist', 'composer') AND ref_id NOT IN (SELECT id FROM artists)) OR "
        "(kind = 'work' AND ref_id NOT IN (SELECT id FROM works))");
}

void moveWorkLikes(Database& db, std::int64_t fromWorkId, std::int64_t toWorkId) {
    Statement s = db.prepare("UPDATE OR IGNORE likes SET ref_id = ?2 WHERE kind = 'work' AND ref_id = ?1");
    s.bind(1, fromWorkId).bind(2, toWorkId);
    s.run();
}

}  // namespace mp::core::library
