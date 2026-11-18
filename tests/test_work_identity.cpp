#include <catch2/catch_test_macros.hpp>

#include "core/library/work_identity.hpp"
#include "core/library/work_parser.hpp"

using namespace mp::core::library;

namespace {

std::string key(const char* title) { return identityKey(identifyWork(title)); }
// Form + number alone: what lets "Symphony No. 5" find "Symphony No. 5, Op. 64" (D39).
std::string nkey(const char* title) { return numberKey(identifyWork(title)); }

}  // namespace

TEST_CASE("identity: one symphony written many ways", "[identity]") {
    const std::string expected = "f:symphony#5";
    CHECK(nkey("Symphony No. 5 in E minor, Op. 64") == expected);
    CHECK(nkey("Symphony No.5 in E Minor") == expected);
    CHECK(nkey("Symphony #5") == expected);
    CHECK(nkey("Symphony 5") == expected);
    CHECK(nkey("Symphony No. 5") == expected);
    CHECK(nkey("Symphonie Nr. 5 e-Moll op. 64") == expected);
    CHECK(nkey("5. Sinfonie e-Moll op. 64") == expected);
    CHECK(nkey("Sinfonía n.º 5 en mi menor, op. 64") == expected);
    CHECK(nkey("Symphonie n° 5 en mi mineur") == expected);
    CHECK(nkey("5th Symphony") == expected);
    CHECK(nkey("Sinfonia n. 5 in mi minore") == expected);
    CHECK(nkey("Symphony No. 6 in B minor, Op. 74 'Pathétique'") == "f:symphony#6");
}

TEST_CASE("identity: instrument matters for concertos and sonatas", "[identity]") {
    CHECK(nkey("Piano Concerto No. 1 in B-flat minor, Op. 23") == "f:piano concerto#1");
    CHECK(nkey("Concerto for Piano and Orchestra No. 1") == "f:piano concerto#1");
    CHECK(nkey("Klavierkonzert Nr. 1 b-Moll op. 23") == "f:piano concerto#1");
    CHECK(nkey("Concerto pour piano n° 1") == "f:piano concerto#1");
    CHECK(nkey("Violin Concerto in D major, Op. 35").empty());  // no number
    CHECK(nkey("Violin Concerto No. 1 in G minor") == "f:violin concerto#1");
    CHECK(nkey("Violinkonzert Nr. 1") == "f:violin concerto#1");
    CHECK(nkey("Violin Concerto No. 1") != nkey("Piano Concerto No. 1"));
    CHECK(nkey("Piano Sonata No. 14 in C-sharp minor, Op. 27 No. 2 'Moonlight'") == "f:piano sonata#14");
    CHECK(nkey("Sonata for Violin and Piano No. 5") == "f:violin sonata#5");
    CHECK(nkey("Violin Sonata No. 5 in F major, Op. 24") == "f:violin sonata#5");
    CHECK(nkey("String Quartet No. 14 in D minor, D. 810") == "f:string quartet#14");
    CHECK(nkey("Streichquartett Nr. 14") == "f:string quartet#14");
}

TEST_CASE("identity: the No. of an opus number is not a work number", "[identity]") {
    CHECK(key("String Quartet in F major, Op. 18 No. 1") == "c:Op. 18 No. 1");
    CHECK(key("String Quartet in F major, Op. 18 No. 1") != key("String Quartet in G major, Op. 18 No. 2"));
}

TEST_CASE("identity: no number, no catalog: form and key", "[identity]") {
    CHECK(key("Piano Sonata in B minor") == "f:piano sonata/B minor");
    CHECK(key("Klaviersonate h-Moll") == "f:piano sonata/B minor");
    CHECK(key("Piano Sonata in B-flat major") == "f:piano sonata/B-flat major");
    CHECK(key("Klaviersonate B-Dur") == "f:piano sonata/B-flat major");  // German B is B-flat
    CHECK(key("Klaviersonate H-Dur") == "f:piano sonata/B major");
    CHECK(key("Sonate Es-Dur") == "f:sonata/E-flat major");
    CHECK(key("Sonata in F# minor") == "f:sonata/F-sharp minor");
    CHECK(key("Sonata in F\xE2\x99\xAF minor") == "f:sonata/F-sharp minor");
    CHECK(key("Sonate fis-Moll") == "f:sonata/F-sharp minor");
    CHECK(key("Sonata in B\xE2\x99\xAD major") == "f:sonata/B-flat major");
}

TEST_CASE("identity: the catalog number beats the number, the number beats the key", "[identity]") {
    CHECK(key("Symphony No. 9 in E minor, Op. 95 'From the New World'") == "c:Op. 95");
    CHECK(key("Symphony No. 9 'From the New World'") == "f:symphony#9");
    CHECK(key("Piano Sonata in B-flat major, D. 960") == "c:D. 960");
    CHECK(key("Piano Sonata in A major, D. 959") != key("Piano Sonata in A major, D. 664"));
    // One catalog number is one work, whatever each edition calls it.
    CHECK(key("Lute Suite No. 2 in A minor, BWV 997") == key("Partita in C Minor for Lute, BWV 997 - Arr. for Guitar"));
    CHECK(key("Sonata in G Minor, BWV 1001") == key("Sonata for Violin Solo No. 1 in G Minor, BWV 1001"));
}

TEST_CASE("identity: titles without an identity give none", "[identity]") {
    CHECK(key("The Nutcracker").empty());
    CHECK(key("Hotel California").empty());
    CHECK(key("Swan Lake").empty());
    CHECK(key("").empty());
}

TEST_CASE("fold: accents and transliterations", "[identity]") {
    CHECK(foldForKey("Dvo\xC5\x99\xC3\xA1k") == "dvorak");
    CHECK(foldForKey("Tchaikovsky") == foldForKey("Tchaikowsky"));
    CHECK(foldForKey("Tchaikovsky") == foldForKey("Tschaikowski"));
    CHECK(foldForKey("Tchaikovsky") == foldForKey("Czajkowski"));
    CHECK(foldForKey("Tchaikovsky") == foldForKey("Chaikovsky"));
    CHECK(foldForKey("Sch\xC3\xB6nberg") == "schonberg");
    CHECK(foldForKey("Bartók") == "bartok");
    CHECK(foldForKey("Mu\xC3\x9F" "orgsky").find("ss") != std::string::npos);
}

TEST_CASE("composers: parsing names", "[identity][composers]") {
    auto check = [](const char* name, const char* surname, const char* initials) {
        const ComposerName n = parseComposerName(name);
        CHECK(n.surname == surname);
        CHECK(n.initials == initials);
    };
    check("Tchaikovsky, Pyotr Ilyich", "chaikovski", "pi");
    check("Pyotr Ilyich Tchaikovsky", "chaikovski", "pi");
    check("P. I. Tchaikovsky", "chaikovski", "pi");
    check("P.I. Tchaikovsky", "chaikovski", "pi");
    check("Peter Iljitsch Tschaikowski", "chaikovski", "pi");
    check("Tchaikovsky", "chaikovski", "");
    check("Bach, Johann Sebastian", "bach", "is");  // j folds to i, consistently
    check("J.S. Bach", "bach", "is");
    check("JS Bach", "bach", "is");
    check("C.P.E. Bach", "bach", "cpe");
    check("Ludwig van Beethoven", "beethoven", "lv");
    check("Vaughan Williams, Ralph", "villiams", "r");  // w folds to v
    check("Ralph Vaughan Williams", "villiams", "rv");
    check("Mozart; Haydn", "mozart", "");
}

TEST_CASE("composers: spellings of one person get one key, different people stay apart", "[identity][composers]") {
    const ComposerNames names({"Tchaikovsky, Pyotr Ilyich", "P. I. Tchaikovsky", "Tchaikovsky", "Tchaikowsky, Peter",
                               "Bach, Johann Sebastian", "J.S. Bach", "Bach, Carl Philipp Emanuel", "C.P.E. Bach", "Bach",
                               "Mozart, Wolfgang Amadeus", "Mozart"});
    const std::string t = names.key("Tchaikovsky, Pyotr Ilyich");
    CHECK(names.key("P. I. Tchaikovsky") == t);
    CHECK(names.key("Tchaikovsky") == t);
    CHECK(names.key("Tchaikowsky, Peter") == t);
    CHECK(names.key("Pyotr Ilyich Tchaikovsky") == t);

    CHECK(names.key("Bach, Johann Sebastian") == names.key("J.S. Bach"));
    CHECK(names.key("Bach, Carl Philipp Emanuel") == names.key("C.P.E. Bach"));
    CHECK(names.key("J.S. Bach") != names.key("C.P.E. Bach"));
    // Two Bachs in the library: a bare "Bach" cannot be assigned to either.
    CHECK(names.key("Bach") != names.key("J.S. Bach"));
    CHECK(names.key("Bach") != names.key("C.P.E. Bach"));

    CHECK(names.key("Mozart") == names.key("Mozart, Wolfgang Amadeus"));
    CHECK(names.key("Mozart") != t);
}

TEST_CASE("composers: the fingerprint changes when the mapping does", "[identity][composers]") {
    const ComposerNames a({"Bach, Johann Sebastian", "Bach"});
    const ComposerNames sameAgain({"Bach", "Bach, Johann Sebastian"});
    const ComposerNames b({"Bach, Johann Sebastian", "Bach", "Bach, Carl Philipp Emanuel"});
    CHECK(a.fingerprint() == sameAgain.fingerprint());
    CHECK(a.fingerprint() != b.fingerprint());
    CHECK(a.key("Bach") == a.key("Bach, Johann Sebastian"));  // one Bach known
    CHECK(b.key("Bach") != b.key("Bach, Johann Sebastian"));  // two known
}

TEST_CASE("detect works: recordings spelled differently by different composer spellings are one work", "[identity][works]") {
    auto track = [](std::int64_t id, std::int64_t album, const char* title, const char* composer) {
        WorkTrackInput t;
        t.id = id;
        t.albumId = album;
        t.trackNo = static_cast<int>(id);
        t.title = title;
        t.composer = composer;
        t.workTag = title;
        return t;
    };
    const ComposerNames names({"Tchaikovsky, Pyotr Ilyich", "Tchaikovsky"});
    const auto r = detectWorks({track(1, 1, "Symphony No. 5 in E minor, Op. 64", "Tchaikovsky, Pyotr Ilyich"),
                                track(2, 2, "Symphony No.5 in E Minor", "Tchaikovsky"),
                                track(3, 3, "Sinfon\xC3\xAD" "a n.\xC2\xBA 5 en mi menor", "Tchaikovsky"),
                                track(4, 4, "Symphony No. 6", "Tchaikovsky")},
                               &names);
    // The first has a catalog number, the others only a number: the scan's merge step joins them (D39,
    // test_works). Here: same composer key, same number key.
    CHECK(r[0].workKey == r[0].workKey.substr(0, r[0].workKey.find('\x1F')) + "\x1F" + "c:Op. 64");
    CHECK(r[1].workKey == r[2].workKey);
    CHECK(r[0].workKey.substr(0, r[0].workKey.find('\x1F')) == r[1].workKey.substr(0, r[1].workKey.find('\x1F')));
    CHECK(r[0].numberKey == r[1].numberKey);
    CHECK(r[0].numberKey == r[2].numberKey);
    CHECK(r[0].workKey != r[3].workKey);
    // Without the composer helper the two spellings are different people.
    const auto plain = detectWorks({track(1, 1, "Symphony No. 5", "Tchaikovsky, Pyotr Ilyich"), track(2, 2, "Symphony No. 5", "Tchaikovsky")});
    CHECK(plain[0].workKey != plain[1].workKey);
}

TEST_CASE("identity: solo instruments however they are written (D39)", "[identity]") {
    CHECK(nkey("Sonata for Solo Violin No. 2 in A Minor, BWV 1003") == "f:violin sonata#2");
    CHECK(nkey("Sonata for Violin Solo No. 2 in A Minor, BWV 1003") == "f:violin sonata#2");
    CHECK(nkey("Sonate pour violon seul n\xC2\xB0 2") == "f:violin sonata#2");
    CHECK(nkey("Lute Suite No. 2 in A minor, BWV 997") == "f:lute suite#2");
    CHECK(nkey("Suite for Lute No. 2") == "f:lute suite#2");
    CHECK(nkey("Lautensuite Nr. 2") == "f:lute suite#2");
    CHECK(identifyWork("Partita in C Minor for Lute, BWV 997 - Arr. for Guitar").instrument == "lute");
    CHECK(identifyWork("Partita in D Minor for Solo Violin, BWV 1004").instrument == "violin");
    CHECK(identifyWork("Partita No.1 in B flat, BWV 825 - Arr. Guitar (D major)").instrument.empty());
    CHECK(identifyWork("Partita No. 2, BWV 1004, arr. for guitar").instrument.empty());
    CHECK(identifyWork("Theorbo Suite No. 1").instrument == "theorbo");
}

TEST_CASE("parser: a movement glued onto the catalog number", "[parser]") {
    const TitleParts p = splitTitle("Sonata for Violin Solo No. 2 in A Minor, BWV 1003 IV. Allegro");
    CHECK(p.work == "Sonata for Violin Solo No. 2 in A Minor, BWV 1003");
    CHECK(p.movementNo == 4);
    CHECK(p.movement == "Allegro");
    CHECK_FALSE(splitAfterCatalog("Sonata in G Minor, BWV 1001").has_value());
    CHECK_FALSE(splitAfterCatalog("Mass, BWV 232 (1748)").has_value());
    // A separator still wins.
    CHECK(splitTitle("Cello Suite No. 1, BWV 1007: I. Prelude").movement == "Prelude");
}

TEST_CASE("identity: roman and ordinal-word numbers, qualifier words (D40)", "[identity]") {
    CHECK(identifyWork("Partita II (d'apr\xC3\xA8s BWV 1004 pour violon solo)").number == 2);
    CHECK(identifyWork("Deuxi\xC3\xA8me concert Brandebourgois en fa majeur, BWV 1047").number == 2);
    CHECK(identifyWork("Cinqui\xC3\xA8me concerto Brandebourgeois en R\xC3\xA9 majeur").number == 5);
    CHECK(identifyWork("F\xC3\xBC" "nfte Sinfonie").number == 5);
    CHECK(identifyWork("Concerto in C, RV 558").number == 0);
    // Other words that name the work keep numbered works apart; spelling and language do not.
    CHECK(nkey("Brandenburg Concerto No. 1") != nkey("Concerto No. 1"));
    CHECK(nkey("Brandenburg Concerto No. 1") == nkey("Brandenburgisches Konzert Nr. 1"));
    CHECK(nkey("Brandenburg Concerto No. 2") == nkey("Deuxi\xC3\xA8me concert Brandebourgois en fa majeur"));
    CHECK(nkey("Symphony No. 5 'Fate'") == nkey("Symphony No. 5"));
}

TEST_CASE("parser: titles from a real library (D40)", "[parser]") {
    // A colon glued to the movement
    TitleParts p = splitTitle("Suite in E Minor, BWV 996 (Arr. for Guitar):IV. Sarabande");
    CHECK(p.work == "Suite in E Minor, BWV 996 (Arr. for Guitar)");
    CHECK(p.movementNo == 4);
    CHECK(p.movement == "Sarabande");
    // Two movements in one track are not work + movement
    p = splitTitle("IV. Menuet II - V. Menuet I");
    CHECK(p.work.empty());
    CHECK(p.movementNo == 4);
    // BWV numbers have no sub-numbers: "No. 1" after one is the movement
    p = splitTitle("Suite Bwv 812, No. 1 - Allemand");
    CHECK(p.work == "Suite Bwv 812");
    CHECK(p.movementNo == 1);
    CHECK(p.movement == "Allemand");
    CHECK(splitTitle("Prelude, Op. 28, No. 4 - Largo").work == "Prelude, Op. 28, No. 4");  // opus keeps its No.
    // A catalog number after "inspired by" names another work
    CHECK_FALSE(findCatalogNumber("Bach autrement II (Inspired by Prelude, BWV 1007)").has_value());
    CHECK(findCatalogNumber("Partita II (d'apr\xC3\xA8s BWV 1004 pour violon solo)") == "BWV 1004");
    // The composer's own catalog beats the opus number
    CHECK(findCatalogNumber("Violin Concerto in E major \xE2\x80\x9CSpring\xE2\x80\x9D, op. 8 no. 1, RV 269") == "RV 269");
    CHECK(findCatalogNumber("Symphony No. 9, Op. 125") == "Op. 125");
}

TEST_CASE("detect works: a leading composer name is not the work (D40)", "[identity][works]") {
    WorkTrackInput t;
    t.id = 1;
    t.albumId = 1;
    t.trackNo = 1;
    t.composer = "Johann Sebastian Bach";
    t.title = "J.S. Bach: Prelude in C Minor, BWV 999";
    WorkTrackInput u = t;
    u.id = 2;
    u.trackNo = 2;
    u.title = "J.S. Bach: Fugue in G Minor, BWV 1000";
    const auto r = detectWorks({t, u});
    CHECK(r[0].workTitle == "Prelude in C Minor, BWV 999");
    CHECK(r[1].workTitle == "Fugue in G Minor, BWV 1000");
    CHECK(r[0].workKey != r[1].workKey);
    // Same for a WORK tag
    WorkTrackInput w = t;
    w.workTag = "Beethoven: Egmont Overture, Op. 84";
    w.composer = "Ludwig van Beethoven";
    CHECK(detectWorks({w})[0].workTitle == "Egmont Overture, Op. 84");
}
