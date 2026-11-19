#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "core/library/database.hpp"
#include "core/library/search.hpp"

namespace mp::core::library {

// Reads everything searchable from the library (existing tracks only, effective tags from v_tracks):
//  - songs: title; performer; album; the work title, composer and movement name are searched too
//  - albums: title; album artist; the composers of its tracks are searched too
//  - artists: performers and album artists with at least one existing track (count1 = albums led, count2 = tracks)
//  - composers: one per person, however the tags spell the name (D49): works are grouped by the composer part of
//    work_key, the id is the lowest artist id among the spellings, the shown name the longest one
//    (count1 = works, count2 = recordings). Composers with tagged tracks but no detected works come too.
//  - works: title; composer; catalog number (count1 = recordings)
// One pass of a few queries; the caller keeps the index until the library changes.
SearchIndex loadSearchIndex(Database& db);

// A composer as a person (D49). Tags spell one person many ways ("Bach", "Bach, Johann Sebastian", "J.S. Bach"), and
// the library keeps one `artists` row per spelling; works are grouped by the composer part of work_key, which is the
// same for all of them (D36). So a composer is a group of artists rows:
//   id          the lowest artist id of the group: what likes and pages use for this person
//   name        the longest spelling
//   key         the composer key shared by the works ("" for a composer tag no work belongs to)
//   artistIds   every artists row of the group (any of them can be the one a clicked tag names)
struct ComposerSummary {
    std::int64_t id = 0;
    std::string name;
    std::string key;
    int works = 0;       // works with at least one existing track
    int recordings = 0;  // distinct (work, album) pairs
    std::set<std::int64_t> artistIds;
};
// Composers with detected works, plus composer tags that no work belongs to (works = recordings = 0).
std::vector<ComposerSummary> loadComposers(Database& db);

// Composer part of a works.work_key ("<composer key>\x1F<identity>"); "" for a work without composer.
std::string composerKeyOfWorkKey(const std::string& workKey);

}  // namespace mp::core::library
