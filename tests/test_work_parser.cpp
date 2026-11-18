#include <catch2/catch_test_macros.hpp>

#include "core/library/work_parser.hpp"

using namespace mp::core::library;

namespace {

std::string catalog(const char* text) { return findCatalogNumber(text).value_or("<none>"); }

}  // namespace

TEST_CASE("catalog numbers: the supported catalogs", "[works]") {
    CHECK(catalog("Cello Suite No. 1 in G major, BWV 1007") == "BWV 1007");
    CHECK(catalog("Suite in E-flat major, BWV998") == "BWV 998");
    CHECK(catalog("Prelude, Fugue and Allegro, BWV. 998") == "BWV 998");
    CHECK(catalog("Piano Concerto No. 21 in C major, K. 467") == "K. 467");
    CHECK(catalog("Eine kleine Nachtmusik, K.525") == "K. 525");
    CHECK(catalog("Requiem in D minor, KV 626") == "K. 626");
    CHECK(catalog("Symphony No. 5 in C minor, Op. 67") == "Op. 67");
    CHECK(catalog("Symphony No. 9, Opus 125") == "Op. 125");
    CHECK(catalog("String Quartet No. 7 in F major, Op. 59 No. 1 'Razumovsky'") == "Op. 59 No. 1");
    CHECK(catalog("String Quartet in F, Op.18/1") == "Op. 18 No. 1");
    CHECK(catalog("Piano Sonata No. 14, Op. 27, No. 2") == "Op. 27 No. 2");
    CHECK(catalog("Piano Sonata in B-flat major, D. 960") == "D. 960");
    CHECK(catalog("String Quartet in D minor, D 810 'Death and the Maiden'") == "D. 810");
    CHECK(catalog("Messiah, HWV 56") == "HWV 56");
    CHECK(catalog("Concerto in A minor, RV 356") == "RV 356");
    CHECK(catalog("Symphony No. 104 in D major, Hob. I:104") == "Hob. I:104");
    CHECK(catalog("Piano Sonata in E-flat, Hob. XVI/52") == "Hob. XVI:52");
    CHECK(catalog("Liebestraum No. 3, S. 541") == "S. 541");
}

TEST_CASE("catalog numbers: ordinary text is not a catalog number", "[works]") {
    CHECK(catalog("Sonata in D major") == "<none>");
    CHECK(catalog("Symphony in D") == "<none>");
    CHECK(catalog("Concerto in D 1st movement") == "<none>");
    CHECK(catalog("Opening Titles") == "<none>");
    CHECK(catalog("Kind of Blue") == "<none>");
    CHECK(catalog("Hello (Live)") == "<none>");
    CHECK(catalog("So What") == "<none>");
    CHECK(catalog("Track 12") == "<none>");
    CHECK(catalog("") == "<none>");
}

TEST_CASE("catalog numbers: the earliest one in the text wins", "[works]") {
    CHECK(catalog("Piano Concerto, K. 466, arr. Op. 5") == "K. 466");
}

TEST_CASE("roman numerals", "[works]") {
    CHECK(parseRoman("I") == 1);
    CHECK(parseRoman("IV") == 4);
    CHECK(parseRoman("IX") == 9);
    CHECK(parseRoman("XII") == 12);
    CHECK(parseRoman("XIV") == 14);
    CHECK(parseRoman("XXXIX") == 39);
    CHECK_FALSE(parseRoman("IIII").has_value());
    CHECK_FALSE(parseRoman("VX").has_value());
    CHECK_FALSE(parseRoman("i").has_value());
    CHECK_FALSE(parseRoman("").has_value());
    CHECK_FALSE(parseRoman("IVX").has_value());
}

TEST_CASE("movement markers", "[works]") {
    CHECK(parseMovementMarker("II. Andante")->number == 2);
    CHECK(parseMovementMarker("IV) Finale")->number == 4);
    CHECK(parseMovementMarker("III - Scherzo")->number == 3);
    CHECK(parseMovementMarker("3. Allegro")->number == 3);
    CHECK(parseMovementMarker("12) Presto")->number == 12);
    CHECK(parseMovementMarker("  I: Adagio")->number == 1);
    CHECK(parseMovementMarker("II. Andante")->length == 4);
    CHECK_FALSE(parseMovementMarker("I Want You").has_value());
    CHECK_FALSE(parseMovementMarker("Imagine").has_value());
    CHECK_FALSE(parseMovementMarker("IVAN. The Terrible").has_value());
    CHECK_FALSE(parseMovementMarker("2.0 Remaster").has_value());
    CHECK_FALSE(parseMovementMarker("1984").has_value());
    CHECK_FALSE(parseMovementMarker("Andante").has_value());
    CHECK_FALSE(parseMovementMarker("").has_value());
}

TEST_CASE("split titles: work and movement", "[works]") {
    SECTION("colon") {
        const TitleParts p = splitTitle("Cello Suite No. 1 in G major, BWV 1007: I. Prélude");
        CHECK(p.work == "Cello Suite No. 1 in G major, BWV 1007");
        CHECK(p.movement == "Prélude");
        CHECK(p.movementNo == 1);
        CHECK(p.hasMarker);
    }
    SECTION("dash") {
        const TitleParts p = splitTitle("Symphony No. 5 in C minor, Op. 67 - II. Andante con moto");
        CHECK(p.work == "Symphony No. 5 in C minor, Op. 67");
        CHECK(p.movement == "Andante con moto");
        CHECK(p.movementNo == 2);
    }
    SECTION("en dash") {
        const TitleParts p = splitTitle("Piano Sonata No. 8 \xE2\x80\x93 III. Rondo");
        CHECK(p.work == "Piano Sonata No. 8");
        CHECK(p.movement == "Rondo");
        CHECK(p.movementNo == 3);
    }
    SECTION("the last separator with a marker wins") {
        const TitleParts p = splitTitle("Piano Sonata No. 8: Pathétique: I. Grave - Allegro di molto e con brio");
        CHECK(p.work == "Piano Sonata No. 8: Pathétique");
        CHECK(p.movement == "Grave - Allegro di molto e con brio");
        CHECK(p.movementNo == 1);
    }
    SECTION("no marker: first separator") {
        const TitleParts p = splitTitle("Symphony No. 9: Scherzo - Molto vivace");
        CHECK(p.work == "Symphony No. 9");
        CHECK(p.movement == "Scherzo - Molto vivace");
        CHECK_FALSE(p.hasMarker);
        CHECK(p.movementNo == 0);
    }
    SECTION("marker without a separator") {
        const TitleParts p = splitTitle("II. Andante");
        CHECK(p.work.empty());
        CHECK(p.movement == "Andante");
        CHECK(p.movementNo == 2);
    }
    SECTION("plain title") {
        const TitleParts p = splitTitle("Hotel California");
        CHECK(p.work.empty());
        CHECK(p.movement == "Hotel California");
        CHECK_FALSE(p.hasMarker);
    }
    SECTION("a hyphen inside a word is not a separator") {
        const TitleParts p = splitTitle("Piano Sonata in E-flat major");
        CHECK(p.work.empty());
    }
}

namespace {

WorkTrackInput track(std::int64_t id, std::int64_t album, int no, const char* title, const char* composer = "") {
    WorkTrackInput t;
    t.id = id;
    t.albumId = album;
    t.trackNo = no;
    t.title = title;
    t.composer = composer;
    t.albumArtist = "Some Artist";
    return t;
}

}  // namespace

TEST_CASE("detect works: WORK and movement tags win", "[works]") {
    WorkTrackInput a = track(1, 1, 1, "Prélude", "Johann Sebastian Bach");
    a.workTag = "Cello Suite No. 1 in G major, BWV 1007";
    a.movementName = "Prélude";
    a.movementNo = 1;
    WorkTrackInput b = track(2, 1, 2, "Allemande", "Johann Sebastian Bach");
    b.workTag = a.workTag;
    b.movementName = "Allemande";
    b.movementNo = 2;

    const auto r = detectWorks({a, b});
    REQUIRE(r.size() == 2);
    CHECK(r[0].source == WorkSource::Tag);
    CHECK(r[0].workTitle == "Cello Suite No. 1 in G major, BWV 1007");
    CHECK(r[0].catalogNo == "BWV 1007");
    CHECK(r[0].workKey == r[1].workKey);
    CHECK(r[0].movementNo == 1);
    CHECK(r[1].movementNo == 2);
    CHECK(r[1].movementName == "Allemande");
    CHECK(r[0].movementFromTag);
}

TEST_CASE("detect works: WORK tag without movement tags takes them from the title", "[works]") {
    WorkTrackInput a = track(1, 1, 1, "Symphony No. 5: I. Allegro con brio", "Beethoven");
    a.workTag = "Symphony No. 5 in C minor, Op. 67";
    WorkTrackInput b = track(2, 1, 2, "Andante con moto", "Beethoven");
    b.workTag = a.workTag;

    const auto r = detectWorks({a, b});
    CHECK(r[0].movementName == "Allegro con brio");
    CHECK(r[0].movementNo == 1);
    CHECK_FALSE(r[0].movementFromTag);
    CHECK(r[1].movementName == "Andante con moto");
    CHECK(r[1].movementNo == 2);  // position in the recording
}

TEST_CASE("detect works: titles with a shared prefix and movement numerals", "[works]") {
    const auto r = detectWorks({
        track(1, 1, 1, "Cello Suite No. 1 in G major, BWV 1007: I. Prélude", "J.S. Bach"),
        track(2, 1, 2, "Cello Suite No. 1 in G major, BWV 1007: II. Allemande", "J.S. Bach"),
        track(3, 1, 3, "Cello Suite No. 1 in G major, BWV 1007: III. Courante", "J.S. Bach"),
        track(4, 1, 4, "Cello Suite No. 2 in D minor, BWV 1008: I. Prélude", "J.S. Bach"),
        track(5, 1, 5, "Cello Suite No. 2 in D minor, BWV 1008: II. Allemande", "J.S. Bach"),
    });
    REQUIRE(r.size() == 5);
    CHECK(r[0].source == WorkSource::Title);
    CHECK(r[0].catalogNo == "BWV 1007");
    CHECK(r[0].workKey == r[2].workKey);
    CHECK(r[0].workKey != r[3].workKey);
    CHECK(r[3].catalogNo == "BWV 1008");
    CHECK(r[1].movementNo == 2);
    CHECK(r[1].movementName == "Allemande");
    CHECK(r[4].movementName == "Allemande");
}

TEST_CASE("detect works: a prefix without markers needs two tracks plus a reason", "[works]") {
    SECTION("two tracks, composer tag") {
        const auto r = detectWorks({
            track(1, 1, 1, "Symphony No. 9: Scherzo", "Beethoven"),
            track(2, 1, 2, "Symphony No. 9: Adagio", "Beethoven"),
        });
        CHECK(r[0].source == WorkSource::Title);
        CHECK(r[0].workKey == r[1].workKey);
        CHECK(r[0].movementNo == 1);  // numbered by position
        CHECK(r[1].movementNo == 2);
    }
    SECTION("pop album: no composer, no catalog, no markers") {
        const auto r = detectWorks({
            track(1, 1, 1, "Live: Intro"),
            track(2, 1, 2, "Live: Outro"),
        });
        CHECK(r[0].source == WorkSource::None);
        CHECK(r[1].source == WorkSource::None);
    }
    SECTION("one lone prefixed track without composer") {
        const auto r = detectWorks({track(1, 1, 1, "Symphony No. 9: Scherzo")});
        CHECK(r[0].source == WorkSource::None);
    }
}

TEST_CASE("detect works: a marker alone is not enough in a pop album", "[works]") {
    const auto r = detectWorks({
        track(1, 1, 1, "Hello"),
        track(2, 1, 2, "II. Interlude"),
        track(3, 1, 3, "World"),
    });
    for (const auto& a : r) {
        CHECK(a.source == WorkSource::None);
    }
}

TEST_CASE("detect works: one composer-tagged track with marker is a work", "[works]") {
    const auto r = detectWorks({track(1, 1, 1, "Piano Sonata in B minor, S. 178: Lento assai", "Liszt")});
    CHECK(r[0].source == WorkSource::Title);
    CHECK(r[0].catalogNo == "S. 178");
    CHECK(r[0].movementName == "Lento assai");
}

TEST_CASE("detect works: a lone track with composer and catalog number", "[works]") {
    const auto r = detectWorks({track(1, 1, 1, "Chaconne from Partita No. 2 in D minor BWV 1004", "J.S. Bach")});
    CHECK(r[0].source == WorkSource::Title);
    CHECK(r[0].catalogNo == "BWV 1004");
    CHECK(r[0].movementNo == 1);
    // Without the composer it is just a title.
    const auto r2 = detectWorks({track(1, 1, 1, "Chaconne from Partita No. 2 in D minor BWV 1004")});
    CHECK(r2[0].source == WorkSource::None);
}

TEST_CASE("detect works: the same work on two albums gets one key (D24)", "[works]") {
    WorkTrackInput a = track(1, 1, 1, "x", "Bach");
    a.workTag = "Suite in E-flat major, BWV 998";
    WorkTrackInput b = track(2, 2, 1, "y", "Bach");
    b.workTag = "Suite in E flat, BWV998";
    const auto r = detectWorks({a, b});
    CHECK(r[0].workKey == r[1].workKey);
    // Another composer's BWV-looking number does not merge.
    WorkTrackInput c = track(3, 3, 1, "z", "Someone Else");
    c.workTag = "Suite, BWV 998";
    CHECK(detectWorks({c})[0].workKey != r[0].workKey);
}

TEST_CASE("detect works: groups are per disc and order of input does not matter", "[works]") {
    WorkTrackInput a = track(1, 1, 1, "Op. 18 No. 1: I. Allegro", "Beethoven");
    a.discNo = 1;
    WorkTrackInput b = track(2, 1, 1, "Op. 18 No. 1: II. Adagio", "Beethoven");
    b.discNo = 2;
    const auto fwd = detectWorks({a, b});
    const auto rev = detectWorks({b, a});
    CHECK(fwd[0].workKey == fwd[1].workKey);
    CHECK(rev[0].trackId == 2);
    CHECK(rev[1].trackId == 1);
    CHECK(fwd[0].movementNo == 1);
    CHECK(fwd[1].movementNo == 2);
}

TEST_CASE("detect works: tracks without an album are judged alone", "[works]") {
    const auto r = detectWorks({
        track(1, 0, 1, "Symphony No. 9: Scherzo", "Beethoven"),
        track(2, 0, 2, "Symphony No. 9: Adagio", "Beethoven"),
    });
    // Each stands alone: no shared prefix count, but the composer tag + no marker + no catalog = not enough.
    CHECK(r[0].source == WorkSource::None);
    CHECK(r[1].source == WorkSource::None);
}
