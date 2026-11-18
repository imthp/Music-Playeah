// Works, movements and credits through the real pipeline: tagged files -> scan -> database (milestone 3b).
#include <catch2/catch_test_macros.hpp>

#include <taglib/id3v2tag.h>
#include <taglib/textidentificationframe.h>
#include <taglib/wavfile.h>

#include <set>

#include "core/library/library.hpp"
#include "core/library/scanner.hpp"
#include "core/library/tag_reader.hpp"
#include "library_fixtures.hpp"

using namespace mp::core::library;
using namespace mp::test;

namespace {

struct Env {
    TempDir dir;
    fs::path music = dir.path() / "music";
    Library lib;
    Scanner scanner;

    Env() : lib(dir.path() / "app" / "library.db"), scanner(lib.dbFile()) {
        fs::create_directories(music);
        REQUIRE(lib.addFolder(music).ok);
    }

    ScanSummary scan() { return scanAndWait(scanner); }

    fs::path track(const std::string& rel, const TagSpec& tags) {
        return makeTrack(music / rel, tags);
    }

    std::int64_t one(const std::string& sql) { return lib.db().scalarInt(sql); }

    // title -> (work title, movement no, movement name), from the effective view
    struct Row {
        std::string workTitle;
        std::int64_t movementNo = 0;
        std::string movementName;
        std::int64_t workId = 0;
    };
    std::map<std::string, Row> rows() {
        std::map<std::string, Row> out;
        Statement s = lib.db().prepare(
            "SELECT t.title, COALESCE(w.title, ''), t.movement_no, t.movement_name, COALESCE(t.work_id, 0) "
            "FROM v_tracks t LEFT JOIN works w ON w.id = t.work_id");
        while (s.step()) {
            out[s.getText(0)] = Row{s.getText(1), s.getInt(2), s.getText(3), s.getInt(4)};
        }
        return out;
    }
};

TagSpec spec(const std::string& title, const std::string& album, int track, const std::string& composer = "Bach") {
    TagSpec t;
    t.title = title;
    t.artist = "Some Player";
    t.album = album;
    t.composer = composer;
    t.track = std::to_string(track);
    return t;
}

}  // namespace

TEST_CASE("works: movement tags group tracks and fill the works table", "[works][scan]") {
    Env env;
    for (int i = 1; i <= 3; ++i) {
        TagSpec t = spec("Movement " + std::to_string(i), "Cello Suites", i);
        t.work = "Cello Suite No. 1 in G major, BWV 1007";
        t.movementNumber = std::to_string(i) + "/6";
        t.movementName = i == 1 ? "Prélude" : (i == 2 ? "Allemande" : "Courante");
        env.track("cs/" + std::to_string(i) + ".wav", t);
    }
    const ScanSummary summary = env.scan();
    REQUIRE(summary.fatalError.empty());

    CHECK(env.one("SELECT COUNT(*) FROM works") == 1);
    CHECK(env.one("SELECT COUNT(DISTINCT work_id) FROM tracks WHERE work_id IS NOT NULL") == 1);
    const auto rows = env.rows();
    CHECK(rows.at("Movement 1").workTitle == "Cello Suite No. 1 in G major, BWV 1007");
    CHECK(rows.at("Movement 2").movementNo == 2);
    CHECK(rows.at("Movement 2").movementName == "Allemande");
    CHECK(env.one("SELECT movement_total FROM tracks WHERE title = 'Movement 1'") == 6);
    CHECK(env.one("SELECT COUNT(*) FROM works WHERE catalog_no = 'BWV 1007' AND composer_id IS NOT NULL") == 1);
}

TEST_CASE("works: detected from titles when there are no work tags", "[works][scan]") {
    Env env;
    env.track("a/1.wav", spec("Symphony No. 5 in C minor, Op. 67: I. Allegro con brio", "Beethoven 5 & 7", 1, "Beethoven"));
    env.track("a/2.wav", spec("Symphony No. 5 in C minor, Op. 67: II. Andante con moto", "Beethoven 5 & 7", 2, "Beethoven"));
    env.track("a/3.wav", spec("Symphony No. 7 in A major, Op. 92: I. Poco sostenuto", "Beethoven 5 & 7", 3, "Beethoven"));
    env.track("a/4.wav", spec("Hotel California", "Beethoven 5 & 7", 4, ""));
    REQUIRE(env.scan().fatalError.empty());

    CHECK(env.one("SELECT COUNT(*) FROM works") == 2);
    const auto rows = env.rows();
    CHECK(rows.at("Symphony No. 5 in C minor, Op. 67: II. Andante con moto").movementNo == 2);
    CHECK(rows.at("Symphony No. 5 in C minor, Op. 67: II. Andante con moto").movementName == "Andante con moto");
    CHECK(rows.at("Symphony No. 5 in C minor, Op. 67: I. Allegro con brio").workId ==
          rows.at("Symphony No. 5 in C minor, Op. 67: II. Andante con moto").workId);
    CHECK(rows.at("Symphony No. 7 in A major, Op. 92: I. Poco sostenuto").workId !=
          rows.at("Symphony No. 5 in C minor, Op. 67: I. Allegro con brio").workId);
    CHECK(rows.at("Hotel California").workId == 0);
}

TEST_CASE("works: one work, two recordings on two albums (D24)", "[works][scan]") {
    Env env;
    for (const char* album : {"Bream plays Bach", "Williams plays Bach"}) {
        for (int i = 1; i <= 2; ++i) {
            TagSpec t = spec(std::string("Part ") + std::to_string(i), album, i);
            t.work = album[0] == 'B' ? "Suite in E-flat major, BWV 998" : "Prelude, Fugue and Allegro, BWV998";
            env.track(std::string(album) + "/" + std::to_string(i) + ".wav", t);
        }
    }
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 1);
    CHECK(env.one("SELECT COUNT(DISTINCT album_id) FROM tracks WHERE work_id IS NOT NULL") == 2);
}

TEST_CASE("works: a rescan keeps ids, picks up retags and removes unused works", "[works][scan]") {
    Env env;
    const fs::path f1 = env.track("a/1.wav", spec("Concerto in A minor, RV 356: I. Allegro", "Vivaldi", 1, "Vivaldi"));
    env.track("a/2.wav", spec("Concerto in A minor, RV 356: II. Largo", "Vivaldi", 2, "Vivaldi"));
    REQUIRE(env.scan().fatalError.empty());
    const std::int64_t work = env.one("SELECT work_id FROM tracks LIMIT 1");
    REQUIRE(work > 0);

    // Nothing changed: nothing is redone and ids stay.
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT work_id FROM tracks LIMIT 1") == work);
    CHECK(env.one("SELECT COUNT(*) FROM works") == 1);

    // Retag both files so they are no longer a work.
    for (const char* name : {"a/1.wav", "a/2.wav"}) {
        TagSpec t = spec(std::string("Plain ") + name, "Vivaldi", 1, "");
        tagFile(env.music / name, t);
        touchLater(env.music / name);
    }
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 0);
    CHECK(env.one("SELECT COUNT(*) FROM tracks WHERE work_id IS NOT NULL") == 0);
    (void)f1;
}

TEST_CASE("works: adding a track later regroups its album", "[works][scan]") {
    Env env;
    // Two tracks sharing a prefix without markers and without composer: not a work yet.
    env.track("a/1.wav", spec("Live Suite: Intro", "Live", 1, ""));
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 0);
    // A second with a marker joins: now it is a work (prefix shared by two, one with a marker).
    env.track("a/2.wav", spec("Live Suite: II. Middle", "Live", 2, ""));
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 1);
    CHECK(env.one("SELECT COUNT(*) FROM tracks WHERE work_id IS NOT NULL") == 2);
}

TEST_CASE("works: a movement override wins over the detected value", "[works][scan][overrides]") {
    Env env;
    env.track("a/1.wav", spec("Concerto in A minor, RV 356: I. Allegro", "Vivaldi", 1, "Vivaldi"));
    env.track("a/2.wav", spec("Concerto in A minor, RV 356: II. Largo", "Vivaldi", 2, "Vivaldi"));
    REQUIRE(env.scan().fatalError.empty());
    env.lib.db().exec(
        "INSERT INTO tag_overrides(track_id, field, value, source, updated_at) "
        "SELECT id, 'movement_name', 'Adagio', 'manual', 0 FROM tracks WHERE title LIKE '%Largo'");
    CHECK(env.rows().at("Concerto in A minor, RV 356: II. Largo").movementName == "Adagio");
    CHECK(env.rows().at("Concerto in A minor, RV 356: I. Allegro").movementName == "Allegro");
}

TEST_CASE("credits: PERFORMER and CONDUCTOR tags become credits and album instrument chips", "[credits][scan]") {
    Env env;
    TagSpec a = spec("One", "Guitar Recital", 1);
    a.performers = {"Julian Bream (guitar)", "Peter Pears (tenor vocals)"};
    a.conductor = "Some Conductor";
    TagSpec b = spec("Two", "Guitar Recital", 2);
    b.performers = {"Julian Bream (Guitar)"};
    env.track("g/1.wav", a);
    env.track("g/2.wav", b);
    REQUIRE(env.scan().fatalError.empty());

    CHECK(env.one("SELECT COUNT(*) FROM credits") == 4);
    CHECK(env.one("SELECT COUNT(*) FROM credits WHERE role = 'conductor'") == 1);
    CHECK(env.one("SELECT COUNT(*) FROM credits c JOIN artists a ON a.id = c.artist_id "
                  "WHERE a.name = 'Julian Bream' AND c.instrument = 'guitar'") == 2);
    CHECK(env.one("SELECT COUNT(*) FROM album_tags WHERE kind = 'instrument' AND tag = 'guitar'") == 1);
    CHECK(env.one("SELECT COUNT(*) FROM album_tags WHERE kind = 'instrument' AND tag = 'tenor vocals'") == 1);

    // Retag: drop the performers. Credits and the chips go; user tags stay.
    env.lib.db().exec(
        "INSERT INTO album_tags(album_id, tag, kind, source) SELECT id, 'late night', 'user', 'user' FROM albums");
    {
        const fs::path file = env.music / "g/1.wav";
        TagLib::FileRef ref(file.c_str());
        TagLib::PropertyMap props = ref.file()->properties();
        props.erase("PERFORMER");
        props.erase("CONDUCTOR");
        ref.file()->setProperties(props);
        REQUIRE(ref.save());
        touchLater(file);
    }
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM credits") == 1);  // only track two's guitar credit is left
    CHECK(env.one("SELECT COUNT(*) FROM album_tags WHERE kind = 'instrument' AND tag = 'tenor vocals'") == 0);
    CHECK(env.one("SELECT COUNT(*) FROM album_tags WHERE kind = 'instrument' AND tag = 'guitar'") == 1);
    CHECK(env.one("SELECT COUNT(*) FROM album_tags WHERE kind = 'user'") == 1);
}

TEST_CASE("credits: splitting and storing", "[credits]") {
    CHECK(splitPerformer("Julian Bream (guitar)") == std::make_pair(std::string("Julian Bream"), std::string("guitar")));
    CHECK(splitPerformer("Julian Bream") == std::make_pair(std::string("Julian Bream"), std::string()));
    CHECK(splitPerformer("  Anne-Sophie Mutter  (violin) ") ==
          std::make_pair(std::string("Anne-Sophie Mutter"), std::string("violin")));
    CHECK(splitPerformer("The (Other) Band (drums)") == std::make_pair(std::string("The (Other) Band"), std::string("drums")));
    CHECK(splitPerformer("(guitar)") == std::make_pair(std::string("(guitar)"), std::string()));

    const std::vector<Credit> credits = {{"performer", "A B", "piano"}, {"conductor", "C", ""}};
    CHECK(parseCredits(formatCredits(credits)) == credits);
    CHECK(parseCredits("").empty());
    CHECK(formatCredits({{"performer", "Tab\tName", "x"}}) == "performer\tTab Name\tx");
}

TEST_CASE("credits: ID3v2.4 TMCL (instrument -> musician pairs) is read", "[credits]") {
    TempDir dir;
    const fs::path file = dir.path() / "tmcl.wav";
    makeTrack(file, spec("T", "A", 1));
    {
        TagLib::RIFF::WAV::File w(file.c_str());
        auto* frame = new TagLib::ID3v2::TextIdentificationFrame("TMCL", TagLib::String::UTF8);
        TagLib::StringList pairs;
        pairs.append("guitar");
        pairs.append("Julian Bream");
        pairs.append("tenor vocals");
        pairs.append("Peter Pears");
        frame->setText(pairs);
        w.ID3v2Tag()->addFrame(frame);
        REQUIRE(w.save());
    }
    FileTags tags;
    std::string error;
    REQUIRE(readTags(file, tags, error));
    const std::vector<Credit> credits = parseCredits(tags.credits);
    REQUIRE(credits.size() == 2);
    CHECK(credits[0] == Credit{"performer", "Julian Bream", "guitar"});
    CHECK(credits[1] == Credit{"performer", "Peter Pears", "tenor vocals"});
}

TEST_CASE("works: Tchaikovsky's Fifth on three albums, tagged three ways, is one work", "[works][scan][identity]") {
    Env env;
    // Album 1: WORK tag with opus number, composer as "Last, First".
    for (int i = 1; i <= 2; ++i) {
        TagSpec t = spec("Mov " + std::to_string(i), "Karajan", i, "Tchaikovsky, Pyotr Ilyich");
        t.work = "Symphony No. 5 in E minor, Op. 64";
        env.track("a1/" + std::to_string(i) + ".wav", t);
    }
    // Album 2: titles only, different spelling, composer "First Last".
    env.track("a2/1.wav", spec("Symphony No.5 in E Minor: I. Andante - Allegro con anima", "Mravinsky", 1, "Pyotr Ilyich Tchaikovsky"));
    env.track("a2/2.wav", spec("Symphony No.5 in E Minor: II. Andante cantabile", "Mravinsky", 2, "Pyotr Ilyich Tchaikovsky"));
    // Album 3: Spanish tags, bare surname.
    for (int i = 1; i <= 2; ++i) {
        TagSpec t = spec("Mov " + std::to_string(i), "Abbado", i, "Tchaikovsky");
        t.work = "Sinfon\xC3\xAD" "a n.\xC2\xBA 5 en mi menor";
        env.track("a3/" + std::to_string(i) + ".wav", t);
    }
    // A different symphony stays separate.
    TagSpec six = spec("Six", "Karajan", 1, "Tchaikovsky");
    six.work = "Symphony No. 6 in B minor, Op. 74 'Path\xC3\xA9tique'";
    env.track("a4/1.wav", six);

    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 2);
    // The Fifth: one work row, three recordings (albums), six tracks; the Sixth: one recording.
    {
        std::string stored;
        Statement ws = env.lib.db().prepare("SELECT title, replace(titles, char(10), ' | ') FROM works ORDER BY id");
        while (ws.step()) {
            stored += "\n  title='" + ws.getText(0) + "' titles=" + ws.getText(1);
        }
        INFO("stored works:" << stored);
        CHECK(env.one("SELECT COUNT(*) FROM works WHERE title LIKE '%No. 5 in E minor%' OR title LIKE 'Symphony No.5%'") == 1);
    }
    CHECK(env.one("SELECT MAX(n) FROM (SELECT COUNT(DISTINCT album_id) AS n FROM tracks WHERE work_id IS NOT NULL GROUP BY work_id)") == 3);
    CHECK(env.one("SELECT MIN(n) FROM (SELECT COUNT(DISTINCT album_id) AS n FROM tracks WHERE work_id IS NOT NULL GROUP BY work_id)") == 1);
    CHECK(env.one("SELECT COUNT(*) FROM tracks WHERE work_id IS NOT NULL") == 7);
    // The composer is linked once per spelling today (artists are not merged yet), but the work has one composer.
    CHECK(env.one("SELECT COUNT(*) FROM works WHERE composer_id IS NOT NULL") == 2);
}

TEST_CASE("works: a second person with the same surname re-groups existing works", "[works][scan][identity]") {
    Env env;
    TagSpec a = spec("Prelude", "Player", 1, "Bach");
    a.work = "Suite No. 1";
    env.track("a/1.wav", a);
    TagSpec b = spec("Prelude", "Player", 1, "Bach, Johann Sebastian");
    b.work = "Suite No. 1";
    env.track("b/1.wav", b);
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 1);  // "Bach" is the only Bach: same person

    // A second Bach appears: the bare "Bach" is now ambiguous and no longer the same person as J.S.
    TagSpec c = spec("Other", "Player", 1, "Bach, Carl Philipp Emanuel");
    c.work = "Suite No. 1";
    env.track("c/1.wav", c);
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 3);
}

TEST_CASE("works: one catalog number is one work, and number-only titles join it (D39)", "[works][scan][identity]") {
    Env env;
    // Edition 1 calls BWV 997 "Lute Suite No. 2", edition 2 a partita, edition 3 gives only the number.
    env.track("e1/1.wav", spec("Lute Suite No. 2 in A minor, BWV 997: I. Prelude", "Zigante", 1));
    env.track("e1/2.wav", spec("Lute Suite No. 2 in A minor, BWV 997: II. Fugue", "Zigante", 2));
    env.track("e2/1.wav", spec("Partita in C Minor for Lute, BWV 997 - Arr. for Guitar: I. Prelude", "Martelli", 1));
    env.track("e2/2.wav", spec("Partita in C Minor for Lute, BWV 997 - Arr. for Guitar: II. Fugue", "Martelli", 2));
    env.track("e3/1.wav", spec("Suite for Lute No. 2: I. Prelude", "Lislevand", 1));
    env.track("e3/2.wav", spec("Suite for Lute No. 2: II. Fugue", "Lislevand", 2));
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(DISTINCT work_id) FROM tracks WHERE work_id IS NOT NULL") == 1);
    CHECK(env.one("SELECT COUNT(*) FROM works") == 1);
    CHECK(env.one("SELECT COUNT(DISTINCT album_id) FROM tracks WHERE work_id IS NOT NULL") == 3);
    // Every title it was seen under is kept for the Works view's family vote.
    CHECK(env.one("SELECT instr(titles, 'Suite for Lute No. 2') > 0 AND instr(titles, 'Partita in C Minor') > 0 FROM works") == 1);
    // ...and the scan stores the family they vote for: two say "lute suite", one "partita".
    CHECK(env.one("SELECT COUNT(*) FROM works WHERE family_key = 'f:suite|lute|'") == 1);

    // Rescanning the number-only album alone keeps it in the catalogued work.
    env.track("e3/3.wav", spec("Suite for Lute No. 2: III. Sarabande", "Lislevand", 3));
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(DISTINCT work_id) FROM tracks WHERE work_id IS NOT NULL") == 1);
}

TEST_CASE("works: a number shared by two catalogued works joins neither", "[works][scan][identity]") {
    Env env;
    // Editions disagree on which suite is "No. 3": the number cannot be trusted.
    env.track("a/1.wav", spec("Lute Suite No. 3 in G minor, BWV 995: I. Prelude", "A", 1));
    env.track("b/1.wav", spec("Lute Suite No. 3 in E major, BWV 1006a: I. Prelude", "B", 1));
    env.track("c/1.wav", spec("Lute Suite No. 3: I. Prelude", "C", 1));
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 3);
}

TEST_CASE("works: a movement glued onto the catalog number is split off", "[works][scan]") {
    Env env;
    env.track("v/1.wav", spec("Sonata for Violin Solo No. 1 in G Minor, BWV 1001 I. Adagio", "V", 1));
    env.track("v/2.wav", spec("Sonata for Violin Solo No. 1 in G Minor, BWV 1001 II. Fuga", "V", 2));
    TagSpec tagged = spec("Adagio", "W", 1);
    tagged.work = "Sonata for Solo Violin No. 1 in G Minor, BWV 1001 I. Adagio";  // first title copied into WORK
    env.track("w/1.wav", tagged);
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 1);
    CHECK(env.one("SELECT COUNT(*) FROM works WHERE title LIKE '%Adagio%'") == 0);
    const auto rows = env.rows();
    CHECK(rows.at("Sonata for Violin Solo No. 1 in G Minor, BWV 1001 II. Fuga").movementNo == 2);
    CHECK(rows.at("Sonata for Violin Solo No. 1 in G Minor, BWV 1001 II. Fuga").movementName == "Fuga");
}

TEST_CASE("works: titled with and without a catalog number, among other numbered concertos (D40)", "[works][scan][identity]") {
    Env env;
    env.track("a/1.wav", spec("Brandenburg Concerto no. 1 in F major: I. Allegro", "Pinnock", 1));
    env.track("a/2.wav", spec("Brandenburg Concerto no. 1 in F major: II. Adagio", "Pinnock", 2));
    env.track("b/1.wav", spec("Brandenburg Concerto No. 1 in F major, BWV 1046: I. Allegro", "Savall", 1));
    env.track("b/2.wav", spec("Brandenburg Concerto No. 1 in F major, BWV 1046: II. Adagio", "Savall", 2));
    // Another "Concerto No. 1" of the same composer does not make the Brandenburg one ambiguous.
    env.track("c/1.wav", spec("Concerto No. 1 in D minor, BWV 1052: I. Allegro", "Pinnock", 1));
    env.track("c/2.wav", spec("Concerto No. 1 in D minor, BWV 1052: II. Adagio", "Pinnock", 2));
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 2);
    CHECK(env.one("SELECT COUNT(DISTINCT album_id) FROM tracks t JOIN works w ON w.id = t.work_id WHERE w.catalog_no = 'BWV 1046'") == 2);
    CHECK(env.one("SELECT COUNT(*) FROM works WHERE family_label = 'Brandenburg Concerto'") == 1);
}

TEST_CASE("works: the shown title is the most common wording, not the first scanned (D40)", "[works][scan]") {
    Env env;
    env.track("a/1.wav", spec("Suite, Bwv 1008: I. Prelude", "A", 1));
    env.track("a/2.wav", spec("Suite, Bwv 1008: II. Allemande", "A", 2));
    env.track("b/1.wav", spec("Cello Suite No. 2 in D minor, BWV 1008: I. Prelude", "B", 1));
    env.track("b/2.wav", spec("Cello Suite No. 2 in D minor, BWV 1008: II. Allemande", "B", 2));
    env.track("c/1.wav", spec("Cello Suite No. 2 in D minor, BWV 1008: I. Prelude", "C", 1));
    REQUIRE(env.scan().fatalError.empty());
    CHECK(env.one("SELECT COUNT(*) FROM works") == 1);
    CHECK(env.one("SELECT COUNT(*) FROM works WHERE title = 'Cello Suite No. 2 in D minor, BWV 1008' AND family_label = 'Cello Suite'") == 1);
}

TEST_CASE("works: a merge keeps every title, whatever the scan order (D40)", "[works][scan][identity]") {
    // Two catalog-less albums share one work before it joins the catalogued one. Which of their titles that
    // work got first depends on the order folders are listed (it differs between Windows and Linux), so run
    // both orders by swapping the tags between the two folders.
    const std::string english = "Symphony No.5 in E Minor";
    const std::string spanish = "Sinfon\xC3\xAD" "a n.\xC2\xBA 5 en mi menor";
    for (const bool spanishFirst : {false, true}) {
        Env env;
        TagSpec op = spec("Mov 1", "Karajan", 1, "Tchaikovsky, Pyotr Ilyich");
        op.work = "Symphony No. 5 in E minor, Op. 64";
        env.track("a1/1.wav", op);
        TagSpec en = spec("Mov 1", "Mravinsky", 1, "Pyotr Ilyich Tchaikovsky");
        en.work = english;
        TagSpec es = spec("Mov 1", "Abbado", 1, "Tchaikovsky");
        es.work = spanish;
        env.track("a2/1.wav", spanishFirst ? es : en);
        env.track("a3/1.wav", spanishFirst ? en : es);
        REQUIRE(env.scan().fatalError.empty());
        INFO("spanishFirst = " << spanishFirst);
        CHECK(env.one("SELECT COUNT(*) FROM works") == 1);
        CHECK(env.one("SELECT COUNT(*) FROM works WHERE instr(titles, '" + english + "') > 0 AND instr(titles, '" + spanish + "') > 0") == 1);
        CHECK(env.one("SELECT COUNT(*) FROM works WHERE title = '" + english + "'") == 1);  // the shortest of the three
    }
}
