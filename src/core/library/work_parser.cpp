#include "core/library/work_parser.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <regex>
#include <tuple>

#include "core/text.hpp"

namespace mp::core::library {

namespace {

constexpr char kKeySeparator = '\x1F';
constexpr std::string_view kEnDash = "\xE2\x80\x93";
constexpr std::string_view kEmDash = "\xE2\x80\x94";

bool isSpace(char c) { return c == ' ' || c == '\t'; }

std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string upperAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string toRoman(int n) {
    static constexpr std::pair<int, const char*> kSteps[] = {{10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
    std::string out;
    for (const auto& [value, text] : kSteps) {
        while (n >= value) {
            out += text;
            n -= value;
        }
    }
    return out;
}

// ---- catalog numbers ------------------------------------------------------------------------

struct CatalogPattern {
    std::regex re;
    std::function<std::string(const std::smatch&)> format;
};

const std::vector<CatalogPattern>& catalogPatterns() {
    using std::regex_constants::ECMAScript;
    using std::regex_constants::icase;
    static const std::vector<CatalogPattern> patterns = [] {
        std::vector<CatalogPattern> p;
        p.push_back({std::regex(R"(\bBWV\.?\s*(\d{1,4}[a-z]?)\b)", ECMAScript | icase),
                     [](const std::smatch& m) { return "BWV " + lowerAscii(m[1].str()); }});
        p.push_back({std::regex(R"(\bHWV\.?\s*(\d{1,3}[a-z]?)\b)", ECMAScript | icase),
                     [](const std::smatch& m) { return "HWV " + lowerAscii(m[1].str()); }});
        p.push_back({std::regex(R"(\bRV\.?\s*(\d{1,3}[a-z]?)\b)", ECMAScript | icase),
                     [](const std::smatch& m) { return "RV " + lowerAscii(m[1].str()); }});
        p.push_back({std::regex(R"(\bHob\.?\s*([IVX]{1,5})\s*[:/.]\s*(\d{1,3}[a-z]?)\b)", ECMAScript | icase),
                     [](const std::smatch& m) { return "Hob. " + upperAscii(m[1].str()) + ":" + lowerAscii(m[2].str()); }});
        // Op. 59 No. 1 / Op.59/1 / Opus 67. Case-insensitive: "OP. 67" happens.
        p.push_back({std::regex(R"(\bOp(?:us)?\.?\s*(\d{1,3}[a-z]?)(?:\s*,?\s*(?:(?:No|Nr|n[°º])\.?\s*|/)(\d{1,2})\b)?)",
                                ECMAScript | icase),
                     [](const std::smatch& m) {
                         std::string s = "Op. " + lowerAscii(m[1].str());
                         if (m[2].matched) {
                             s += " No. " + m[2].str();
                         }
                         return s;
                     }});
        // Case-sensitive: "K" and "D" are also ordinary letters.
        p.push_back({std::regex(R"(\b(?:KV|K)\.?\s*(\d{1,4}[a-f]?)\b)", ECMAScript),
                     [](const std::smatch& m) { return "K. " + m[1].str(); }});
        p.push_back({std::regex(R"(\bD(?:\.\s*|\s+)(\d{1,3}[a-z]?)\b)", ECMAScript),
                     [](const std::smatch& m) { return "D. " + m[1].str(); }});
        p.push_back({std::regex(R"(\bS\.\s*(\d{1,4}[a-z]?)\b)", ECMAScript),
                     [](const std::smatch& m) { return "S. " + m[1].str(); }});
        return p;
    }();
    return patterns;
}

// ---- splitting ------------------------------------------------------------------------------

struct Separator {
    std::size_t pos = 0;
    std::size_t length = 0;
};

std::vector<Separator> findSeparators(std::string_view s) {
    std::vector<Separator> found;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == ':' && i + 1 < s.size() && isSpace(s[i + 1])) {
            found.push_back({i, 2});
        } else if (s[i] == ':' && i + 1 < s.size() && parseMovementMarker(s.substr(i + 1))) {
            found.push_back({i, 1});  // "Suite in E minor, BWV 996 (Arr. for Guitar):IV. Sarabande"
        } else if (isSpace(s[i])) {
            if (s.compare(i + 1, 2, "- ") == 0) {
                found.push_back({i, 3});
            } else if (s.compare(i + 1, kEnDash.size() + 1, std::string(kEnDash) + " ") == 0) {
                found.push_back({i, 1 + kEnDash.size() + 1});
            } else if (s.compare(i + 1, kEmDash.size() + 1, std::string(kEmDash) + " ") == 0) {
                found.push_back({i, 1 + kEmDash.size() + 1});
            }
        }
    }
    return found;
}

std::string trimmed(std::string_view s) { return mp::core::trimText(s); }

}  // namespace

namespace {

struct CatalogMatch {
    std::string number;
    std::size_t pos = 0;
    std::size_t length = 0;
};

// The earliest catalog number in the text, with where it stands.
std::optional<CatalogMatch> findCatalogMatch(std::string_view text) {
    // Every catalog number contains a digit; most titles do not, and regex searches are the slow part of a scan.
    if (std::none_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }
    std::string haystack(text);
    // "Bach autrement II (Inspired by Prelude, BWV 1007)": a catalog number after "inspired by", "based on",
    // "homage"… names another work. Blanked up to the closing bracket (same length, so positions stay).
    static const std::regex kNotThisWork(R"((?:inspired\s+by|based\s+on|hommage|homage|tribute\s+to)[^)\]]*)",
                                         std::regex_constants::ECMAScript | std::regex_constants::icase);
    std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> blanks;
    for (std::sregex_iterator it(haystack.begin(), haystack.end(), kNotThisWork), end; it != end; ++it) {
        blanks.emplace_back(it->position(0), it->length(0));
    }
    for (const auto& [pos, len] : blanks) {
        std::fill_n(haystack.begin() + pos, len, ' ');
    }
    std::optional<CatalogMatch> best;
    // A composer's thematic catalog (BWV, K., RV, D., HWV, Hob., S.) names a piece once and for all; an opus
    // number is the publisher's and often comes beside it ("op. 8 no. 1, RV 269"). Thematic first, then the
    // earliest in the text.
    std::optional<CatalogMatch> opus;
    for (const CatalogPattern& p : catalogPatterns()) {
        std::smatch m;
        if (std::regex_search(haystack, m, p.re)) {
            const auto pos = static_cast<std::size_t>(m.position(0));
            CatalogMatch found{p.format(m), pos, static_cast<std::size_t>(m.length(0))};
            std::optional<CatalogMatch>& slot = found.number.rfind("Op.", 0) == 0 ? opus : best;
            if (!slot || pos < slot->pos) {
                slot = std::move(found);
            }
        }
    }
    return best ? best : opus;
}

// "Suite Bwv 812, No. 1" -> strips ", No. 1" from `work` and returns 1, when it follows a catalog number that
// has no sub-numbers of its own (every catalog but opus, whose "Op. 28 No. 4" the catalog pattern takes whole).
std::optional<int> trailingMovementNumber(std::string& work) {
    const std::optional<CatalogMatch> m = findCatalogMatch(work);
    if (!m || m->number.rfind("Op.", 0) == 0) {
        return std::nullopt;
    }
    static const std::regex kNo(R"(^\s*,?\s*(?:no|nr|n\xC2[\xB0\xBA])\.?\s*(\d{1,2})\s*$)",
                                std::regex_constants::ECMAScript | std::regex_constants::icase);
    const std::string rest = work.substr(m->pos + m->length);
    std::smatch no;
    if (!std::regex_match(rest, no, kNo)) {
        return std::nullopt;
    }
    const int n = std::stoi(no[1].str());
    if (n <= 0) {
        return std::nullopt;
    }
    work.resize(m->pos + m->length);
    return n;
}

}  // namespace

std::optional<std::string> findCatalogNumber(std::string_view text) {
    if (std::optional<CatalogMatch> m = findCatalogMatch(text)) {
        return std::move(m->number);
    }
    return std::nullopt;
}

std::optional<TitleParts> splitAfterCatalog(std::string_view title) {
    const std::optional<CatalogMatch> m = findCatalogMatch(title);
    if (!m) {
        return std::nullopt;
    }
    const std::size_t end = m->pos + m->length;
    const std::string_view rest = title.substr(end);
    if (rest.empty() || !isSpace(rest.front())) {
        return std::nullopt;  // "BWV 1003/IV" or the end of the title
    }
    const std::optional<MovementMarker> marker = parseMovementMarker(rest);
    if (!marker) {
        return std::nullopt;
    }
    TitleParts parts;
    parts.work = trimmed(title.substr(0, end));
    parts.movement = trimmed(rest.substr(marker->length));
    parts.movementNo = marker->number;
    parts.hasMarker = true;
    if (parts.movement.empty()) {
        return std::nullopt;
    }
    return parts;
}

std::optional<int> parseRoman(std::string_view text) {
    if (text.empty() || text.size() > 8) {
        return std::nullopt;
    }
    int total = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto value = [](char c) { return c == 'I' ? 1 : (c == 'V' ? 5 : (c == 'X' ? 10 : 0)); };
        const int v = value(text[i]);
        if (v == 0) {
            return std::nullopt;
        }
        const int next = i + 1 < text.size() ? value(text[i + 1]) : 0;
        total += v < next ? -v : v;
    }
    if (total < 1 || total > 39 || toRoman(total) != text) {
        return std::nullopt;
    }
    return total;
}

std::optional<MovementMarker> parseMovementMarker(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && isSpace(text[i])) {
        ++i;
    }
    const std::size_t start = i;
    int number = 0;
    if (i < text.size() && (text[i] == 'I' || text[i] == 'V' || text[i] == 'X')) {
        while (i < text.size() && (text[i] == 'I' || text[i] == 'V' || text[i] == 'X')) {
            ++i;
        }
        const std::optional<int> n = parseRoman(text.substr(start, i - start));
        if (!n) {
            return std::nullopt;
        }
        number = *n;
    } else if (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0 && i - start < 3) {
            number = number * 10 + (text[i] - '0');
            ++i;
        }
        if (i - start > 2 || number == 0) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    while (i < text.size() && isSpace(text[i])) {
        ++i;
    }
    if (i >= text.size()) {
        return std::nullopt;
    }
    std::size_t punct = 0;
    const char c = text[i];
    if (c == '.' || c == ')' || c == ':' || c == '-') {
        punct = 1;
    } else if (text.compare(i, kEnDash.size(), kEnDash) == 0) {
        punct = kEnDash.size();
    } else if (text.compare(i, kEmDash.size(), kEmDash) == 0) {
        punct = kEmDash.size();
    } else {
        return std::nullopt;
    }
    // "1.5" or "2.0 Remaster": a number followed by "." and another digit is not a movement marker.
    if (c == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0) {
        return std::nullopt;
    }
    i += punct;
    while (i < text.size() && isSpace(text[i])) {
        ++i;
    }
    return MovementMarker{number, i};
}

TitleParts splitTitle(std::string_view title) {
    TitleParts parts;
    const std::string_view whole = title;

    struct Candidate {
        std::string left;
        std::string right;
        std::optional<MovementMarker> marker;
    };
    std::vector<Candidate> candidates;
    for (const Separator& sep : findSeparators(whole)) {
        Candidate c;
        c.left = trimmed(whole.substr(0, sep.pos));
        c.right = trimmed(whole.substr(sep.pos + sep.length));
        if (c.left.empty() || c.right.empty()) {
            continue;
        }
        if (parseMovementMarker(c.left)) {
            continue;  // "IV. Menuet II - V. Menuet I": two movements, not work + movement
        }
        c.marker = parseMovementMarker(c.right);
        candidates.push_back(std::move(c));
    }

    const Candidate* chosen = nullptr;
    for (const Candidate& c : candidates) {
        if (c.marker) {
            chosen = &c;  // the last one with a marker
        }
    }
    if (chosen == nullptr) {
        // No separator: "Sonata No. 2 in A minor, BWV 1003 IV. Allegro".
        if (std::optional<TitleParts> glued = splitAfterCatalog(whole)) {
            return *glued;
        }
    }
    if (chosen == nullptr && !candidates.empty()) {
        chosen = &candidates.front();
    }

    if (chosen != nullptr) {
        parts.work = chosen->left;
        if (!chosen->marker) {
            // "Suite Bwv 812, No. 1 - Allemande": BWV numbers have no sub-numbers, so "No. 1" is the movement.
            if (const std::optional<int> n = trailingMovementNumber(parts.work)) {
                parts.hasMarker = true;
                parts.movementNo = *n;
                parts.movement = chosen->right;
                return parts;
            }
        }
        if (chosen->marker) {
            parts.hasMarker = true;
            parts.movementNo = chosen->marker->number;
            const std::string rest = trimmed(std::string_view(chosen->right).substr(chosen->marker->length));
            parts.movement = rest.empty() ? chosen->right : rest;
        } else {
            parts.movement = chosen->right;
        }
        return parts;
    }

    if (const std::optional<MovementMarker> marker = parseMovementMarker(whole)) {
        parts.hasMarker = true;
        parts.movementNo = marker->number;
        const std::string rest = trimmed(whole.substr(marker->length));
        parts.movement = rest.empty() ? trimmed(whole) : rest;
        return parts;
    }
    parts.movement = trimmed(whole);
    return parts;
}

std::string makeWorkKey(const std::string& composerKey, const std::string& identity, const std::string& title) {
    std::string key = composerKey;
    key += kKeySeparator;
    key += identity.empty() ? "t:" + normalizeText(title) : identity;
    return key;
}

// ---- detection ------------------------------------------------------------------------------

namespace {

// "J.S. Bach: Prelude in C Minor, BWV 999" -> "Prelude in C Minor, BWV 999" when the part before the colon
// is the track's composer (same surname, no digits, at most four words). Otherwise unchanged.
std::string withoutComposerPrefix(const std::string& title, const std::string& composer) {
    const std::size_t colon = title.find(": ");
    if (composer.empty() || colon == std::string::npos || colon == 0) {
        return title;
    }
    const std::string left = title.substr(0, colon);
    if (std::any_of(left.begin(), left.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) ||
        std::count(left.begin(), left.end(), ' ') > 3) {
        return title;
    }
    const ComposerName a = parseComposerName(left);
    const ComposerName b = parseComposerName(composer);
    if (a.surname.empty() || a.surname != b.surname) {
        return title;
    }
    return trimmed(std::string_view(title).substr(colon + 2));
}

struct TrackFacts {
    std::string title;  // the track title without a leading "<composer>: "
    TitleParts parts;
    std::optional<std::string> catalogInTitle;
    std::optional<std::string> catalogInPrefix;
    std::string prefixKey;  // normalized parts.work
};

}  // namespace

std::vector<WorkAssignment> detectWorks(const std::vector<WorkTrackInput>& tracks, const ComposerNames* composers) {
    std::vector<WorkAssignment> out(tracks.size());
    std::vector<TrackFacts> facts(tracks.size());

    // Group by (album, disc); a track without an album stands alone.
    std::map<std::pair<std::int64_t, int>, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const WorkTrackInput& t = tracks[i];
        out[i].trackId = t.id;
        facts[i].title = withoutComposerPrefix(t.title, t.composer);
        facts[i].parts = splitTitle(facts[i].title);
        facts[i].catalogInTitle = findCatalogNumber(facts[i].title);
        if (!facts[i].parts.work.empty()) {
            facts[i].catalogInPrefix = findCatalogNumber(facts[i].parts.work);
            facts[i].prefixKey = normalizeText(facts[i].parts.work);
        }
        const std::int64_t groupAlbum = t.albumId != 0 ? t.albumId : -t.id;
        groups[{groupAlbum, t.albumId != 0 ? t.discNo : 0}].push_back(i);
    }

    auto assign = [&](std::size_t i, WorkSource source, std::string title, std::string catalog) {
        WorkAssignment& a = out[i];
        a.source = source;
        a.workTitle = std::move(title);
        a.catalogNo = std::move(catalog);
        a.composer = tracks[i].composer;
        const std::string& owner = tracks[i].composer.empty() ? tracks[i].albumArtist : tracks[i].composer;
        const std::string ownerKey = composers != nullptr ? composers->key(owner) : normalizeText(owner);
        WorkIdentity identity = identifyWork(a.workTitle);
        if (identity.catalog.empty()) {
            identity.catalog = a.catalogNo;
        }
        a.workKey = makeWorkKey(ownerKey, identityKey(identity), a.workTitle);
        a.numberKey = numberKey(identity);
    };

    for (auto& [groupKey, members] : groups) {
        std::stable_sort(members.begin(), members.end(), [&](std::size_t a, std::size_t b) {
            if (tracks[a].trackNo != tracks[b].trackNo) {
                return tracks[a].trackNo < tracks[b].trackNo;
            }
            return tracks[a].id < tracks[b].id;
        });

        // 1. WORK tags.
        for (const std::size_t i : members) {
            const WorkTrackInput& t = tracks[i];
            if (t.workTag.empty()) {
                continue;
            }
            std::optional<std::string> catalog = findCatalogNumber(t.workTag);
            if (!catalog) {
                catalog = facts[i].catalogInTitle;
            }
            // Some taggers copy the first track's title into WORK ("…, BWV 1001 I. Adagio") or start it with
            // the composer ("Beethoven: Egmont Overture, Op. 84").
            const std::string workTag = withoutComposerPrefix(t.workTag, t.composer);
            const std::optional<TitleParts> glued = splitAfterCatalog(workTag);
            assign(i, WorkSource::Tag, glued ? glued->work : workTag, catalog.value_or(std::string()));

            WorkAssignment& a = out[i];
            const TitleParts& p = facts[i].parts;
            a.movementFromTag = !t.movementName.empty() || t.movementNo > 0;
            a.movementName = t.movementName;
            if (a.movementName.empty()) {
                // The title often repeats the work: "Work: I. Movement". Without a split it is the movement itself.
                a.movementName = p.work.empty() ? (p.hasMarker ? p.movement : trimmed(t.title)) : p.movement;
            }
            a.movementNo = t.movementNo > 0 ? t.movementNo : (p.hasMarker ? p.movementNo : 0);
        }

        // 2. Titles: count how many untagged tracks share each prefix.
        std::map<std::string, int> prefixCount;
        std::map<std::string, bool> prefixHasMarker;
        for (const std::size_t i : members) {
            if (out[i].source == WorkSource::None && !facts[i].prefixKey.empty()) {
                ++prefixCount[facts[i].prefixKey];
                if (facts[i].parts.hasMarker) {
                    prefixHasMarker[facts[i].prefixKey] = true;
                }
            }
        }
        for (const std::size_t i : members) {
            if (out[i].source != WorkSource::None) {
                continue;
            }
            const TrackFacts& f = facts[i];
            const bool composer = !tracks[i].composer.empty();
            if (!f.prefixKey.empty()) {
                const int count = prefixCount[f.prefixKey];
                const bool catalog = f.catalogInPrefix.has_value();
                bool qualifies;
                if (f.parts.hasMarker) {
                    qualifies = count >= 2 || composer || catalog;
                } else {
                    qualifies = (count >= 2 && (composer || catalog || prefixHasMarker[f.prefixKey])) ||
                                (composer && catalog);
                }
                if (qualifies) {
                    assign(i, WorkSource::Title, f.parts.work, f.catalogInPrefix.value_or(std::string()));
                    out[i].movementName = f.parts.movement;
                    out[i].movementNo = f.parts.hasMarker ? f.parts.movementNo : 0;
                    continue;
                }
            }
            // 3. A lone track with a composer and a catalog number: a one-movement work.
            if (f.prefixKey.empty() && composer && f.catalogInTitle) {
                assign(i, WorkSource::Title, trimmed(f.title), *f.catalogInTitle);
                out[i].movementName = out[i].workTitle;
                out[i].movementNo = 1;
            }
        }

        // Movements without a number take their position within the recording (same work, same group).
        std::map<std::string, int> position;
        for (const std::size_t i : members) {
            if (out[i].source == WorkSource::None) {
                continue;
            }
            const int pos = ++position[out[i].workKey];
            if (out[i].movementNo == 0) {
                out[i].movementNo = pos;
            }
            if (out[i].movementName.empty()) {
                out[i].movementName = trimmed(tracks[i].title);
            }
        }
    }
    return out;
}

}  // namespace mp::core::library
