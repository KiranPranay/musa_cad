// AutoCAD text control-code substitution (render-time, derived-not-baked): the ONE
// function shared by single-line TEXT, paragraph MTEXT, and the Leader/MLeader labels.

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/text/text_codes.hpp"

using namespace musacad::core::text;

namespace {
const std::string kDeg = "\xC2\xB0";       // U+00B0 degree
const std::string kPm = "\xC2\xB1";        // U+00B1 plus-minus
const std::string kDia = "\xE2\x8C\x80";   // U+2300 diameter
} // namespace

TEST_CASE("substitute_text_codes expands the engineering symbol codes") {
    CHECK(substitute_text("90%%d") == "90" + kDeg);
    CHECK(substitute_text("%%p0.5") == kPm + "0.5");
    CHECK(substitute_text("%%c50") == kDia + "50");
    // A full engineering callout, the headline case.
    CHECK(substitute_text("%%c50 H7 %%p0.02") == kDia + "50 H7 " + kPm + "0.02");
}

TEST_CASE("substitute_text_codes: literal percent and ASCII-by-code") {
    CHECK(substitute_text("100%%%") == "100%");      // %%% -> literal %
    CHECK(substitute_text("ab%%37cd") == "ab%cd");   // %%37 -> '%' (decimal 37)
    CHECK(substitute_text("%%176") == kDeg);         // %%176 -> Latin-1 0xB0 = degree
    CHECK(substitute_text("50% off") == "50% off");  // a lone % is untouched
    // Out-of-range / zero codes stay literal -- never a wrapped glyph or an embedded NUL.
    CHECK(substitute_text("%%300") == "%%300"); // 300 > 255
    CHECK(substitute_text("%%256") == "%%256"); // would be &0xFF == 0 (NUL)
    CHECK(substitute_text("%%0") == "%%0");     // explicit zero
    CHECK(substitute_text("x%%300y").find('\0') == std::string::npos); // no NUL byte emitted
}

TEST_CASE("substitute_text_codes is case-insensitive on the code letter") {
    CHECK(substitute_text("90%%D") == "90" + kDeg);
    CHECK(substitute_text("%%C50") == kDia + "50");
}

TEST_CASE("substitute_text_codes records overline / underline runs and removes the toggles") {
    const SubstitutedText o = substitute_text_codes("Pipe %%oOD%%o end");
    CHECK(o.text == "Pipe OD end");
    REQUIRE(o.overline.size() == 1);
    CHECK(o.text.substr(o.overline[0].begin, o.overline[0].end - o.overline[0].begin) == "OD");
    CHECK(o.underline.empty());

    const SubstitutedText u = substitute_text_codes("x%%u123%%uy");
    CHECK(u.text == "x123y");
    REQUIRE(u.underline.size() == 1);
    CHECK(u.text.substr(u.underline[0].begin, u.underline[0].end - u.underline[0].begin) == "123");

    // An unterminated toggle decorates to the end of the string.
    const SubstitutedText open = substitute_text_codes("a%%ob");
    REQUIRE(open.overline.size() == 1);
    CHECK(open.text == "ab");
    CHECK(open.overline[0].end == open.text.size());
}

TEST_CASE("substitute_text_codes: \\U+XXXX Unicode escape is universal") {
    // Deliberately widened from MTEXT-only: the stroke font carries far more symbols
    // than there are free %%-letters, so \U+ is THE general escape and must reach
    // single-line TEXT, leaders and dimension text too -- otherwise a GD&T symbol
    // would be usable in a paragraph but not in a callout.
    CHECK(substitute_text("\\U+2300 50") == kDia + " 50");
    CHECK(substitute_text("\\U+00B1") == kPm);
    CHECK(substitute_text("\\U+2300") == kDia); // was left literal before; now expands
    // A malformed escape (too few hex digits) is still left literal.
    CHECK(substitute_text("\\U+12") == "\\U+12");
    // A backslash that is not an escape is untouched, so paths survive.
    CHECK(substitute_text("C:\\Users\\parts") == "C:\\Users\\parts");
}

TEST_CASE("substitute_text_codes expands the hole-callout aliases") {
    const std::string kCbore = "\xE2\x8C\xB4"; // U+2334 counterbore / spotface
    const std::string kDepth = "\xE2\x86\xA7"; // U+21A7 depth
    const std::string kCsink = "\xE2\x8C\xB5"; // U+2335 countersink
    CHECK(substitute_text("%%b%%c15") == kCbore + kDia + "15");
    CHECK(substitute_text("%%h8.6") == kDepth + "8.6");
    CHECK(substitute_text("%%v%%c20") == kCsink + kDia + "20");
    CHECK(substitute_text("%%B %%H %%V") == kCbore + " " + kDepth + " " + kCsink); // case-insensitive

    // The new letters must not have shadowed an existing code, and the numeric form
    // must still win for digits.
    CHECK(substitute_text("%%d%%p%%c%%%") == kDeg + kPm + kDia + "%");
    CHECK(substitute_text("%%176") == kDeg);

    // The full three-line counterbore callout from the issue, as one string.
    CHECK(substitute_text("6X %%c9 THRU / %%b%%c15 / %%h8.6") ==
          "6X " + kDia + "9 THRU / " + kCbore + kDia + "15 / " + kDepth + "8.6");
}

TEST_CASE("substitute_text_codes leaves plain text untouched (no allocation surprises)") {
    CHECK(substitute_text("plain ASCII text, no codes.") == "plain ASCII text, no codes.");
    const SubstitutedText s = substitute_text_codes("nothing here");
    CHECK_FALSE(s.has_decor());
}
