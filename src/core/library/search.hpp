#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mp::core::library {

// ---- Library search (SPEC §10 Search, D50) -----------------------------------------------------------
//
// An in-memory index of everything searchable: songs, albums, artists, composers, works. Built from the
// database (search_load.cpp) when the library changes and kept in memory: a lookup is a scan over a few
// thousand short strings (about a millisecond at 6k tracks, still only milliseconds at 100k), so the page
// can answer on every key press without FTS tables, triggers or a second copy of the data in SQLite.
//
// Matching is by words: every word of the query must appear somewhere in the item's text. Case, accents and
// punctuation are ignored ("beyonce" finds "Beyoncé", "j s bach" finds "J.S. Bach", "bwv996" does not find
// "BWV 996" on purpose: words stay words).

enum class SearchKind { Song = 0, Album, Artist, Composer, Work };
inline constexpr std::size_t kSearchKinds = 5;

struct SearchItem {
    SearchKind kind = SearchKind::Song;
    std::int64_t id = 0;       // track, album, artist (also for composers: canonical spelling, D49) or work id
    std::string title;         // what is shown
    std::string subtitle;      // shown with it: performer, album artist, "3 albums · 31 tracks"...
    std::string detail;        // songs: album title; works: catalog number
    std::string extraText;     // searched, never shown: an album's composers, a song's work title
    std::int64_t albumId = 0;  // songs: the album it is on
    std::string artHash;       // cover ("" = none)
    std::int64_t durationMs = 0;
    int count1 = 0;            // album: tracks · artist: albums · composer: works · work: recordings
    int count2 = 0;            // artist: tracks · composer: recordings
};

// Lower case, accents removed, every run of punctuation or white space one space, trimmed:
// "  J.S.  Bach: Dvořák's " -> "j s bach dvorak s". Only for matching, never for display.
std::string foldForSearch(std::string_view utf8);

class SearchIndex {
public:
    struct Hit {
        const SearchItem* item = nullptr;  // valid until the index is changed
        int score = 0;                     // higher is better; 0 never appears
    };
    struct Results {
        std::array<std::vector<Hit>, kSearchKinds> byKind;  // each best first, at most `limit` long
        std::array<std::size_t, kSearchKinds> totals{};     // matches before cutting to `limit`
        std::optional<Hit> top;                             // the best hit over all kinds
        const std::vector<Hit>& of(SearchKind kind) const { return byKind[static_cast<std::size_t>(kind)]; }
    };

    void clear();
    void reserve(std::size_t n);
    void add(SearchItem item);
    std::size_t size() const { return entries_.size(); }

    // All kinds at once. An empty (or punctuation-only) query matches nothing.
    Results search(std::string_view query, std::size_t limit) const;
    // One kind only, with a different limit (the Songs chip shows more than the All view).
    std::vector<Hit> search(std::string_view query, SearchKind kind, std::size_t limit) const;

private:
    struct Entry {
        SearchItem item;
        std::string titleKey;  // folded title
        std::string allKey;    // folded title, subtitle, detail and extra text, one space apart
    };
    std::vector<Entry> entries_;
};

}  // namespace mp::core::library
