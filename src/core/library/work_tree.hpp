#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/library/work_identity.hpp"

namespace mp::core::library {

// ---- Works by composer (browse view, D38) -----------------------------------------------------------
//
// Composer -> family -> work, e.g. Bach -> Brandenburg Concerto -> No. 1 ... No. 6. Built from the rows of
// the `works` table; pure, so it can be tested without a database or Qt.

struct WorkEntry {
    std::int64_t id = 0;
    std::string title;
    std::string catalog;
    std::string composer;  // composer tag of the work ("" = none: grouped under "no composer")
    std::string workKey;   // works.work_key: "<composer key>\x1F<identity>"; the composer key groups spellings
    std::vector<std::string> titles;  // every title the work was seen under (works.titles); may be empty
    std::string numberKey;            // works.number_key ("f:suite#2"): its number orders the family
    // works.family_key / family_label, stored by the scan. When !familyKnown they are worked out here from
    // the titles (tests, old rows), which runs regexes: slow in debug builds for a whole library.
    bool familyKnown = false;
    std::string familyKey;
    std::string familyLabel;
    int recordings = 0;
    int tracks = 0;
};

// One line under a composer: a family of two or more works, or a single work (label empty, one entry).
struct WorkFamilyNode {
    std::string label;
    std::string catalogRange;  // "BWV 1007–1012", "BWV 995–997, 1006a"; "" when no work has a catalog number
    std::vector<WorkEntry> works;  // in number, then catalog order
};

struct ComposerNode {
    std::string name;  // the most complete spelling ("Bach, Johann Sebastian" over "Bach"); "" = no composer
    std::vector<WorkFamilyNode> families;  // alphabetical
    int workCount = 0;
};

// What a work's titles say about it, decided at scan time (D38, D40):
//  - key: the family most titles name. A plain form ("Suite") counts for the more specific family of the same
//    form that other titles name ("Cello Suite"); a title without a form only counts when no title has one.
//    The work's own title wins a tie. "" = no family (the work stands alone).
//  - label: the most common wording of that family that names its instrument; if none does ("Partita in D
//    minor for solo violin"), one is built from the key ("Violin Partita").
//  - title: the most common title of that family (case-insensitive); ties go to one without "arr."/"transcr.",
//    then the shorter one.
//  - tie between families: the one the composer already has more works in (`familySize`, may be empty), so a
//    piece its editions call both "Partita" and "Lute Suite" joins the Lute Suites; then the work's own title.
struct WorkFamilyVote {
    std::string key;
    std::string label;
    std::string title;
    bool tied = false;  // the winner shared the most votes with another family
};
WorkFamilyVote familyOfWork(const std::string& title, const std::vector<std::string>& titles,
                            const std::function<int(const std::string& familyKey)>& familySize = {});

// "BWV 1007", "BWV 1008"… -> "BWV 1007–1012": runs of consecutive plain numbers become ranges; numbers with
// a letter ("1006a") stand alone; several prefixes are joined with "; ". Unparseable catalogs are kept as they are.
std::string catalogRange(const std::vector<std::string>& catalogs);

// Composers in surname order, the no-composer group last. A work's family is the one most of its titles
// name (a recording called "Partita for Lute, BWV 997" does not pull the Lute Suite No. 2 out of its family).
std::vector<ComposerNode> groupWorksByComposer(std::vector<WorkEntry> works);

}  // namespace mp::core::library
