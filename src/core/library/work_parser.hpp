#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/library/work_identity.hpp"

namespace mp::core::library {

// ---- Title parsing (SPEC §7b, step 2: "track titles on the same album") -----------------------
//
// Pure functions: no database, no TagLib. Strings are UTF-8.

// Finds the first catalog number in `text` and returns it in one canonical spelling, so the same
// work written two ways ("BWV1007", "BWV 1007") gets one key. Known catalogs (SPEC §7b):
//   BWV 1007   K. 525 (also KV, K 525)   Op. 59 No. 1 (also "Op.59/1", "Opus 67")   D. 960
//   HWV 56   RV 269   Hob. XVI:52 (also Hob. XVI/52)   S. 244
// Returns nullopt if there is none. "D 960" needs a dot or a space; a lone "D" in "Sonata in D" is not a catalog.
std::optional<std::string> findCatalogNumber(std::string_view text);

// Parses a roman numeral ("IV" -> 4). Upper case only, 1..39, canonical forms only ("IIII" is rejected).
std::optional<int> parseRoman(std::string_view text);

struct MovementMarker {
    int number = 0;               // 4 for "IV." and for "4)"
    std::size_t length = 0;       // bytes of the marker incl. its punctuation and following spaces
};
// A movement marker at the start of `text` (after spaces): roman numeral or 1-2 digits, then ".", ")",
// ":" or a dash. "II. Andante" and "2) Andante" and "III - Scherzo" have one; "I Want You" does not.
std::optional<MovementMarker> parseMovementMarker(std::string_view text);

struct TitleParts {
    std::string work;      // text before the separator ("Cello Suite No. 1 in G major, BWV 1007"), "" if none
    std::string movement;  // text after it with the marker removed ("Prélude")
    int movementNo = 0;    // value of the marker, 0 if none
    bool hasMarker = false;
};

// Splits "<work> : <movement>" on ": ", " - ", " – " or " — ". Of several separators the last one that is
// followed by a movement marker wins ("Op. 67 - II. Andante"); with no marker anywhere the first one is used.
// A title with a marker but no separator ("II. Andante") gives work == "" and the marker's number.
// A title with neither returns work == "" and movement == the whole title.
TitleParts splitTitle(std::string_view title);

// A movement glued onto the catalog number without a separator: "Sonata No. 2, BWV 1003 IV. Allegro" ->
// work "Sonata No. 2, BWV 1003", movement 4 "Allegro". nullopt if the title is not shaped like that.
std::optional<TitleParts> splitAfterCatalog(std::string_view title);

// ---- Grouping tracks into works ------------------------------------------------------------

// What the detector needs to know about one track (effective tags: overrides already applied).
struct WorkTrackInput {
    std::int64_t id = 0;
    std::int64_t albumId = 0;  // 0 = the track has no album
    int discNo = 0;
    int trackNo = 0;
    std::string title;
    std::string composer;
    std::string albumArtist;
    std::string workTag;
    std::string movementName;
    int movementNo = 0;
    int movementTotal = 0;
};

enum class WorkSource { None, Tag, Title };

struct WorkAssignment {
    std::int64_t trackId = 0;
    WorkSource source = WorkSource::None;
    std::string workTitle;   // display title of the work
    std::string catalogNo;   // canonical, "" if none
    std::string composer;    // composer tag ("" if the track has none)
    std::string workKey;     // library-wide identity (D24, D36, D39): composer key + work identity, or title if none
    std::string numberKey;   // numberKey() of the title ("f:suite#2"), "" if none: lets catalog-less recordings join
    int movementNo = 0;      // from the tag, else the title, else the position in the recording (1-based)
    std::string movementName;
    bool movementFromTag = false;  // movement number/name came from tags (false: derived)
};

// Decides, for every track, which work it belongs to. Order of the input does not matter; tracks are
// grouped by (album, disc) and ordered by track number. Rules, best source first:
//   1. WORK tag present: that is the work (title = tag). Movement from tags, else from the title.
//   2. Otherwise the title is split (see splitTitle). Tracks of one album+disc that share the text before
//      the separator form a work when
//        - the movement marker is present and (the prefix has a catalog number, the track has a composer
//          tag, or two or more tracks share the prefix), or
//        - there is no marker but two or more tracks share the prefix and (composer tag, catalog number,
//          or a marker on another track of that group).
//   3. A track with a composer tag and a catalog number in its title but no split is a one-movement work.
// Everything else gets WorkSource::None. The returned vector has one entry per input track, same order.
//
// `composers` (optional) makes spelling variants of a composer one person (see ComposerNames); without it the
// normalized tag text is used. Work identity: see identityKey() in work_identity.hpp (D36).
std::vector<WorkAssignment> detectWorks(const std::vector<WorkTrackInput>& tracks, const ComposerNames* composers = nullptr);

// The key under which a work is stored once per library (D24): the composer's key plus the work's identity
// (identityKey(), or the normalized title when the text gives no identity). Exposed for tests.
std::string makeWorkKey(const std::string& composerKey, const std::string& identity, const std::string& title);

}  // namespace mp::core::library
