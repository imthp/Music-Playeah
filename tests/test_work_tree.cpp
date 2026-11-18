#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/library/work_identity.hpp"
#include "core/library/work_tree.hpp"

using namespace mp::core::library;

namespace {

std::string label(const char* title) { return workFamily(title).label; }
std::string key(const char* title) { return workFamily(title).key; }

WorkEntry work(std::int64_t id, std::string title, std::string composer, std::string composerKey, std::string catalog = {}) {
    WorkEntry w;
    w.id = id;
    w.title = std::move(title);
    w.composer = std::move(composer);
    w.catalog = std::move(catalog);
    w.workKey = composerKey + '\x1F' + "x" + std::to_string(id);
    w.recordings = 1;
    w.tracks = 3;
    return w;
}

}  // namespace

TEST_CASE("A family name is the title without number, key and catalog number", "[works][family]") {
    CHECK(label("Brandenburg Concerto No. 1 in F major, BWV 1046") == "Brandenburg Concerto");
    CHECK(label("Brandenburg Concerto No.5") == "Brandenburg Concerto");
    CHECK(label("Cello Suite No. 1 in G major, BWV 1007") == "Cello Suite");
    CHECK(label("Suite for Cello No. 3") == "Suite for Cello");
    CHECK(label("Lute Suite in E minor, BWV 996") == "Lute Suite");
    CHECK(label("Symphony No. 5 in E minor, Op. 64") == "Symphony");
    CHECK(label("Symphony #5") == "Symphony");
    CHECK(label("5. Sinfonie e-Moll") == "Sinfonie");
    CHECK(label("5th Symphony") == "Symphony");
    CHECK(label("String Quartet Op. 18 No. 1") == "String Quartet");
    CHECK(label("Piano Sonata D. 960") == "Piano Sonata");
    CHECK(label("Piano Sonata Hob. XVI:52") == "Piano Sonata");
    CHECK(label("Sinfonía n.º 5 en mi menor") == "Sinfonía");
    CHECK(label("Mass in B minor") == "Mass");
    CHECK(label("Goldberg Variations, BWV 988") == "Goldberg Variations");
    CHECK(label("Concerto for 2 Violins in D minor") == "Concerto");
    CHECK(label("Violin Concerto - Allegro") == "Violin Concerto");
    CHECK(label("BWV 1007").empty());
    CHECK(key("BWV 1007").empty());
}

TEST_CASE("Family keys ignore spelling, language and word order", "[works][family]") {
    CHECK(key("Cello Suite No. 1") == key("Suite for Cello No. 3"));
    CHECK(key("Cello Suite No. 1") == key("Suite pour violoncelle seul n° 2"));
    CHECK(key("Symphony No. 5") == key("5. Sinfonie e-Moll"));
    CHECK(key("Piano Concerto No. 1") == key("Concerto for Piano and Orchestra No. 2"));
    CHECK(key("Klavierkonzert Nr. 2") == key("Piano Concerto No. 1"));
    CHECK(key("Brandenburg Concerto No. 1") == key("Brandenburg Concertos"));
    CHECK(key("Brandenburg Concerto No. 1") != key("Violin Concerto No. 1"));
    CHECK(key("Brandenburg Concerto No. 1") != key("Concerto No. 1"));
    CHECK(key("Cello Suite No. 1") != key("Lute Suite No. 1"));
    CHECK(key("Orchestral Suite No. 3") != key("Suite No. 3"));
    CHECK(key("Goldberg Variations") == key("Goldberg variations, BWV 988"));
}

TEST_CASE("Works are grouped by composer, then by family", "[works][family]") {
    std::vector<WorkEntry> works = {
        work(1, "Brandenburg Concerto No. 3 in G major", "J.S. Bach", "bach|js", "BWV 1048"),
        work(2, "Cello Suite No. 1 in G major", "Bach, Johann Sebastian", "bach|js", "BWV 1007"),
        work(3, "Brandenburg Concerto No. 1 in F major", "Bach, Johann Sebastian", "bach|js", "BWV 1046"),
        work(4, "Lute Suite in E minor", "Bach", "bach|js", "BWV 996"),
        work(5, "Lute Suite in G minor", "Bach", "bach|js", "BWV 995"),
        work(6, "Suite for Cello No. 2", "J.S. Bach", "bach|js", "BWV 1008"),
        work(7, "Goldberg Variations", "J.S. Bach", "bach|js", "BWV 988"),
        work(8, "Symphony No. 5 in E minor", "Tchaikovsky, Pyotr Ilyich", "chaikovski|pi", "Op. 64"),
        work(9, "Untitled", "", "someone"),
        work(10, "Flute Sonata in E minor", "Bach, Carl Philipp Emanuel", "bach|cpe"),
    };
    const auto tree = groupWorksByComposer(works);
    REQUIRE(tree.size() == 4);
    CHECK(tree[0].name == "Bach, Carl Philipp Emanuel");
    CHECK(tree[1].name == "Bach, Johann Sebastian");  // the most complete spelling
    CHECK(tree[2].name == "Tchaikovsky, Pyotr Ilyich");
    CHECK(tree[3].name.empty());                      // no composer: last

    const ComposerNode& js = tree[1];
    CHECK(js.workCount == 7);
    REQUIRE(js.families.size() == 4);
    CHECK(js.families[0].label == "Brandenburg Concerto");
    REQUIRE(js.families[0].works.size() == 2);
    CHECK(js.families[0].works[0].id == 3);  // No. 1 before No. 3
    CHECK(js.families[0].works[1].id == 1);
    CHECK(js.families[1].label == "Cello Suite");  // "Suite for Cello" joins it
    CHECK(js.families[1].works.size() == 2);
    CHECK(js.families[2].label.empty());           // a single work stands alone
    CHECK(js.families[2].works.front().title == "Goldberg Variations");
    CHECK(js.families[3].label == "Lute Suite");
    REQUIRE(js.families[3].works.size() == 2);
    CHECK(js.families[3].works[0].id == 5);  // unnumbered: catalog order, BWV 995 before 996

    CHECK(tree[2].families.size() == 1);
    CHECK(tree[2].families[0].label.empty());
}

TEST_CASE("Families: accents, instruments after the key, arrangements (D39)", "[works][family]") {
    CHECK(label("Concerto pour clavecin en R\xC3\xA9 mineur, BWV 1052") == "Concerto pour clavecin");
    CHECK(key("Concerto pour clavecin en R\xC3\xA9 mineur, BWV 1052") == key("Harpsichord Concerto No. 1 in D minor, BWV 1052"));
    CHECK(key("Concerto pour clavecin en fa mineur, BWV 1056") == key("Harpsichord Concerto No. 5"));
    CHECK(key("Partita in D Minor for Solo Violin, BWV 1004 - Arr. for Guitar") == key("Partita for Violin Solo No. 2"));
    CHECK(key("Partita No.1 in B flat, BWV 825 - Arr. Guitar (D major)") != key("Partita for Violin Solo No. 2"));
    CHECK(key("Sonata for Solo Violin No. 2") == key("Sonata for Violin Solo No. 2"));
    CHECK(key("Lute Suite No. 1") == key("Suite for Lute No. 3"));
    CHECK(key("Lute Suite No. 1") != key("Cello Suite No. 1"));
}

TEST_CASE("Catalog ranges beside a family name", "[works][family]") {
    CHECK(catalogRange({"BWV 1007", "BWV 1008", "BWV 1009", "BWV 1010", "BWV 1011", "BWV 1012"}) == "BWV 1007\xE2\x80\x93" "1012");
    CHECK(catalogRange({"BWV 996", "BWV 995", "BWV 997", "BWV 1006a"}) == "BWV 995\xE2\x80\x93" "997, 1006a");
    CHECK(catalogRange({"BWV 1046", "BWV 1048"}) == "BWV 1046, 1048");
    CHECK(catalogRange({"BWV 1001", "BWV 1002"}) == "BWV 1001, 1002");
    CHECK(catalogRange({"Op. 64", "", "Op. 74"}) == "Op. 64, 74");
    CHECK(catalogRange({"BWV 1052", "BWV 1052", "BWV 1053"}) == "BWV 1052, 1053");
    CHECK(catalogRange({}).empty());
    CHECK(catalogRange({"K. 525", "BWV 1"}) == "K. 525; BWV 1");
}

TEST_CASE("A work goes to the family most of its titles name", "[works][family]") {
    WorkEntry w = work(1, "Partita in C Minor for Lute, BWV 997", "Bach, Johann Sebastian", "bach|js", "BWV 997");
    w.titles = {"Partita in C Minor for Lute, BWV 997", "Lute Suite No. 2 in A minor, BWV 997", "Suite for Lute No. 2"};
    std::vector<WorkEntry> works = {w, work(2, "Lute Suite No. 1 in E minor", "Bach, Johann Sebastian", "bach|js", "BWV 996")};
    const auto tree = groupWorksByComposer(works);
    REQUIRE(tree.size() == 1);
    REQUIRE(tree[0].families.size() == 1);
    CHECK(tree[0].families[0].label == "Lute Suite");
    CHECK(tree[0].families[0].works.size() == 2);
    CHECK(tree[0].families[0].catalogRange == "BWV 996, 997");
}

TEST_CASE("Families from a real library (D40)", "[works][family]") {
    CHECK(key("Deuxi\xC3\xA8me concert Brandebourgois en fa majeur, BWV 1047") == key("Brandenburg Concerto No. 2"));
    CHECK(label("Deuxi\xC3\xA8me concert Brandebourgois en fa majeur, BWV 1047") == "concert Brandebourgois");
    CHECK(key("Concerto \xE2\x80\x9CLa Notte\xE2\x80\x9D for Bassoon, Strings & Continuo in B\xE2\x99\xAD, RV 501") == key("Bassoon Concerto in C"));
    CHECK(key("Concerto \xE2\x80\x9CL\xE2\x80\x99" "Autunno\xE2\x80\x9D for Violin, Strings & Continuo in F, RV 293") == key("Violin Concerto in A major, RV 344"));
    CHECK(label("Prelude, Fugue and Allegro in E-Flat Major, BWV 998") == "Prelude, Fugue and Allegro");
    CHECK(key("Prelude, Fugue and Allegro in E-Flat Major, BWV 998") != key("Prelude in C minor, BWV 999"));
    CHECK(key("Concerto pour deux clavecins en ut majeur, BWV 1061") == key("Concerto pour clavecin en fa mineur, BWV 1056"));
    CHECK(label("Bach autrement II (Inspired by Prelude, BWV 1007)") == "Bach autrement");
    CHECK(key("Partita II (d'apr\xC3\xA8s BWV 1004 pour violon solo)") == key("Violin Partita No. 2"));
}

TEST_CASE("A work's family, label and title come from all its titles (D40)", "[works][family]") {
    // "Suite" alone counts for the more specific "Cello Suite"
    WorkFamilyVote v = familyOfWork("Suite, Bwv 1008", {"Suite, Bwv 1008", "Cello Suite No. 2 in D minor, BWV 1008"});
    CHECK(v.key == key("Cello Suite No. 2"));
    CHECK(v.label == "Cello Suite");
    CHECK(v.title == "Cello Suite No. 2 in D minor, BWV 1008");
    // Titles without a form do not outvote ones with a form
    v = familyOfWork("Bach autrement II", {"Cello Suite No. 1 in G major, BWV 1007"});
    CHECK(v.key == key("Cello Suite No. 1"));
    // No title names the instrument in its first words: the label is built from the key
    v = familyOfWork("Partita in D Minor for Solo Violin, BWV 1004 - Arr. for Guitar", {});
    CHECK(v.label == "Violin Partita");
    // The shown title: the most common wording, case-insensitive, not an arrangement
    v = familyOfWork("Partita in D Minor for Solo Violin, BWV 1004 - Arr. for Guitar",
                     {"Violin Partita No. 2 in D minor, BWV 1004", "Violin Partita No. 2 in D Minor, BWV 1004",
                      "Violin Partita No. 2 in D minor, BWV 1004 (arr. for guitar)"});
    CHECK(v.title == "Violin Partita No. 2 in D minor, BWV 1004");
    // A tie goes to the family the composer already has more works in
    const std::vector<std::string> both = {"Partita in C Minor for Lute, BWV 997", "Lute Suite No. 2 in A minor, BWV 997"};
    v = familyOfWork(both[0], both);
    CHECK(v.tied);
    CHECK(v.key == key("Partita for Lute"));  // without context: the work's own title
    v = familyOfWork(both[0], both, [](const std::string& k) { return k == key("Lute Suite No. 1") ? 3 : 0; });
    CHECK(v.key == key("Lute Suite No. 1"));
    CHECK(catalogRange({"Op. 8 No. 1", "Op. 8 No. 2", "Op. 8 No. 3", "Op. 8 No. 4"}) == "Op. 8 No. 1\xE2\x80\x93" "4");
}
