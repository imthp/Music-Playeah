#include <catch2/catch_test_macros.hpp>

#include "core/library/database.hpp"
#include "core/library/likes.hpp"
#include "library_fixtures.hpp"

using namespace mp::core::library;
using mp::test::TempDir;

namespace {

void exec(Database& db, const char* sql) { db.exec(sql); }

}  // namespace

TEST_CASE("Kind names round-trip and unknown names are refused", "[likes]") {
    for (const LikeKind kind : {LikeKind::Track, LikeKind::Album, LikeKind::Artist, LikeKind::Composer, LikeKind::Work}) {
        const auto back = likeKindFromName(likeKindName(kind));
        REQUIRE(back.has_value());
        CHECK(*back == kind);
    }
    CHECK_FALSE(likeKindFromName("playlist").has_value());
    CHECK_FALSE(likeKindFromName("").has_value());
}

TEST_CASE("A like is stored once and keeps its first time", "[likes]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.migrate();
    Likes likes(db);

    CHECK(likes.set(LikeKind::Track, 7, true, 1000));
    CHECK_FALSE(likes.set(LikeKind::Track, 7, true, 2000));  // already liked: nothing changes
    auto all = likes.all();
    REQUIRE(all.size() == 1);
    CHECK(all[0].kind == LikeKind::Track);
    CHECK(all[0].refId == 7);
    CHECK(all[0].likedAt == 1000);

    CHECK(likes.set(LikeKind::Track, 7, false, 3000));
    CHECK_FALSE(likes.set(LikeKind::Track, 7, false, 3000));  // not liked any more
    CHECK(likes.all().empty());
}

TEST_CASE("The same id can be liked as several kinds, newest first", "[likes]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.migrate();
    Likes likes(db);
    likes.set(LikeKind::Artist, 5, true, 100);
    likes.set(LikeKind::Composer, 5, true, 300);  // the same artists row, liked as a composer
    likes.set(LikeKind::Album, 5, true, 200);

    const auto all = likes.all();
    REQUIRE(all.size() == 3);
    CHECK(all[0].kind == LikeKind::Composer);
    CHECK(all[1].kind == LikeKind::Album);
    CHECK(all[2].kind == LikeKind::Artist);

    likes.set(LikeKind::Composer, 5, false, 400);  // unliking one kind leaves the others
    CHECK(likes.all().size() == 2);
}

TEST_CASE("Pruning drops likes of rows that are gone and keeps the rest", "[likes]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.migrate();
    exec(db, "INSERT INTO artists(id, name, name_key, sort_name) VALUES (1, 'A', 'a', 'a')");
    exec(db, "INSERT INTO albums(id, title, album_artist, sort_key) VALUES (1, 'T', 'A', 'a|t')");
    exec(db, "INSERT INTO works(id, title, sort_key, work_key) VALUES (1, 'W', 'w', 'a\x1F" "c:X 1')");
    exec(db, "INSERT INTO tracks(id, path, mtime, size, added_at) VALUES (1, '/x.flac', 1, 1, 1)");

    Likes likes(db);
    for (const std::int64_t id : {1, 2}) {  // 1 exists, 2 never did (or was deleted)
        for (const LikeKind kind : {LikeKind::Album, LikeKind::Artist, LikeKind::Composer, LikeKind::Work, LikeKind::Track}) {
            likes.set(kind, id, true, 10);
        }
    }
    REQUIRE(likes.all().size() == 10);

    pruneLikes(db);

    const auto all = likes.all();
    CHECK(all.size() == 6);  // 4 kinds with row 1, plus track 1 and track 2 (tracks are never deleted, D28)
    for (const LikeEntry& e : all) {
        if (e.kind != LikeKind::Track) {
            CHECK(e.refId == 1);
        }
    }
}

TEST_CASE("A merged work keeps its like", "[likes]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.migrate();
    Likes likes(db);

    likes.set(LikeKind::Work, 10, true, 100);
    moveWorkLikes(db, 10, 20);
    auto all = likes.all();
    REQUIRE(all.size() == 1);
    CHECK(all[0].refId == 20);

    // Both were liked: one like remains, the survivor's own, and nothing throws.
    likes.set(LikeKind::Work, 10, true, 200);
    moveWorkLikes(db, 10, 20);
    all = likes.all();
    CHECK(all.size() == 2);  // the unmovable like of 10 stays until pruning removes the deleted work
    pruneLikes(db);          // works 10 and 20 do not exist in this database
    CHECK(likes.all().empty());
}
