#include <catch2/catch_test_macros.hpp>

#include "core/text.hpp"

using namespace mp::core;

TEST_CASE("normalizeText folds case, spaces and common accents", "[text]") {
    CHECK(normalizeText("  The   BEATLES ") == "the beatles");
    CHECK(normalizeText("Björk") == normalizeText("BJÖRK"));
    CHECK(normalizeText("Dvořák") == normalizeText("DVOŘÁK"));
    CHECK(normalizeText("Łódź") == normalizeText("ŁÓDŹ"));
    CHECK(normalizeText("Чайковский") == normalizeText("ЧАЙКОВСКИЙ"));
    CHECK(normalizeText("ΜΟΥΣΙΚΗ") == normalizeText("μουσικη"));
    CHECK(normalizeText("a\tb\nc") == "a b c");
    CHECK(normalizeText("a\xC2\xA0 b") == "a b");  // no-break space
    CHECK(normalizeText("   ").empty());
    CHECK(normalizeText("").empty());
}

TEST_CASE("normalizeText keeps accents and leaves odd bytes alone", "[text]") {
    CHECK(normalizeText("Beyoncé") != normalizeText("Beyonce"));
    CHECK(normalizeText("×") == "×");  // multiplication sign is not a capital letter
    const std::string broken = "ab\xFF" "cd";
    CHECK(normalizeText(broken) == broken);  // invalid UTF-8 survives unchanged
}

TEST_CASE("sortName drops a leading article", "[text]") {
    CHECK(sortName("The Beatles") == "beatles");
    CHECK(sortName("the who") == "who");
    CHECK(sortName("Theodore") == "theodore");
    CHECK(sortName("The") == "the");
}

TEST_CASE("trimText trims ASCII and no-break spaces", "[text]") {
    CHECK(trimText("  a b \t") == "a b");
    CHECK(trimText("\xC2\xA0x\xC2\xA0") == "x");
    CHECK(trimText("").empty());
}

TEST_CASE("paths convert to UTF-8 and back", "[text]") {
    const std::string name = "Bj\xC3\xB6rk/J\xC3\xB3ga.flac";
    const auto path = pathFromUtf8(name);
    CHECK(pathToUtf8(path) == genericUtf8(path));
    CHECK(genericUtf8(pathFromUtf8("a/b/c.flac")) == "a/b/c.flac");  // (on Windows "a\\b" becomes "a/b" too)
    CHECK(pathToUtf8(pathFromUtf8(name)) == name);
}
