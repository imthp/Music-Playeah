#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mp::core::library {

// ---- Work identity (SPEC §7b, D24, D36) -------------------------------------------------------
//
// Two recordings of "the same work" are rarely tagged the same way: "Symphony No. 5 in E minor, Op. 64",
// "Symphony No.5", "Symphony #5", "Sinfonía n.º 5 en mi menor", "5. Sinfonie e-Moll" all mean one work.
// This module reduces such text to a language-independent identity, and composer names to a spelling-
// independent key, so the library can store the work once. Pure functions; UTF-8 in and out.

struct WorkIdentity {
    std::string form;        // canonical English form: "symphony", "concerto", "sonata", "string quartet"...
    std::string instrument;  // canonical solo instrument or "" ("piano", "violin", "string"...)
    int number = 0;          // "No. 5" -> 5; 0 = none
    std::string musicalKey;  // "E minor", "B-flat major"; "" = none
    std::string catalog;     // from findCatalogNumber, "" = none
    std::string qualifier;   // other words naming the work, stemmed: "Brandenburg Concerto" -> "brand"; "" = none
};

// Reads form, instrument, number and key from a work title in English, German, French, Italian or Spanish.
// Fields that are not present stay empty/0.
WorkIdentity identifyWork(std::string_view text);

// The part of the identity that decides whether two titles are the same work; "" if the text gives nothing
// reliable (then the caller falls back to the normalized title). In order of preference (D39):
//   catalog number                 "c:BWV 998"              one catalog number is one work, whatever the title
//   form + number (+ instrument)   "f:piano concerto#1"     numbered works without a catalog number
//   form + key (+ instrument)      "f:sonata/B minor"       the rest
std::string identityKey(const WorkIdentity& id);

// Form + number (+ instrument, + qualifier words) alone, "" if the title has no form or no number:
// "f:symphony#5", "f:piano concerto#1", "f:concerto [brand]#1" (so "Brandenburg Concerto No. 1" is not any
// "Concerto No. 1"). A work found by catalog
// number keeps it, so a recording titled only "Lute Suite No. 2" can be matched to "…, BWV 997".
std::string numberKey(const WorkIdentity& id);

// Lower case with accents and transliteration variants folded: "Dvořák" == "Dvorak", "Čajkovskij"-style
// spellings of Tchaikovsky all give "chaikovski". Only for building keys, never for display.
std::string foldForKey(std::string_view utf8);

// ---- Families (browse grouping) ----------------------------------------------------------------
//
// Works that belong together on a composer's page: "Brandenburg Concerto No. 1 in F major, BWV 1046" and
// "Brandenburg Concerto No. 5" are both in the family "Brandenburg Concerto"; "Cello Suite No. 1" and
// "Suite for Cello No. 3" share one family too. Only for browsing: it never changes a work's identity.
struct WorkFamily {
    std::string key;    // "" = the title gives no family name (e.g. a bare "BWV 1007")
    std::string label;  // display name, taken from the title: "Brandenburg Concerto"
    bool complete = true;  // the label itself names the key's instrument ("Partita" for a violin partita: false)
};
// Label: the title up to the first number (digits, roman "II", "No."), key, catalog number, colon or bracket,
// without a leading ordinal ("5.", "Deuxième"). Key: "f:<form>|<instrument>|<other words>" when a form is
// recognized, so spelling, language and word order do not matter ("Cello Suite" == "Suite for Cello" ==
// "Suite pour violoncelle"; other words are stemmed to 5 letters: "Brandenburg" == "Brandebourgeois"),
// else "t:<folded label>". A second instrument is ignored ("Concerto for Violin and Oboe" is a violin concerto).
WorkFamily workFamily(std::string_view workTitle);

// ---- Composers ---------------------------------------------------------------------------------

struct ComposerName {
    std::string surname;   // last word of the family part, folded ("Vaughan Williams" -> "vaughan williams" -> "williams")
    std::string initials;  // initials of the given names, folded: "Johann Sebastian" / "J.S." -> "js"; "" if none
};
// "Tchaikovsky, Pyotr Ilyich", "Pyotr Ilyich Tchaikovsky", "P. I. Tchaikovsky" -> {chaikovski, pi}.
// Several names in one tag ("A; B") use the first.
ComposerName parseComposerName(std::string_view name);

// Maps composer tag spellings of one library to one key per person. Built from every composer string in
// the library, because a bare "Tchaikovsky" can only be assigned when the library knows one Tchaikovsky:
// given the names {"Tchaikovsky, Pyotr Ilyich", "P. I. Tchaikovsky", "Tchaikovsky"} all three get one key.
// Different people stay apart ("Bach, Johann Sebastian" vs "Bach, Carl Philipp Emanuel": initials differ),
// and a bare "Bach" stays its own key while several Bachs exist.
class ComposerNames {
public:
    ComposerNames() = default;
    explicit ComposerNames(const std::vector<std::string>& allComposerTags);

    // Never empty for a non-empty name.
    std::string key(std::string_view composerTag) const;

    // Changes whenever `key()` would give a different answer for some name of the library: stored so a scan
    // knows when stored work keys are stale.
    const std::string& fingerprint() const { return fingerprint_; }

private:
    struct Group {
        std::vector<std::string> initials;  // distinct, of this surname
    };
    std::vector<std::pair<std::string, Group>> groups_;  // by folded surname
    std::string fingerprint_;
    std::string canonicalInitials(const std::string& surname, const std::string& initials) const;
};

}  // namespace mp::core::library
