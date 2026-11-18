#include "core/library/work_tree.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string_view>
#include <utility>

#include "core/library/work_identity.hpp"

namespace mp::core::library {

namespace {

std::string composerKeyOf(const WorkEntry& w) {
    if (w.composer.empty()) {
        return {};
    }
    const std::size_t sep = w.workKey.find('\x1F');
    return sep == std::string::npos ? foldForKey(w.composer) : w.workKey.substr(0, sep);
}

// "BWV 1046" < "BWV 1051" < "BWV 995a" by prefix, then number, then the rest.
struct CatalogSort {
    std::string prefix;
    long number = -1;
    std::string rest;
};
CatalogSort catalogSort(const std::string& catalog) {
    CatalogSort c;
    std::size_t i = 0;
    while (i < catalog.size() && std::isdigit(static_cast<unsigned char>(catalog[i])) == 0) {
        c.prefix += static_cast<char>(std::tolower(static_cast<unsigned char>(catalog[i])));
        ++i;
    }
    std::size_t digits = i;
    while (digits < catalog.size() && digits - i < 9 && std::isdigit(static_cast<unsigned char>(catalog[digits])) != 0) {
        ++digits;
    }
    if (digits > i) {
        c.number = std::stol(catalog.substr(i, digits - i));
    }
    c.rest = catalog.substr(digits);
    return c;
}

struct Sorted {
    WorkEntry work;
    int number = 0;  // "No. 5" -> 5; 0 sorts after numbered works
    CatalogSort catalog;
    std::string titleKey;
};

bool workLess(const Sorted& a, const Sorted& b) {
    if ((a.number > 0) != (b.number > 0)) {
        return a.number > 0;
    }
    if (a.number != b.number) {
        return a.number < b.number;
    }
    if (a.catalog.prefix != b.catalog.prefix) {
        return a.catalog.prefix < b.catalog.prefix;
    }
    if (a.catalog.number != b.catalog.number) {
        return a.catalog.number < b.catalog.number;
    }
    if (a.catalog.rest != b.catalog.rest) {
        return a.catalog.rest < b.catalog.rest;
    }
    return a.titleKey < b.titleKey;
}

}  // namespace

namespace {

std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isArrangement(const std::string& title) {
    const std::string t = lowerAscii(title);
    for (const char* w : {"arr.", "arr ", "arranged", "transcr", "bearb", "adapt"}) {
        if (t.find(w) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// "f:suite|lute|" -> "Lute Suite"; "f:viola da gamba..." keeps "da" lower case.
std::string labelFromKey(const std::string& key) {
    const std::size_t a = key.find('|');
    const std::size_t b = key.find('|', a + 1);
    if (key.rfind("f:", 0) != 0 || a == std::string::npos || b == std::string::npos) {
        return {};
    }
    const std::string form = key.substr(2, a - 2);
    const std::string instrument = key.substr(a + 1, b - a - 1);
    std::string words = instrument.empty() ? form : instrument + " " + form;
    bool start = true;
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const char c = words[i];
        if (start && words.compare(i, 3, "da ") != 0 && words.compare(i, 3, "de ") != 0) {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        } else {
            out += c;
        }
        start = c == ' ';
    }
    return out;
}

}  // namespace

WorkFamilyVote familyOfWork(const std::string& title, const std::vector<std::string>& titles,
                            const std::function<int(const std::string& familyKey)>& familySize) {
    struct Seen {
        std::string title;
        WorkFamily family;
    };
    std::vector<Seen> seen{{title, workFamily(title)}};
    for (const std::string& t : titles) {
        if (std::none_of(seen.begin(), seen.end(), [&](const Seen& s) { return s.title == t; })) {
            seen.push_back({t, workFamily(t)});
        }
    }

    std::map<std::string, int> votes;
    for (const Seen& s : seen) {
        if (!s.family.key.empty()) {
            ++votes[s.family.key];
        }
    }
    // A title without a form ("Bach autrement") only counts when no title names one.
    if (std::any_of(votes.begin(), votes.end(), [](const auto& v) { return v.first.rfind("f:", 0) == 0; })) {
        std::erase_if(votes, [](const auto& v) { return v.first.rfind("t:", 0) == 0; });
    }
    // A plain form ("f:suite||") counts for the most-voted specific family of that form ("f:suite|cello|").
    std::map<std::string, std::string> movedTo;  // plain key -> specific key
    for (auto& [key, count] : votes) {
        if (key.rfind("f:", 0) != 0 || key.size() < 4 || key.compare(key.size() - 2, 2, "||") != 0) {
            continue;
        }
        const std::string prefix = key.substr(0, key.size() - 1);  // "f:suite|"
        std::string best;
        for (const Seen& s : seen) {  // in order, so the first seen wins a tie
            const std::string& k = s.family.key;
            if (k != key && k.rfind(prefix, 0) == 0 && votes.contains(k) && (best.empty() || votes[k] > votes[best])) {
                best = k;
            }
        }
        if (!best.empty()) {
            movedTo[key] = best;
        }
    }
    for (const auto& [from, to] : movedTo) {
        votes[to] += votes[from];
        votes.erase(from);
    }

    WorkFamilyVote out;
    int bestVotes = 0;
    for (const auto& [key, count] : votes) {
        bestVotes = std::max(bestVotes, count);
    }
    std::vector<std::string> tied;  // in seen order: the work's own title first
    for (const Seen& s : seen) {
        const auto it = votes.find(s.family.key);
        if (it != votes.end() && it->second == bestVotes &&
            std::find(tied.begin(), tied.end(), s.family.key) == tied.end()) {
            tied.push_back(s.family.key);
        }
    }
    if (tied.empty()) {
        return out;
    }
    out.tied = tied.size() > 1;
    out.key = tied.front();
    if (out.tied && familySize) {
        int bestSize = familySize(out.key);
        for (std::size_t i = 1; i < tied.size(); ++i) {
            if (const int size = familySize(tied[i]); size > bestSize) {
                bestSize = size;
                out.key = tied[i];
            }
        }
    }

    // Title: the most common one of the winner (case-insensitive), not an arrangement, shorter.
    std::map<std::string, int> titleVotes;
    for (const Seen& s : seen) {
        if (s.family.key == out.key) {
            ++titleVotes[lowerAscii(s.title)];
        }
    }
    const Seen* bestTitle = nullptr;
    for (const Seen& s : seen) {
        if (s.family.key != out.key) {
            continue;
        }
        if (bestTitle == nullptr) {
            bestTitle = &s;
            continue;
        }
        const int a = titleVotes[lowerAscii(s.title)];
        const int b = titleVotes[lowerAscii(bestTitle->title)];
        const bool arrA = isArrangement(s.title);
        const bool arrB = isArrangement(bestTitle->title);
        if (a > b || (a == b && (arrA != arrB ? !arrA : s.title.size() < bestTitle->title.size()))) {
            bestTitle = &s;
        }
    }
    out.title = bestTitle != nullptr ? bestTitle->title : title;
    // Label: the chosen title's wording when it names the family fully; else the most common complete wording
    // of the winner; else built from the key.
    if (bestTitle != nullptr && bestTitle->family.complete) {
        out.label = bestTitle->family.label;
    }
    std::map<std::string, int> labels;
    for (const Seen& s : seen) {
        if (s.family.key == out.key && s.family.complete && !s.family.label.empty()) {
            ++labels[s.family.label];
        }
    }
    int bestLabel = 0;
    const bool fromTitle = !out.label.empty();
    for (const Seen& s : seen) {
        if (fromTitle) {
            break;
        }
        if (s.family.key == out.key && s.family.complete && labels[s.family.label] > bestLabel) {
            bestLabel = labels[s.family.label];
            out.label = s.family.label;
        }
    }
    if (out.label.empty()) {
        out.label = labelFromKey(out.key);
    }
    if (out.label.empty()) {
        for (const Seen& s : seen) {
            if (s.family.key == out.key) {
                out.label = s.family.label;
                break;
            }
        }
    }

    return out;
}

std::string catalogRange(const std::vector<std::string>& catalogs) {
    // prefix -> numbers (plain) and others (with a suffix), each sorted
    std::vector<std::pair<std::string, std::pair<std::vector<long>, std::vector<CatalogSort>>>> byPrefix;
    std::vector<std::string> odd;
    for (const std::string& c : catalogs) {
        if (c.empty()) {
            continue;
        }
        // The last number is the one that runs: "Op. 8 No. 1".."Op. 8 No. 4" -> "Op. 8 No. 1–4".
        CatalogSort cs;
        {
            std::size_t end = c.size();
            while (end > 0 && std::isalpha(static_cast<unsigned char>(c[end - 1])) != 0) {
                --end;
            }
            std::size_t start = end;
            while (start > 0 && std::isdigit(static_cast<unsigned char>(c[start - 1])) != 0 && end - start < 9) {
                --start;
            }
            if (start < end) {
                cs.prefix = c.substr(0, start);
                std::transform(cs.prefix.begin(), cs.prefix.end(), cs.prefix.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                cs.number = std::stol(c.substr(start, end - start));
                cs.rest = c.substr(end);
            }
        }
        if (cs.number < 0) {
            if (std::find(odd.begin(), odd.end(), c) == odd.end()) {
                odd.push_back(c);
            }
            continue;
        }
        // Keep the prefix as written ("BWV "), compare it folded.
        const std::string written = c.substr(0, c.size() - std::to_string(cs.number).size() - cs.rest.size());
        auto it = std::find_if(byPrefix.begin(), byPrefix.end(), [&](const auto& e) {
            std::string folded = e.first;
            std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return folded == cs.prefix;
        });
        if (it == byPrefix.end()) {
            byPrefix.push_back({written, {}});
            it = byPrefix.end() - 1;
        }
        if (cs.rest.empty()) {
            it->second.first.push_back(cs.number);
        } else {
            it->second.second.push_back(cs);
        }
    }
    std::string out;
    for (auto& [prefix, lists] : byPrefix) {
        auto& [numbers, suffixed] = lists;
        std::sort(numbers.begin(), numbers.end());
        numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
        std::sort(suffixed.begin(), suffixed.end(), [](const CatalogSort& a, const CatalogSort& b) {
            return a.number != b.number ? a.number < b.number : a.rest < b.rest;
        });
        // Items in number order: runs of plain numbers, single suffixed ones.
        std::vector<std::pair<long, std::string>> items;  // sort number, text
        for (std::size_t i = 0; i < numbers.size();) {
            std::size_t j = i;
            while (j + 1 < numbers.size() && numbers[j + 1] == numbers[j] + 1) {
                ++j;
            }
            std::string text = std::to_string(numbers[i]);
            if (j > i) {
                text += (j == i + 1 ? ", " : "\xE2\x80\x93") + std::to_string(numbers[j]);  // two in a row: "1, 2"
            }
            items.emplace_back(numbers[i], text);
            i = j + 1;
        }
        for (const CatalogSort& c : suffixed) {
            const std::string text = std::to_string(c.number) + c.rest;
            if (items.empty() || items.back().second != text) {
                items.emplace_back(c.number, text);
            }
        }
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::string part = prefix;
        for (std::size_t i = 0; i < items.size(); ++i) {
            part += (i ? ", " : "") + items[i].second;
        }
        out += (out.empty() ? "" : "; ") + part;
    }
    for (const std::string& c : odd) {
        out += (out.empty() ? "" : "; ") + c;
    }
    return out;
}

std::vector<ComposerNode> groupWorksByComposer(std::vector<WorkEntry> works) {
    struct Family {
        std::vector<Sorted> works;
        std::map<std::string, int> labels;  // label -> count, the most common one is shown
    };
    struct Composer {
        std::string name;
        std::map<std::string, Family> families;  // by family key; works without one get "#<id>"
    };
    std::map<std::string, Composer> composers;  // by composer key; "" = no composer

    for (WorkEntry& w : works) {
        Composer& c = composers[composerKeyOf(w)];
        if (w.composer.size() > c.name.size()) {
            c.name = w.composer;
        }
        WorkFamily fam;
        if (w.familyKnown) {
            fam.key = w.familyKey;
            fam.label = w.familyLabel;
        } else {
            const WorkFamilyVote v = familyOfWork(w.title, w.titles);
            fam.key = v.key;
            fam.label = v.label;
        }
        Family& f = composers[composerKeyOf(w)].families[fam.key.empty() ? "#" + std::to_string(w.id) : fam.key];
        if (!fam.label.empty()) {
            ++f.labels[fam.label];
        }
        Sorted s;
        if (const std::size_t hash = w.numberKey.rfind('#'); hash != std::string::npos) {
            s.number = std::atoi(w.numberKey.c_str() + hash + 1);
        } else if (!w.familyKnown) {
            s.number = identifyWork(w.title).number;
        }
        s.catalog = catalogSort(w.catalog);
        s.titleKey = foldForKey(w.title);
        s.work = std::move(w);
        f.works.push_back(std::move(s));
    }

    std::vector<ComposerNode> out;
    for (auto& [key, c] : composers) {
        ComposerNode node;
        node.name = c.name;
        std::vector<std::pair<std::string, WorkFamilyNode>> lines;  // sort key, node
        for (auto& [famKey, f] : c.families) {
            std::sort(f.works.begin(), f.works.end(), workLess);
            node.workCount += static_cast<int>(f.works.size());
            if (f.works.size() == 1) {
                WorkFamilyNode single;
                single.works.push_back(std::move(f.works.front().work));
                lines.emplace_back(foldForKey(single.works.front().title), std::move(single));
                continue;
            }
            WorkFamilyNode fam;
            int best = 0;
            for (const auto& [label, count] : f.labels) {
                // Most common; on a tie the shorter, then alphabetical (map order).
                if (count > best || (count == best && label.size() < fam.label.size())) {
                    best = count;
                    fam.label = label;
                }
            }
            std::vector<std::string> catalogs;
            for (Sorted& s : f.works) {
                catalogs.push_back(s.work.catalog);
                fam.works.push_back(std::move(s.work));
            }
            fam.catalogRange = catalogRange(catalogs);
            lines.emplace_back(foldForKey(fam.label), std::move(fam));
        }
        std::stable_sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& line : lines) {
            node.families.push_back(std::move(line.second));
        }
        out.push_back(std::move(node));
    }
    // std::map put "" (no composer) first: move it to the end.
    if (!out.empty() && out.front().name.empty()) {
        std::rotate(out.begin(), out.begin() + 1, out.end());
    }
    return out;
}

}  // namespace mp::core::library
