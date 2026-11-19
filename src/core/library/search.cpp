#include "core/library/search.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "core/text.hpp"

namespace mp::core::library {

namespace {

// Latin-1 Supplement and Latin Extended-A letters (U+00C0..U+017F) as plain letters. "" = keep the letter.
// Generated from Unicode decompositions, with the few that do not decompose (ß æ ø ð þ đ ł ħ ı œ ŧ...) by hand.
constexpr const char* kLatin[0x180 - 0xC0] = {
    "a", "a", "a", "a", "a", "a", "ae", "c",
    "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", " ",
    "o", "u", "u", "u", "u", "y", "th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c",
    "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", " ",
    "o", "u", "u", "u", "u", "y", "th", "y",
    "a", "a", "a", "a", "a", "a", "c", "c",
    "c", "c", "c", "c", "c", "c", "d", "d",
    "d", "d", "e", "e", "e", "e", "e", "e",
    "e", "e", "e", "e", "g", "g", "g", "g",
    "g", "g", "g", "g", "h", "h", "h", "h",
    "i", "i", "i", "i", "i", "i", "i", "i",
    "i", "i", "ij", "ij", "j", "j", "k", "k",
    "", "l", "l", "l", "l", "l", "l", "l",
    "l", "l", "l", "n", "n", "n", "n", "n",
    "n", "n", "", "", "o", "o", "o", "o",
    "o", "o", "oe", "oe", "r", "r", "r", "r",
    "r", "r", "s", "s", "s", "s", "s", "s",
    "s", "s", "t", "t", "t", "t", "t", "t",
    "u", "u", "u", "u", "u", "u", "u", "u",
    "u", "u", "u", "u", "w", "w", "y", "y",
    "y", "z", "z", "z", "z", "z", "z", "s",
};

// One code point from UTF-8 at `i` (advances i). Invalid bytes come back as themselves, so nothing is lost.
char32_t nextCodePoint(std::string_view s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    auto cont = [&](std::size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && cont(1)) {
        const char32_t cp = (static_cast<char32_t>(b0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        const char32_t cp = (static_cast<char32_t>(b0 & 0x0F) << 12) | (static_cast<char32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        const char32_t cp = (static_cast<char32_t>(b0 & 0x07) << 18) | (static_cast<char32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                            (static_cast<char32_t>(static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    return b0;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Typographic punctuation outside ASCII that separates words like an ASCII one does.
bool isWideSeparator(char32_t cp) {
    return cp == 0x00A0 || cp == 0x00AB || cp == 0x00BB || cp == 0x00B7 || (cp >= 0x2010 && cp <= 0x2027) ||
           cp == 0x2030 || cp == 0x3000 || cp == 0x00A1 || cp == 0x00BF;
}

bool isAsciiAlnum(char32_t cp) { return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z'); }

// Position of `word` in `text` where it starts a word (at the start or after a space), or npos.
std::size_t findWordStart(const std::string& text, const std::string& word) {
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
        if (at == 0 || text[at - 1] == ' ') {
            return at;
        }
    }
    return std::string::npos;
}

std::vector<std::string> splitWords(const std::string& folded) {
    std::vector<std::string> words;
    for (std::size_t start = 0; start < folded.size();) {
        std::size_t end = folded.find(' ', start);
        if (end == std::string::npos) {
            end = folded.size();
        }
        if (end > start) {
            words.push_back(folded.substr(start, end - start));
        }
        start = end + 1;
    }
    return words;
}

// How well one entry matches: 0 = not at all.
//   100 the title is the query · 80 the title starts with it · 60 every word starts a word of the title ·
//   40 every word is somewhere in the title · 25 every word starts a word of the rest · 10 somewhere in the rest.
int scoreOf(const std::string& titleKey, const std::string& allKey, const std::string& query, const std::vector<std::string>& words) {
    for (const std::string& w : words) {
        if (allKey.find(w) == std::string::npos) {
            return 0;
        }
    }
    if (titleKey == query) {
        return 100;
    }
    if (titleKey.compare(0, query.size(), query) == 0) {
        return 80;
    }
    bool inTitle = true;
    bool startsInTitle = true;
    bool startsAnywhere = true;
    for (const std::string& w : words) {
        if (titleKey.find(w) == std::string::npos) {
            inTitle = false;
        }
        if (findWordStart(titleKey, w) == std::string::npos) {
            startsInTitle = false;
        }
        if (findWordStart(allKey, w) == std::string::npos) {
            startsAnywhere = false;
        }
    }
    if (startsInTitle) {
        return 60;
    }
    if (inTitle) {
        return 40;
    }
    return startsAnywhere ? 25 : 10;
}

// A person who matches by name is usually what the query is about ("bach" is Bach, not an album of his).
constexpr int kPersonBonus = 25;

}  // namespace

std::string foldForSearch(std::string_view utf8) {
    const std::string lower = normalizeText(utf8);  // case folding (Latin, Greek, Cyrillic) and white space
    std::string out;
    out.reserve(lower.size());
    bool space = true;  // true at the start: no leading space
    auto putSpace = [&] {
        if (!space) {
            out += ' ';
            space = true;
        }
    };
    for (std::size_t i = 0; i < lower.size();) {
        const char32_t cp = nextCodePoint(lower, i);
        if (isAsciiAlnum(cp)) {
            out += static_cast<char>(cp);
            space = false;
        } else if (cp < 0x80 || isWideSeparator(cp)) {
            putSpace();
        } else if (cp >= 0xC0 && cp < 0x180 && kLatin[cp - 0xC0][0] != '\0') {
            const std::string_view plain = kLatin[cp - 0xC0];
            if (plain == " ") {
                putSpace();
            } else {
                out += plain;
                space = false;
            }
        } else {
            appendUtf8(out, cp);
            space = false;
        }
    }
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

void SearchIndex::clear() { entries_.clear(); }

void SearchIndex::reserve(std::size_t n) { entries_.reserve(n); }

void SearchIndex::add(SearchItem item) {
    Entry e;
    e.titleKey = foldForSearch(item.title);
    std::string all = e.titleKey;
    for (const std::string* part : {&item.subtitle, &item.detail, &item.extraText}) {
        const std::string folded = foldForSearch(*part);
        if (!folded.empty()) {
            all += ' ';
            all += folded;
        }
    }
    e.allKey = std::move(all);
    e.item = std::move(item);
    entries_.push_back(std::move(e));
}

namespace {

bool better(const SearchIndex::Hit& a, const SearchIndex::Hit& b, const std::string& keyA, const std::string& keyB) {
    if (a.score != b.score) {
        return a.score > b.score;
    }
    // Same score: the shorter title is the closer match, then the one with more in it (a bigger album), then A–Z.
    if (keyA.size() != keyB.size()) {
        return keyA.size() < keyB.size();
    }
    if (a.item->count1 != b.item->count1) {
        return a.item->count1 > b.item->count1;
    }
    return keyA < keyB;
}

}  // namespace

SearchIndex::Results SearchIndex::search(std::string_view query, std::size_t limit) const {
    Results results;
    const std::string q = foldForSearch(query);
    if (q.empty()) {
        return results;
    }
    const std::vector<std::string> words = splitWords(q);

    // Collect every match (index into entries_), per kind.
    std::array<std::vector<std::pair<int, std::size_t>>, kSearchKinds> matches;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        const int score = scoreOf(e.titleKey, e.allKey, q, words);
        if (score > 0) {
            matches[static_cast<std::size_t>(e.item.kind)].emplace_back(score, i);
        }
    }

    std::optional<std::pair<int, std::size_t>> top;  // rated with the person bonus
    for (std::size_t k = 0; k < kSearchKinds; ++k) {
        auto& list = matches[k];
        results.totals[k] = list.size();
        auto lessThan = [&](const std::pair<int, std::size_t>& a, const std::pair<int, std::size_t>& b) {
            const Entry& ea = entries_[a.second];
            const Entry& eb = entries_[b.second];
            return better({&ea.item, a.first}, {&eb.item, b.first}, ea.titleKey, eb.titleKey);
        };
        const std::size_t keep = std::min(limit, list.size());
        std::partial_sort(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(keep), list.end(), lessThan);
        auto& out = results.byKind[k];
        out.reserve(keep);
        for (std::size_t n = 0; n < keep; ++n) {
            out.push_back({&entries_[list[n].second].item, list[n].first});
        }
        if (list.empty()) {
            continue;
        }
        // The best of this kind competes for the top result. With limit 0 nothing was sorted: find it.
        std::pair<int, std::size_t> best = list.front();
        if (keep == 0) {
            best = *std::min_element(list.begin(), list.end(), lessThan);
        }
        const bool person = k == static_cast<std::size_t>(SearchKind::Artist) || k == static_cast<std::size_t>(SearchKind::Composer);
        const int rated = best.first + (person && best.first >= 60 ? kPersonBonus : 0);
        if (!top || rated > top->first) {
            top = std::make_pair(rated, best.second);
        }
    }
    if (top) {
        results.top = Hit{&entries_[top->second].item, top->first};
    }
    return results;
}

std::vector<SearchIndex::Hit> SearchIndex::search(std::string_view query, SearchKind kind, std::size_t limit) const {
    std::vector<Hit> out;
    const std::string q = foldForSearch(query);
    if (q.empty() || limit == 0) {
        return out;
    }
    const std::vector<std::string> words = splitWords(q);
    std::vector<std::pair<int, std::size_t>> list;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        if (e.item.kind != kind) {
            continue;
        }
        if (const int score = scoreOf(e.titleKey, e.allKey, q, words); score > 0) {
            list.emplace_back(score, i);
        }
    }
    const std::size_t keep = std::min(limit, list.size());
    std::partial_sort(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(keep), list.end(), [&](const auto& a, const auto& b) {
        const Entry& ea = entries_[a.second];
        const Entry& eb = entries_[b.second];
        return better({&ea.item, a.first}, {&eb.item, b.first}, ea.titleKey, eb.titleKey);
    });
    for (std::size_t n = 0; n < keep; ++n) {
        out.push_back({&entries_[list[n].second].item, list[n].first});
    }
    return out;
}

}  // namespace mp::core::library
