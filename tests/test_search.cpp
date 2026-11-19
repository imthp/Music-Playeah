#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/library/database.hpp"
#include "core/library/search.hpp"
#include "core/library/search_load.hpp"
#include "library_fixtures.hpp"

using namespace mp::core::library;
using mp::test::TempDir;

namespace {

SearchItem item(SearchKind kind, std::int64_t id, std::string title, std::string subtitle = {}, std::string extra = {}) {
    SearchItem i;
    i.kind = kind;
    i.id = id;
    i.title = std::move(title);
    i.subtitle = std::move(subtitle);
    i.extraText = std::move(extra);
    return i;
}

std::vector<std::int64_t> ids(const std::vector<SearchIndex::Hit>& hits) {
    std::vector<std::int64_t> out;
    for (const auto& h : hits) {
        out.push_back(h.item->id);
    }
    return out;
}

}  // namespace

TEST_CASE("Folding ignores case, accents and punctuation", "[search]") {
    CHECK(foldForSearch("Beyoncé") == "beyonce");
    CHECK(foldForSearch("BEYONCÉ") == "beyonce");
    CHECK(foldForSearch("Dvořák") == "dvorak");
    CHECK(foldForSearch("J.S. Bach") == "j s bach");
    CHECK(foldForSearch("  Bach:   Lute   Suites  ") == "bach lute suites");
    CHECK(foldForSearch("Suite in E minor, BWV 996") == "suite in e minor bwv 996");
    CHECK(foldForSearch("Sigur Rós – Ágætis byrjun") == "sigur ros agaetis byrjun");
    CHECK(foldForSearch("Æon Flux") == "aeon flux");
    CHECK(foldForSearch("Łódź") == "lodz");
    CHECK(foldForSearch("Дмитрий Шостакович") == "дмитрий шостакович");  // other scripts stay, lower case
    CHECK(foldForSearch("...") == "");
    CHECK(foldForSearch("") == "");
}

TEST_CASE("Every word of the query must be found", "[search]") {
    SearchIndex index;
    index.add(item(SearchKind::Album, 1, "Bach: Lute Suites", "Halden Moor"));
    index.add(item(SearchKind::Album, 2, "Bach: Cello Suites", "Ondine Ravel"));
    index.add(item(SearchKind::Album, 3, "Lute Songs", "Sofia Arenal"));

    CHECK(ids(index.search("lute", SearchKind::Album, 10)) == std::vector<std::int64_t>{3, 1});  // 3: title starts with it
    CHECK(ids(index.search("lute suite", SearchKind::Album, 10)) == std::vector<std::int64_t>{1});
    CHECK(ids(index.search("suite lute", SearchKind::Album, 10)) == std::vector<std::int64_t>{1});  // word order is free
    CHECK(ids(index.search("lute cello", SearchKind::Album, 10)).empty());
    CHECK(ids(index.search("halden", SearchKind::Album, 10)) == std::vector<std::int64_t>{1});  // the subtitle counts
}

TEST_CASE("Matching ignores accents and punctuation on both sides", "[search]") {
    SearchIndex index;
    index.add(item(SearchKind::Artist, 1, "Beyoncé"));
    index.add(item(SearchKind::Composer, 2, "Bach, J.S."));
    CHECK(ids(index.search("beyonce", SearchKind::Artist, 5)) == std::vector<std::int64_t>{1});
    CHECK(ids(index.search("BEYONCÉ", SearchKind::Artist, 5)) == std::vector<std::int64_t>{1});
    CHECK(ids(index.search("j s bach", SearchKind::Composer, 5)) == std::vector<std::int64_t>{2});
    CHECK(ids(index.search("j.s. bach", SearchKind::Composer, 5)) == std::vector<std::int64_t>{2});
    CHECK(index.search("???", 5).top == std::nullopt);
}

TEST_CASE("Better matches come first", "[search]") {
    SearchIndex index;
    index.add(item(SearchKind::Album, 1, "Songs About Bach"));         // every word starts a word of the title
    index.add(item(SearchKind::Album, 2, "Abachi"));                   // only somewhere inside the title
    index.add(item(SearchKind::Album, 3, "Bach"));                     // the title is the query
    index.add(item(SearchKind::Album, 4, "Bach: Lute Suites"));        // the title starts with it
    index.add(item(SearchKind::Album, 5, "Lute", "Bach Ensemble"));    // only the subtitle has it
    CHECK(ids(index.search("bach", SearchKind::Album, 10)) == std::vector<std::int64_t>{3, 4, 1, 2, 5});
}

TEST_CASE("Searched-only text finds items but is never needed to show them", "[search]") {
    SearchIndex index;
    index.add(item(SearchKind::Song, 1, "IV. Sarabande", "Halden Moor", "Suite in E minor BWV 996 Johann Sebastian Bach"));
    CHECK(ids(index.search("bwv 996", SearchKind::Song, 5)) == std::vector<std::int64_t>{1});
    CHECK(ids(index.search("sebastian sarabande", SearchKind::Song, 5)) == std::vector<std::int64_t>{1});
}

TEST_CASE("The top result prefers a person whose name matches", "[search]") {
    SearchIndex index;
    index.add(item(SearchKind::Album, 1, "Bach: Lute Suites", "Halden Moor"));
    index.add(item(SearchKind::Composer, 2, "Johann Sebastian Bach"));
    index.add(item(SearchKind::Song, 3, "Bach Prelude", "Somebody"));

    auto r = index.search("bach", 5);
    REQUIRE(r.top.has_value());
    CHECK(r.top->item->kind == SearchKind::Composer);

    // ...but an album with exactly that name is what was asked for.
    index.add(item(SearchKind::Album, 4, "Bach"));
    r = index.search("bach", 5);
    REQUIRE(r.top.has_value());
    CHECK(r.top->item->id == 4);

    // A person who only has the word somewhere in the name is not preferred (no bonus below "starts a word").
    SearchIndex other;
    other.add(item(SearchKind::Artist, 1, "Unbacheable"));
    other.add(item(SearchKind::Album, 2, "Bach Variations"));
    CHECK(other.search("bach", 5).top->item->id == 2);
}

TEST_CASE("Results are cut to the limit but the totals stay", "[search]") {
    SearchIndex index;
    for (int i = 0; i < 12; ++i) {
        index.add(item(SearchKind::Song, i, "Prelude " + std::to_string(i)));
    }
    index.add(item(SearchKind::Work, 100, "Prelude and Fugue"));
    const auto r = index.search("prelude", 5);
    CHECK(r.of(SearchKind::Song).size() == 5);
    CHECK(r.totals[static_cast<std::size_t>(SearchKind::Song)] == 12);
    CHECK(r.of(SearchKind::Work).size() == 1);
    CHECK(r.of(SearchKind::Album).empty());
    CHECK(index.search("prelude", SearchKind::Song, 8).size() == 8);
    CHECK(index.search("prelude", SearchKind::Song, 0).empty());
    CHECK(index.search("", 5).top == std::nullopt);
}

TEST_CASE("Equal matches: shorter title first, then bigger, then A to Z", "[search]") {
    SearchIndex index;
    auto album = [&](std::int64_t id, std::string title, int tracks) {
        SearchItem i = item(SearchKind::Album, id, std::move(title));
        i.count1 = tracks;
        index.add(std::move(i));
    };
    album(1, "Suites Two", 8);
    album(2, "Suites One", 20);
    album(3, "Suites", 3);
    album(4, "Suites Zed", 8);
    CHECK(ids(index.search("suites", SearchKind::Album, 10)) == std::vector<std::int64_t>{3, 2, 1, 4});
}

TEST_CASE("The index is read from the database: existing tracks only", "[search][db]") {
    TempDir dir;
    Database db(dir.path() / "lib.db");
    db.migrate();
    db.exec(
        "INSERT INTO artists(id, name, name_key, sort_name) VALUES "
        " (1, 'Halden Moor', 'halden moor', 'halden moor'), (2, 'Bach', 'bach', 'bach'), "
        " (3, 'Johann Sebastian Bach', 'johann sebastian bach', 'bach johann sebastian'), (4, 'Gone Band', 'gone band', 'gone band'), (5, 'Erik Satie', 'erik satie', 'satie erik');"
        "INSERT INTO albums(id, title, album_artist, album_artist_id, art_hash, sort_key) VALUES "
        " (1, 'Lute Suites', 'Halden Moor', 1, 'abc', 'halden moor|lute suites'), (2, 'Lost Album', 'Gone Band', 4, NULL, 'gone band|lost album');"
        "INSERT INTO works(id, composer_id, title, catalog_no, sort_key, work_key) VALUES "
        " (1, 2, 'Suite in E minor, BWV 996', 'BWV 996', 's1', 'bach' || char(31) || 'c:BWV 996'), "
        " (2, 3, 'Suite in C minor, BWV 997', 'BWV 997', 's2', 'bach' || char(31) || 'c:BWV 997');"
        "INSERT INTO tracks(id, path, mtime, size, added_at, title, artist, artist_id, composer, composer_id, album, album_id, work_id, duration_ms, movement_name) VALUES "
        " (1, '/a/1.flac', 1, 1, 1, 'Sarabande', 'Halden Moor', 1, 'Bach', 2, 'Lute Suites', 1, 1, 234000, 'Sarabande'), "
        " (2, '/a/2.flac', 1, 1, 1, 'Prelude', 'Halden Moor', 1, 'Johann Sebastian Bach', 3, 'Lute Suites', 1, 2, 120000, 'Prelude'), "
        " (3, '/a/3.flac', 1, 1, 1, 'Lost Song', 'Gone Band', 4, '', NULL, 'Lost Album', 2, NULL, 1000, ''), "
        " (4, '/a/4.flac', 1, 1, 1, 'Gymnopedie', 'Halden Moor', 1, 'Erik Satie', 5, 'Lute Suites', 1, NULL, 1000, '');"
        "UPDATE tracks SET missing_since = 5 WHERE id = 3;");

    const SearchIndex index = loadSearchIndex(db);

    auto r = index.search("sarabande", 5);
    REQUIRE(r.of(SearchKind::Song).size() == 1);
    const SearchItem& song = *r.of(SearchKind::Song)[0].item;
    CHECK(song.id == 1);
    CHECK(song.albumId == 1);
    CHECK(song.artHash == "abc");
    CHECK(song.durationMs == 234000);
    CHECK(song.subtitle == "Halden Moor");
    CHECK(song.detail == "Lute Suites");

    CHECK(index.search("lost", 5).top == std::nullopt);  // a missing track hides its album and band, too
    CHECK(index.search("gone", 5).top == std::nullopt);

    // The album is found through its composer, the works through their catalog numbers.
    CHECK(ids(index.search("bach", SearchKind::Album, 5)) == std::vector<std::int64_t>{1});
    CHECK(ids(index.search("bwv 997", SearchKind::Work, 5)) == std::vector<std::int64_t>{2});

    // One composer for both spellings: the lowest artist id, the longer name, both works counted.
    const auto composers = index.search("bach", SearchKind::Composer, 5);
    REQUIRE(composers.size() == 1);
    CHECK(composers[0].item->id == 2);
    CHECK(composers[0].item->title == "Johann Sebastian Bach");
    CHECK(composers[0].item->count1 == 2);
    CHECK(composers[0].item->count2 == 2);

    // A composer tag with no detected work still gets a person (and a page), with no works.
    const auto satie = index.search("satie", SearchKind::Composer, 5);
    REQUIRE(satie.size() == 1);
    CHECK(satie[0].item->id == 5);
    CHECK(satie[0].item->count1 == 0);

    const auto artists = index.search("halden", SearchKind::Artist, 5);
    REQUIRE(artists.size() == 1);
    CHECK(artists[0].item->count1 == 1);  // one album led
    CHECK(artists[0].item->count2 == 3);  // three tracks

    // The works carry their composer and catalog number.
    const auto works = index.search("suite", SearchKind::Work, 5);
    REQUIRE(works.size() == 2);
    CHECK(works[0].item->detail.rfind("BWV 99", 0) == 0);
}
