#include "core/library/work_identity.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>

#include "core/library/work_parser.hpp"
#include "core/text.hpp"

namespace mp::core::library {

namespace {

// Latin-1 (U+00C0..U+00FF) and Latin Extended-A (U+0100..U+017F) letters folded to ASCII.
constexpr std::string_view kLatin1Lower = "aaaaaaaceeeeiiiidnooooo-ouuuuytsaaaaaaaceeeeiiiidnooooo-ouuuuyty";
constexpr std::string_view kLatinExtA =
    "aaaaaaccccccccddddeeeeeeeeeegggggggghhhhiiiiiiiiiiiijjkkkllllllllllnnnnnnnnnoooooooorrrrrrssssssssttttttuuuuuuuuuuuuwwyyyzzzzzzs";
static_assert(kLatin1Lower.size() == 64);
static_assert(kLatinExtA.size() == 128);

std::string foldAccents(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(std::tolower(c));
        } else if (c >= 0xC2 && c <= 0xC5 && i + 1 < s.size()) {
            const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            ++i;
            if (cp == 0xB0 || cp == 0xBA) {  // ° º ("n° 5", "n.º 5")
                out += 'o';
            } else if (cp == 0xAA) {
                out += 'a';
            } else if (cp == 0xDF) {
                out += "ss";
            } else if (cp == 0xC6 || cp == 0xE6) {
                out += "ae";
            } else if (cp == 0x152 || cp == 0x153) {
                out += "oe";
            } else if (cp >= 0xC0 && cp <= 0xFF) {
                const char f = kLatin1Lower[cp - 0xC0];
                out += f == '-' ? ' ' : f;
            } else if (cp >= 0x100 && cp <= 0x17F) {
                out += kLatinExtA[cp - 0x100];
            } else {
                out += static_cast<char>(c);
                out += s[i];
            }
        } else if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x99) {
            const unsigned char third = static_cast<unsigned char>(s[i + 2]);
            if (third == 0xAF) {  // ♯
                out += '#';
                i += 2;
            } else if (third == 0xAD) {  // ♭
                out += 'b';
                i += 2;
            } else {
                out += static_cast<char>(c);
            }
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    for (std::size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) {
        s.replace(pos, from.size(), to);
    }
    return s;
}

using Table = std::vector<std::pair<std::string, std::string>>;

// Canonical name -> spellings (folded, lower case). Longer spellings are tried first.
const Table& formTable() {
    static const Table t = {
        {"symphony", "symphony|symphonie|sinfonie|sinfonia|symfonie|symfoni|simfonia|symphonies"},
        {"concerto", "concerto|konzert|concert|concierto|koncert|konsert|konzertstuck"},
        {"sonata", "sonata|sonate|sonat"},
        {"quartet", "quartet|quartett|quatuor|quartetto|cuarteto"},
        {"quintet", "quintet|quintett|quintette|quintetto|quinteto"},
        {"sextet", "sextet|sextett|sextuor|sestetto|sexteto"},
        {"septet", "septet|septett|septuor|settimino"},
        {"octet", "octet|oktett|octuor|ottetto|octeto"},
        {"trio", "trio"},
        {"suite", "suite"},
        {"overture", "overture|ouverture|ouvertura|uberture"},
        {"partita", "partita"},
        {"serenade", "serenade|serenata|serenta"},
        {"mass", "mass|messe|missa|messa"},
        {"requiem", "requiem"},
        {"fantasia", "fantasia|fantasy|fantasie|fantaisie"},
        {"etude", "etude|study|studie"},
        {"prelude", "prelude|praludium"},
        {"nocturne", "nocturne|notturno"},
        {"mazurka", "mazurka"},
        {"polonaise", "polonaise|polacca"},
        {"waltz", "waltz|walzer|valse|valzer"},
        {"rhapsody", "rhapsody|rhapsodie|rapsodia"},
        {"ballade", "ballade|ballad|ballata"},
        {"scherzo", "scherzo"},
        {"impromptu", "impromptu"},
        {"cantata", "cantata|kantate|cantate"},
        {"oratorio", "oratorio"},
        {"sinfonietta", "sinfonietta"},
    };
    return t;
}

const Table& instrumentTable() {
    static const Table t = {
        {"piano", "piano|klavier|clavier|pianoforte|piano-forte"},
        {"violin", "violin|violine|violon|violino"},
        {"viola", "viola|bratsche|alto"},
        {"cello", "cello|violoncello|violoncelle|violonchelo"},
        {"double bass", "double bass|kontrabass|contrabass|contrebasse|contrabbasso"},
        {"flute", "flute|flote|flauto"},
        {"oboe", "oboe|hautbois"},
        {"clarinet", "clarinet|klarinette|clarinette|clarinetto|clarinete"},
        {"bassoon", "bassoon|fagott|basson|fagotto"},
        {"horn", "horn|waldhorn|corno"},
        {"trumpet", "trumpet|trompete|trompette|tromba"},
        {"trombone", "trombone|posaune"},
        {"organ", "organ|orgel|orgue|organo|organ"},
        {"harp", "harp|harfe|harpe|arpa"},
        {"guitar", "guitar|gitarre|guitare|chitarra|guitarra"},
        {"lute", "lute|laute|lauten|luth|liuto|laud"},
        {"theorbo", "theorbo|theorbe|tiorba|teorba|tiorbe"},
        {"recorder", "recorder|blockflote|flute a bec|flauto dolce"},
        {"viola da gamba", "viola da gamba|gamba|gambe|viole de gambe"},
        {"mandolin", "mandolin|mandoline|mandolino"},
        {"harpsichord", "harpsichord|cembalo|clavecin|clavicembalo"},
        {"string", "string|strings|streich|cordes|archi|cuerdas"},
        {"wind", "wind|winds|blaser|vents|fiati"},
    };
    return t;
}

// "a|b|c" -> "(?:c|b|a)" with the longest first, so "violoncello" is tried before "violon".
std::string alternation(const Table& table) {
    std::vector<std::string> words;
    for (const auto& [canonical, spellings] : table) {
        std::size_t start = 0;
        while (start <= spellings.size()) {
            std::size_t bar = spellings.find('|', start);
            if (bar == std::string::npos) {
                bar = spellings.size();
            }
            words.push_back(spellings.substr(start, bar - start));
            start = bar + 1;
        }
    }
    std::sort(words.begin(), words.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    std::string out = "(?:";
    for (std::size_t i = 0; i < words.size(); ++i) {
        out += (i ? "|" : "") + words[i];
    }
    return out + ")";
}

std::string canonicalOf(const Table& table, const std::string& word) {
    for (const auto& [canonical, spellings] : table) {
        const std::string wrapped = "|" + spellings + "|";
        if (wrapped.find("|" + word + "|") != std::string::npos) {
            return canonical;
        }
    }
    return {};
}

struct Patterns {
    std::regex form;
    std::regex instrumentBefore;  // an instrument word right before the form
    std::regex instrumentFor;     // "for violin", "pour piano", "fur klavier", "for solo violin", anywhere after the form
    std::regex arrangement;       // "arr. for guitar", "transcr. by…": what follows is not the work's instrument
    std::regex glued;             // German compounds: klavierkonzert
    std::regex noAfter;           // number after the form
    std::regex bareAfter;         // "Symphony 5"
    std::regex numberBefore;      // "5th symphony", "5. Sinfonie"
    std::regex opus;              // stripped before number parsing so "Op. 18 No. 1" gives no number
    std::regex keyLetter;         // "e minor", "b-flat major", "fis-moll"
    std::regex keySolfege;        // "mi menor", "si bemol majeur"
};

const Patterns& patterns() {
    using std::regex_constants::ECMAScript;
    using std::regex_constants::icase;
    static const Patterns p = [] {
        const std::string forms = alternation(formTable());
        const std::string instruments = alternation(instrumentTable());
        Patterns r;
        r.form = std::regex("\\b(" + forms + ")\\b", ECMAScript | icase);
        r.instrumentBefore = std::regex("\\b(" + instruments + ")\\s*$", ECMAScript | icase);
        r.instrumentFor = std::regex("\\b(?:for|pour|fur|per|para)\\s+(?:the\\s+|a\\s+)?(?:solo\\s+)?(?:(?:\\d|two|three|four|deux|trois|quatre|zwei|drei|vier|due|tre|quattro|dos|tres|cuatro)\\s+)?(" + instruments + ")\\b", ECMAScript | icase);
        r.arrangement = std::regex("\\b(?:arr|arranged|arrangement|transcr|transcribed|transcription|bearb|bearbeitung|version)\\b", ECMAScript | icase);
        r.glued = std::regex("\\b(klavier|violin|violoncello|cello|streich|bratschen|floten|orgel|gitarren|lauten|horn|oboen|klarinetten|fagott)(konzert|sonate|quartett|trio|quintett|sextett|suite)",
                             ECMAScript | icase);
        r.noAfter = std::regex("(?:\\bno|\\bnr|\\bn\\.?o|\\bnum(?:ber|ero)?|\\bn\\.|#)\\s*\\.?\\s*(\\d{1,3})\\b", ECMAScript | icase);
        r.bareAfter = std::regex("^(?:\\s+(?:for|pour|fur|per|para|de)\\s+" + instruments + ")?\\s*(?:and\\s+orchestra\\s*)?(\\d{1,3})\\b", ECMAScript | icase);
        r.numberBefore = std::regex("(\\d{1,3})\\s*(?:st|nd|rd|th|e|er|eme|\\.|\\.o|o)?\\s*$", ECMAScript | icase);
        r.opus = std::regex("\\bop(?:us)?\\s*\\.?\\s*(?:posth\\.?\\s*)?\\d{1,3}[a-z]?(?:\\s*,?\\s*(?:no|nr)\\s*\\.?\\s*\\d{1,2}|/\\d{1,2})?", ECMAScript | icase);
        r.keyLetter = std::regex("\\b([a-h])(?:[\\s-]*(sharp|flat|is|es|s|#|b))?[\\s-]*(major|minor|dur|moll|majeur|mineur)\\b", ECMAScript | icase);
        r.keySolfege = std::regex("\\b(do|re|mi|fa|sol|la|si)\\s*(bemol|sostenido|diese|dieses|bemolle|diesis)?\\s*(mayor|menor|majeur|mineur|maggiore|minore)\\b", ECMAScript | icase);
        return r;
    }();
    return p;
}

std::string keyName(char letter, const std::string& accidental, const std::string& mode) {
    const bool german = mode == "dur" || mode == "moll";
    std::string note(1, static_cast<char>(std::toupper(static_cast<unsigned char>(letter))));
    std::string acc;
    if (letter == 'h') {
        note = "B";  // German H is B natural
    } else if (!accidental.empty()) {
        acc = (accidental == "sharp" || accidental == "is" || accidental == "#") ? "sharp" : "flat";
    } else if (german && letter == 'b') {
        acc = "flat";  // German B is B-flat
    }
    const bool minor = mode == "minor" || mode == "moll" || mode == "mineur" || mode == "menor" || mode == "minore";
    return note + (acc.empty() ? "" : "-" + acc) + (minor ? " minor" : " major");
}

// "Partita II", "Suite III (d'après…)": an upper-case roman number right after a form word, in the original text.
int romanAfterForm(std::string_view text) {
    static const std::regex kRomanAfter("\\b(?:" + alternation(formTable()) + ")\\s+([ivx]{1,4})(?=[\\s,.;:()'\"]|$)",
                                        std::regex_constants::ECMAScript | std::regex_constants::icase);
    const std::string s(text);
    std::smatch m;
    if (!std::regex_search(s, m, kRomanAfter)) {
        return 0;
    }
    const std::optional<int> n = parseRoman(m[1].str());  // upper case only: "vi" or "xi" as words stay words
    return n && *n <= 20 ? *n : 0;
}

}  // namespace

// Families (below) and identity share the reading of a title's name.
struct FamilyParts {
    std::string label;       // the name part of the title
    std::string form;        // first form word, canonical
    std::string instrument;  // first instrument word in the label, canonical
    std::string rest;        // the other words, folded and stemmed
};
FamilyParts familyParts(std::string_view workTitle);
int ordinalValue(const std::string& foldedWord);  // "deuxieme" -> 2, 0 if not an ordinal word

std::string foldForKey(std::string_view utf8) {
    std::string s = foldAccents(utf8);
    // Transliteration variants of the same sound (Tchaikovsky / Tschaikowski / Czajkowski -> chaikovski).
    s = replaceAll(std::move(s), "tsch", "ch");
    s = replaceAll(std::move(s), "tch", "ch");
    s = replaceAll(std::move(s), "cz", "ch");
    std::string out;
    for (const char c : s) {
        if (c == 'w') {
            out += 'v';
        } else if (c == 'j' || c == 'y') {
            out += 'i';
        } else if (std::isalnum(static_cast<unsigned char>(c)) != 0 || (static_cast<unsigned char>(c) >= 0x80)) {
            out += c;
        } else if (c == ' ' && !out.empty() && out.back() != ' ') {
            out += ' ';
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return replaceAll(std::move(out), "ii", "i");
}

WorkIdentity identifyWork(std::string_view text) {
    WorkIdentity id;
    if (const auto catalog = findCatalogNumber(text)) {
        id.catalog = *catalog;
    }
    const Patterns& p = patterns();

    std::string folded = foldAccents(text);
    folded = std::regex_replace(folded, p.glued, "$1 $2");

    // Key first: "in e minor" must not be mistaken for anything else, and it does not depend on the form.
    {
        std::smatch m;
        if (std::regex_search(folded, m, p.keyLetter)) {
            const std::string mode = m[3].str();
            std::string lowerMode = mode;
            std::transform(lowerMode.begin(), lowerMode.end(), lowerMode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string lowerAcc = m[2].matched ? m[2].str() : std::string();
            std::transform(lowerAcc.begin(), lowerAcc.end(), lowerAcc.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            id.musicalKey = keyName(static_cast<char>(std::tolower(static_cast<unsigned char>(m[1].str()[0]))), lowerAcc, lowerMode);
        } else if (std::regex_search(folded, m, p.keySolfege)) {
            static const std::map<std::string, char> kSolfege = {{"do", 'c'}, {"re", 'd'}, {"mi", 'e'}, {"fa", 'f'}, {"sol", 'g'}, {"la", 'a'}, {"si", 'b'}};
            std::string name = m[1].str();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string acc = m[2].matched ? m[2].str() : std::string();
            std::string mode = m[3].str();
            std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool sharp = acc.find("sost") == 0 || acc.find("dies") == 0;
            const bool minor = mode == "menor" || mode == "mineur" || mode == "minore";
            std::string note(1, static_cast<char>(std::toupper(static_cast<unsigned char>(kSolfege.at(name)))));
            id.musicalKey = note + (acc.empty() ? "" : (sharp ? "-sharp" : "-flat")) + (minor ? " minor" : " major");
        }
    }

    std::smatch form;
    if (!std::regex_search(folded, form, p.form)) {
        return id;
    }
    std::string formWord = form[1].str();
    std::transform(formWord.begin(), formWord.end(), formWord.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    id.form = canonicalOf(formTable(), formWord);

    const std::string before = folded.substr(0, static_cast<std::size_t>(form.position(0)));
    std::string after = folded.substr(static_cast<std::size_t>(form.position(0) + form.length(0)));

    // Solo instrument: "Piano Concerto", "Concerto for Piano and Orchestra", "Partita in D minor for solo violin".
    // Only up to "Arr. for Guitar": an arrangement's instrument is not the work's.
    std::smatch inst;
    std::size_t instrumentStart = before.size();
    std::string original = after;
    if (std::smatch arr; std::regex_search(after, arr, p.arrangement)) {
        original = after.substr(0, static_cast<std::size_t>(arr.position(0)));
    }
    if (std::regex_search(before, inst, p.instrumentBefore)) {
        std::string w = inst[1].str();
        std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        id.instrument = canonicalOf(instrumentTable(), w);
        instrumentStart = static_cast<std::size_t>(inst.position(1));
    } else if (std::regex_search(original, inst, p.instrumentFor)) {
        std::string w = inst[1].str();
        std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        id.instrument = canonicalOf(instrumentTable(), w);
    }

    // Numbers: never take the "No." of an opus number ("Op. 18 No. 1").
    after = std::regex_replace(after, p.opus, " ");
    std::smatch num;
    if (std::regex_search(after, num, p.noAfter)) {
        id.number = std::atoi(num[1].str().c_str());
    } else if (std::regex_search(after, num, p.bareAfter)) {
        id.number = std::atoi(num[1].str().c_str());
    } else if (const int roman = romanAfterForm(text); roman > 0) {
        id.number = roman;  // "Partita II"
    } else {
        const std::string lead = before.substr(0, instrumentStart);
        if (std::regex_search(lead, num, p.numberBefore)) {
            id.number = std::atoi(num[1].str().c_str());
        } else {
            // An ordinal word right before the form: "Deuxième concert", "Fünfte Sinfonie", "Second Suite".
            const std::size_t end = lead.find_last_not_of(" \t");
            if (end != std::string::npos) {
                const std::size_t space = lead.find_last_of(" \t", end);
                const std::size_t start = space == std::string::npos ? 0 : space + 1;
                id.number = ordinalValue(lead.substr(start, end + 1 - start));
            }
        }
    }
    id.qualifier = familyParts(text).rest;
    return id;
}

namespace {

// "piano concerto", "concerto [brand]", "violin sonata"
std::string formPrefix(const WorkIdentity& id) {
    std::string prefix = id.instrument.empty() ? id.form : id.instrument + " " + id.form;
    if (!id.qualifier.empty()) {
        prefix += " [" + id.qualifier + "]";
    }
    return prefix;
}

}  // namespace

std::string numberKey(const WorkIdentity& id) {
    if (id.form.empty() || id.number <= 0) {
        return {};
    }
    return "f:" + formPrefix(id) + "#" + std::to_string(id.number);
}

std::string identityKey(const WorkIdentity& id) {
    if (!id.catalog.empty()) {
        return "c:" + id.catalog;
    }
    if (std::string k = numberKey(id); !k.empty()) {
        return k;
    }
    if (!id.form.empty() && !id.musicalKey.empty()) {
        return "f:" + formPrefix(id) + "/" + id.musicalKey;
    }
    return {};
}

// ---- families ----------------------------------------------------------------------------------

namespace {

std::string trimmed(std::string s) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// Plural and inflected spellings ("Concertos", "Sinfonien", "Konzerte", "Suites") of a table word.
std::string canonicalLoose(const Table& table, const std::string& word) {
    if (std::string c = canonicalOf(table, word); !c.empty()) {
        return c;
    }
    for (const std::string_view suffix : {"es", "s", "en", "n", "e", "i"}) {
        if (word.size() > suffix.size() + 2 && word.compare(word.size() - suffix.size(), suffix.size(), suffix) == 0) {
            if (std::string c = canonicalOf(table, word.substr(0, word.size() - suffix.size())); !c.empty()) {
                return c;
            }
        }
    }
    return {};
}

bool isFamilyStopword(const std::string& w) {
    static const std::set<std::string> kWords = {
        "for", "pour", "fur", "per", "para", "de", "di", "du", "des", "del", "the", "a", "and", "und", "et", "e", "y", "ed",
        "in", "en", "no", "nr", "n", "op", "opus", "of", "la", "le", "les", "il", "der", "die", "das", "with", "mit", "avec", "con",
        "orchestra", "orchester", "orchestre", "orquesta", "solo", "seul", "sola", "allein", "d", "l",
        "continuo", "basso", "bc",  // accompaniment, like "and orchestra"
        // "Concerto for two violins" is in the violin concerto family
        "two", "three", "four", "deux", "trois", "quatre", "zwei", "drei", "vier", "due", "tre", "quattro", "dos", "tres", "cuatro"};
    return kWords.contains(w);
}

}  // namespace

int ordinalValue(const std::string& foldedWord) {
    static const std::map<std::string, int> kOrdinals = [] {
        const std::vector<std::pair<int, std::vector<std::string>>> rows = {
            {1, {"first", "premier", "premiere", "erste", "erster", "erstes", "primo", "prima", "primero", "primera", "primer"}},
            {2, {"second", "seconde", "deuxieme", "zweite", "zweiter", "zweites", "secondo", "seconda", "segundo", "segunda"}},
            {3, {"third", "troisieme", "dritte", "dritter", "drittes", "terzo", "terza", "tercero", "tercera", "tercer"}},
            {4, {"fourth", "quatrieme", "vierte", "vierter", "viertes", "quarto", "quarta", "cuarto", "cuarta"}},
            {5, {"fifth", "cinquieme", "funfte", "funfter", "fuenfte", "quinto", "quinta"}},
            {6, {"sixth", "sixieme", "sechste", "sechster", "sesto", "sesta", "sexto", "sexta"}},
            {7, {"seventh", "septieme", "siebte", "siebter", "settimo", "settima", "septimo", "septima"}},
            {8, {"eighth", "huitieme", "achte", "achter", "ottavo", "ottava", "octavo", "octava"}},
            {9, {"ninth", "neuvieme", "neunte", "neunter", "nono", "nona", "noveno", "novena"}},
            {10, {"tenth", "dixieme", "zehnte", "zehnter", "decimo", "decima"}},
        };
        std::map<std::string, int> m;
        for (const auto& [n, words] : rows) {
            for (const std::string& w : words) {
                m.emplace(w, n);
            }
        }
        return m;
    }();
    const auto it = kOrdinals.find(foldedWord);
    return it == kOrdinals.end() ? 0 : it->second;
}

FamilyParts familyParts(std::string_view workTitle) {
    using std::regex_constants::ECMAScript;
    using std::regex_constants::icase;
    // A leading ordinal: "5. Sinfonie", "5th Symphony", "No. 5 Symphony" (ordinal words: below).
    static const std::regex kLeading("^\\s*(?:(?:no|nr)\\.?\\s*)?\\d{1,3}\\s*(?:st|nd|rd|th|er|e|\\.)?\\s+", ECMAScript | icase);
    // Where the family name ends: a digit, a colon, semicolon, bracket or slash, a spaced dash, a key ("in F
    // major", "en ré", "e-Moll"), "n°"/"nº" (UTF-8), or a catalog prefix followed by a roman numeral (Hob. XVI).
    // A comma does not end it: "Prelude, Fugue and Allegro" is one name.
    static const std::regex kCut(
        "\\d|[:;(\\[/]|\\s-\\s|\\s(?:\\xE2\\x80\\x93|\\xE2\\x80\\x94)\\s|\\bn\\.?\\xC2[\\xB0\\xBA]|"
        "\\s(?:in|en)\\s+(?:[a-h](?:b|#|\\xE2\\x99[\\xAD\\xAF])?(?:\\s|-|$|,)|(?:do|re|r\\xC3[\\xA9\\x89]|mi|fa|sol|la|si|ut)(?=[\\s,.;:)]|$))|"
        "\\s[a-h](?:is|es|s)?-(?:dur|moll)\\b|"
        "\\b(?:hob|op|opus)\\b\\.?\\s*[ivxl]",
        ECMAScript | icase);
    // A roman number: "Partita II", "Bach autrement III". Upper case only, so words like "vi" stay.
    static const std::regex kRoman("\\s[IVX]{1,4}(?=[\\s,.;:()'\"]|$)", ECMAScript);
    // Left at the end after the cut: "No.", "Nr", "#", "Op.", "BWV", "K.", "for", single letters (catalog "D.")...
    static const std::regex kTrailing(
        "(?:\\s+|^)(?:no|nr|n|num|number|numero|op|opus|bwv|kv|hwv|rv|wq|woo|twv|buxwv|sz|fp|hob|for|pour|fur|f\\xC3\\xBCr|per|para|"
        "de|in|en|and|und|et|&|#|[a-z])\\.?\\s*$|[\\s.,#&\\-]+$",
        ECMAScript | icase);

    // A nickname in quotes does not name the family: Concerto “La Notte” for Bassoon is a bassoon concerto.
    static const std::regex kQuoted("\\s*(?:\xE2\x80\x9C.*?\xE2\x80[\x9D\x9C]|\xE2\x80\x9E.*?\xE2\x80\x9C|"
                                    "\xC2\xAB.*?\xC2\xBB|\"[^\"]*\")",
                                    ECMAScript);
    std::string text = std::regex_replace(std::string(workTitle), kQuoted, "");
    if (trimmed(text).empty()) {
        text = std::string(workTitle);
    }
    text = std::regex_replace(text, kLeading, "", std::regex_constants::format_first_only);
    // A leading ordinal word: "Deuxième concert Brandebourgeois", "Fünfte Sinfonie".
    {
        const std::size_t start = text.find_first_not_of(" \t");
        const std::size_t end = text.find_first_of(" \t", start == std::string::npos ? 0 : start);
        if (start != std::string::npos && end != std::string::npos &&
            ordinalValue(foldAccents(text.substr(start, end - start))) > 0) {
            text.erase(0, end);
        }
    }
    std::size_t cut = text.size();
    std::smatch m;
    if (std::regex_search(text, m, kCut)) {
        cut = static_cast<std::size_t>(m.position(0));
    }
    if (std::regex_search(text, m, kRoman)) {
        cut = std::min(cut, static_cast<std::size_t>(m.position(0)));
    }
    text.resize(cut);
    for (std::string before; before != text;) {
        before = text;
        text = std::regex_replace(text, kTrailing, "");
    }
    FamilyParts f;
    f.label = trimmed(text);
    if (f.label.empty()) {
        return f;
    }

    // Form and solo instrument by meaning; the other words folded and stemmed, in order.
    const std::string folded = std::regex_replace(foldAccents(f.label), patterns().glued, "$1 $2");
    std::string word;
    const auto take = [&] {
        if (word.empty()) {
            return;
        }
        if (std::string c = canonicalLoose(formTable(), word); !c.empty()) {
            if (f.form.empty()) {
                f.form = c;
            }
        } else if (std::string i = canonicalLoose(instrumentTable(), word); !i.empty()) {
            if (f.instrument.empty()) {
                f.instrument = i;  // a second one is ignored
            }
        } else if (!isFamilyStopword(word) && ordinalValue(word) == 0) {
            const std::string k = foldForKey(word);
            f.rest += (f.rest.empty() ? "" : " ") + k.substr(0, 5);  // "brandenburg" == "brandebourgeois"
        }
        word.clear();
    };
    for (std::size_t i = 0; i < folded.size(); ++i) {
        const auto c = static_cast<unsigned char>(folded[i]);
        // Quotes, dashes and other punctuation of U+2000–U+206F (E2 80/81 xx) and « » (C2 AB/BB) end a word.
        if (c == 0xE2 && i + 2 < folded.size() && (static_cast<unsigned char>(folded[i + 1]) & 0xFE) == 0x80) {
            take();
            i += 2;
        } else if (c == 0xC2 && i + 1 < folded.size() &&
                   (static_cast<unsigned char>(folded[i + 1]) == 0xAB || static_cast<unsigned char>(folded[i + 1]) == 0xBB)) {
            take();
            i += 1;
        } else if (std::isalnum(c) != 0 || c >= 0x80) {
            word += static_cast<char>(c);
        } else {
            take();
        }
    }
    take();
    return f;
}

WorkFamily workFamily(std::string_view workTitle) {
    const FamilyParts parts = familyParts(workTitle);
    WorkFamily f;
    f.label = parts.label;
    if (f.label.empty()) {
        return f;
    }
    if (parts.form.empty()) {
        f.key = "t:" + foldForKey(f.label);
        return f;
    }
    // The instrument may stand after the cut ("Partita in D minor for solo violin"): the whole title knows it.
    std::string instrument = parts.instrument;
    if (const std::string whole = identifyWork(workTitle).instrument; !whole.empty()) {
        instrument = whole;
    }
    f.complete = instrument == parts.instrument;
    f.key = "f:" + parts.form + "|" + instrument + "|" + parts.rest;
    return f;
}

// ---- composers ---------------------------------------------------------------------------------

namespace {

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == ' ' || c == '.' || c == ',' || c == '-') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

std::string initialsOf(const std::vector<std::string>& given) {
    std::string out;
    for (const std::string& w : given) {
        out += w.front();
    }
    return out;
}

}  // namespace

ComposerName parseComposerName(std::string_view name) {
    std::string first(name);
    if (const std::size_t semi = first.find(';'); semi != std::string::npos) {
        first = first.substr(0, semi);
    }
    // "J.S." or "P. I." without spaces: split initials so each letter counts.
    first = trimText(first);
    ComposerName out;
    const std::size_t comma = first.find(',');
    std::string family;
    std::string given;
    if (comma != std::string::npos) {
        family = first.substr(0, comma);
        given = first.substr(comma + 1);
    } else {
        // "Pyotr Ilyich Tchaikovsky": the last word is the surname, the rest are given names.
        const std::size_t space = first.find_last_of(' ');
        if (space == std::string::npos) {
            family = first;
        } else {
            family = first.substr(space + 1);
            given = first.substr(0, space);
        }
    }
    // Dots separate initials ("J.S.", "P.I."); folding would otherwise glue them into one word.
    family = replaceAll(std::move(family), ".", " ");
    given = replaceAll(std::move(given), ".", " ");
    std::vector<std::string> familyWords = words(foldForKey(family));
    out.surname = familyWords.empty() ? std::string() : familyWords.back();  // "Vaughan Williams" -> williams

    std::vector<std::string> givenWords = words(foldForKey(given));
    // "JS" / "CPE" written without dots: every letter is an initial.
    if (givenWords.size() == 1 && givenWords[0].size() <= 3 &&
        std::all_of(given.begin(), given.end(), [](unsigned char c) { return std::isupper(c) != 0 || c == ' '; })) {
        out.initials = givenWords[0];
    } else {
        out.initials = initialsOf(givenWords);
    }
    return out;
}

ComposerNames::ComposerNames(const std::vector<std::string>& allComposerTags) {
    std::map<std::string, std::set<std::string>> bySurname;
    for (const std::string& tag : allComposerTags) {
        const ComposerName n = parseComposerName(tag);
        if (!n.surname.empty()) {
            bySurname[n.surname].insert(n.initials);
        }
    }
    for (auto& [surname, set] : bySurname) {
        Group g;
        g.initials.assign(set.begin(), set.end());
        groups_.emplace_back(surname, std::move(g));
    }
    // Fingerprint: the full mapping surname|initials -> canonical, in a stable order.
    for (const auto& [surname, group] : groups_) {
        for (const std::string& initials : group.initials) {
            fingerprint_ += surname + "|" + initials + ">" + canonicalInitials(surname, initials) + ";";
        }
    }
}

// The longest initials string that `initials` is a prefix of, if all such candidates form one chain
// (each a prefix of the next); otherwise `initials` unchanged.
std::string ComposerNames::canonicalInitials(const std::string& surname, const std::string& initials) const {
    for (const auto& [name, group] : groups_) {
        if (name != surname) {
            continue;
        }
        std::vector<std::string> candidates;
        for (const std::string& other : group.initials) {
            if (other.compare(0, initials.size(), initials) == 0) {
                candidates.push_back(other);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
        for (std::size_t i = 1; i < candidates.size(); ++i) {
            if (candidates[i].compare(0, candidates[i - 1].size(), candidates[i - 1]) != 0) {
                return initials;  // two different people share this prefix
            }
        }
        return candidates.empty() ? initials : candidates.back();
    }
    return initials;
}

std::string ComposerNames::key(std::string_view composerTag) const {
    const ComposerName n = parseComposerName(composerTag);
    if (n.surname.empty()) {
        return normalizeText(composerTag);
    }
    return n.surname + "|" + canonicalInitials(n.surname, n.initials);
}

}  // namespace mp::core::library
