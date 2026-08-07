// Part D: native v3 round-trips text + dimensions + dimstyles losslessly; older
// files still load; DXF carries text + linear dimensions + the DIMSTYLE table.

#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
GeometryStore make_annotated_store() {
    GeometryStore s;
    DimStyle ds;
    ds.name = "Arch";
    ds.text_height = 3.5;
    ds.precision = 1;
    ds.arrow_type = static_cast<std::uint8_t>(ArrowType::Tick);
    ds.dim_lineweight = 50;
    ds.arrow_color = {false, Rgb{255, 0, 0}};  // explicit red arrowheads
    ds.text_color = {false, Rgb{255, 255, 0}}; // explicit yellow text
    const std::uint16_t arch = s.add_dimstyle(ds);

    s.add_text({1, 2}, 5.0, 0.5, 1, "Hello World 123", {});
    s.add_line({0, 0}, {10, 0}, {});
    s.add_dimension(DimType::Linear, {0, 0}, {10, 0}, {5, 4}, 0, {});
    s.add_dimension(DimType::Aligned, {0, 0}, {3, 4}, {-1, 1}, arch, {});
    s.add_dimension(DimType::Radius, {0, 0}, {8, 0}, {8, 0}, 0, {});
    s.add_dimension(DimType::Angular, {0, 0}, {10, 0}, {0, 10}, 0, {});
    s.add_leader({0, 0}, {12, 8}, 2.5, arch, "Note here", {});
    return s;
}
} // namespace

TEST_CASE("Native v4 round-trips text + dims + leaders + expanded dimstyles losslessly") {
    const Document a = document_from_store(make_annotated_store());
    REQUIRE(a.texts.size() == 1);
    REQUIRE(a.dims.size() == 4);
    REQUIRE(a.leaders.size() == 1);
    REQUIRE(a.dimstyles.size() == 2); // Standard + Arch

    const auto path = (std::filesystem::temp_directory_path() / "musacad_annot.musa").string();
    REQUIRE(save_native(a, path).ok);
    Document b;
    REQUIRE(load_native(path, b).ok);
    GeometryStore restored;
    populate_store(restored, b);
    // Exact: text content, all dim types, leader, per-element dimstyle colours.
    REQUIRE(document_from_store(restored) == a);
    std::filesystem::remove(path);
}

TEST_CASE("v3 files (no leaders / old dimstyle) still load") {
    // A v3 file with the old 7-field DIMSTYLE record and no LEADER lines.
    const std::string v3 =
        "MUSACAD 3\nLAYER 255 255 255 0 25 1 0 0 0\nDIMSTYLE 2.5 2.5 0 0.6 1.25 2 1 Standard\n"
        "DIM 0 0 0 10 0 5 3 0 0 7 0 0 0 0 25\nEND\n";
    Document doc;
    REQUIRE(parse_native(v3, doc).ok);
    REQUIRE(doc.dims.size() == 1);
    REQUIRE(doc.leaders.empty());
    REQUIRE(doc.dimstyles.size() == 1);
    REQUIRE(doc.dimstyles[0].name == "Standard");
}

TEST_CASE("Text content with spaces survives the native round-trip") {
    Document a;
    a.texts.push_back(DocText{{0, 0}, 2.5, 0.0, 0, "two  spaces and tabs\there", {}});
    Document b;
    REQUIRE(parse_native(serialize_native(a), b).ok);
    REQUIRE(b.texts.size() == 1);
    REQUIRE(b.texts[0].content == "two  spaces and tabs\there");
}

TEST_CASE("v2 files (no annotations) still load") {
    const std::string v2 = "MUSACAD 2\nLAYER 255 0 0 0 25 1 0 0 red\nLINE 0 0 1 1 1 7 0 0 0 0 25\nEND\n";
    Document doc;
    REQUIRE(parse_native(v2, doc).ok);
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.texts.empty());
    REQUIRE(doc.dims.empty());
    REQUIRE(doc.dimstyles.size() == 1); // defaulted Standard
}

TEST_CASE("DXF carries text + all dimension types + leader + a DIMSTYLE table") {
    const Document a = document_from_store(make_annotated_store());
    const std::string dxf = serialize_dxf(a);
    REQUIRE(dxf.find("\nTEXT\n") != std::string::npos);
    REQUIRE(dxf.find("\nDIMENSION\n") != std::string::npos);
    REQUIRE(dxf.find("\nDIMSTYLE\n") != std::string::npos);
    REQUIRE(dxf.find("\nLEADER\n") != std::string::npos);

    Document b;
    REQUIRE(parse_dxf(dxf, b).ok);
    REQUIRE(b.dims.size() == 4);
    REQUIRE(b.leaders.size() == 1); // leader survives as a leader (label as text)
    // All four dim types survive the DXF round-trip.
    bool lin = false;
    bool ali = false;
    bool rad = false;
    bool ang = false;
    for (const DocDim& d : b.dims) {
        lin = lin || d.type == static_cast<std::uint8_t>(DimType::Linear);
        ali = ali || d.type == static_cast<std::uint8_t>(DimType::Aligned);
        rad = rad || d.type == static_cast<std::uint8_t>(DimType::Radius);
        ang = ang || d.type == static_cast<std::uint8_t>(DimType::Angular);
    }
    REQUIRE(lin);
    REQUIRE(ali);
    REQUIRE(rad);
    REQUIRE(ang);
    // The original text plus the leader's label both come through as text.
    bool found_hello = false;
    for (const DocText& t : b.texts) {
        found_hello = found_hello || t.content == "Hello World 123";
    }
    REQUIRE(found_hello);
    bool has_arch = false;
    for (const DimStyle& ds : b.dimstyles) {
        has_arch = has_arch || ds.name == "Arch";
    }
    REQUIRE(has_arch);
}

// ---------------------------------------------------------------------------
// Native v15: dimension text decoration (issue #7).
// ---------------------------------------------------------------------------

TEST_CASE("v15 round-trips dimension decoration losslessly (store -> doc -> file -> doc -> store)") {
    GeometryStore s;
    // One of every tolerance mode, plus prefixes/suffixes with spaces and raw %%-codes.
    s.add_dimension(DimType::Linear, {0, 0}, {50, 0}, {25, 10}, 0, {}, {}, "6X ", " TYP", {});
    s.add_dimension(DimType::Linear, {0, 0}, {50, 0}, {25, 20}, 0, {}, {}, "%%c", " H7",
                    DimTolerance{TolMode::Symmetric, 0.1, -0.1});
    s.add_dimension(DimType::Aligned, {0, 0}, {30, 40}, {10, 30}, 0, {}, {}, "", "",
                    DimTolerance{TolMode::Limits, 0.046, 0.0});
    s.add_dimension(DimType::Linear, {0, 0}, {12, 0}, {6, 5}, 0, {}, {}, "", "",
                    DimTolerance{TolMode::Basic, 0.0, 0.0});
    s.add_dimension(DimType::Linear, {0, 0}, {12, 0}, {6, 8}, 0, {}, {}, "", "",
                    DimTolerance{TolMode::Reference, 0.0, 0.0});

    const Document a = document_from_store(s);
    REQUIRE(a.format_version == kFormatVersion);
    REQUIRE(a.dims.size() == 5);

    const auto path = (std::filesystem::temp_directory_path() / "musacad_dimdecor.musa").string();
    REQUIRE(save_native(a, path).ok);
    Document b;
    REQUIRE(load_native(path, b).ok);
    GeometryStore restored;
    populate_store(restored, b);
    REQUIRE(document_from_store(restored) == a); // value-for-value, decoration included

    // Spot-check the fields rather than trusting equality alone.
    REQUIRE(b.dims[0].prefix == "6X ");
    REQUIRE(b.dims[0].suffix == " TYP");
    REQUIRE(b.dims[1].prefix == "%%c"); // stored RAW; codes expand at render time
    REQUIRE(b.dims[2].tol.mode == TolMode::Limits);
    REQUIRE(b.dims[2].tol.upper == 0.046);
    REQUIRE(b.dims[3].tol.mode == TolMode::Basic);
    REQUIRE(b.dims[4].tol.mode == TolMode::Reference);
    std::filesystem::remove(path);
}

TEST_CASE("v14 dimensions still load, as undecorated (older-version compatibility)") {
    // A real v14 DIM line: 16 base tokens + the 15-field v8 override block, no
    // decoration fields and no prefix/suffix lines after it.
    const std::string v14 =
        "MUSACAD 14\nLAYER 255 255 255 0 25 1 0 0 0\n"
        "DIMSTYLE 2.5 2.5 0 0.6 1.25 2 1 25 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 Standard\n"
        "DIM 0 0 0 10 0 5 3 0 0 7 255 255 255 0 25 1 0 2 1 3.5 2.5 0 0 0 0 0 0 0 0 0\n"
        "LINE 0 0 1 1 0 7 255 255 255 0 25\nEND\n";
    Document doc;
    REQUIRE(parse_native(v14, doc).ok);
    REQUIRE(doc.dims.size() == 1);
    REQUIRE(doc.dims[0].tol.mode == TolMode::None);
    REQUIRE(doc.dims[0].prefix.empty());
    REQUIRE(doc.dims[0].suffix.empty());
    REQUIRE(doc.dims[0].overrides.has(DimOverrides::kTextHeight)); // the v8 block still parsed
    REQUIRE(doc.dims[0].overrides.text_height == 3.5);
    // The record AFTER the dimension must still be found -- i.e. the reader did not
    // swallow the following line looking for a prefix.
    REQUIRE(doc.lines.size() == 1);
}

// ---------------------------------------------------------------------------
// Native v16: the shared override block gains text_fit (issue #12).
// ---------------------------------------------------------------------------

TEST_CASE("v16 round-trips text_fit on dimensions, leaders and the dimstyle") {
    GeometryStore s;
    // A style-level default plus a per-dimension override, so both halves of the
    // override-first-else-style resolution are exercised through the file.
    DimStyle ds;
    ds.name = "Narrow";
    ds.text_fit = static_cast<std::uint8_t>(TextFit::Outside);
    s.add_dimstyle(ds);

    DimOverrides ov;
    ov.set(DimOverrides::kTextFit, true);
    ov.text_fit = static_cast<std::uint8_t>(TextFit::Inside);
    s.add_dimension(DimType::Linear, {0, 0}, {2, 0}, {1, 5}, 0, {}, ov, "", "", {});
    // The block is SHARED, so a leader must round-trip the wider form too even though
    // text_fit is meaningless for it -- that is the cost of one definition, and it has
    // to actually work.
    s.add_leader({0, 0}, {5, 5}, 2.5, 0, "note", {}, 0, ov);

    const Document a = document_from_store(s);
    REQUIRE(a.format_version == kFormatVersion);
    const auto path = (std::filesystem::temp_directory_path() / "musacad_textfit.musa").string();
    REQUIRE(save_native(a, path).ok);
    Document b;
    REQUIRE(load_native(path, b).ok);
    GeometryStore restored;
    populate_store(restored, b);
    REQUIRE(document_from_store(restored) == a);

    REQUIRE(b.dims[0].overrides.has(DimOverrides::kTextFit));
    REQUIRE(b.dims[0].overrides.text_fit == static_cast<std::uint8_t>(TextFit::Inside));
    REQUIRE(b.leaders[0].overrides.has(DimOverrides::kTextFit));
    // The dimstyle's own default survives, including the multi-word-name boundary the
    // writer has to protect (text_fit precedes the name).
    bool found = false;
    for (const DimStyle& x : b.dimstyles) {
        if (x.name == "Narrow") {
            REQUIRE(x.text_fit == static_cast<std::uint8_t>(TextFit::Outside));
            found = true;
        }
    }
    REQUIRE(found);
    std::filesystem::remove(path);
}

TEST_CASE("A dimstyle name with spaces survives the v16 text_fit field") {
    // text_fit had to go BEFORE the name because the name absorbs every remaining
    // token. Prove the boundary holds for a multi-word name.
    GeometryStore s;
    DimStyle ds;
    ds.name = "ISO 25 Narrow Text";
    ds.text_fit = static_cast<std::uint8_t>(TextFit::Inside);
    s.add_dimstyle(ds);
    const Document a = document_from_store(s);
    Document b;
    REQUIRE(parse_native(serialize_native(a), b).ok);
    bool found = false;
    for (const DimStyle& x : b.dimstyles) {
        if (x.name == "ISO 25 Narrow Text") {
            REQUIRE(x.text_fit == static_cast<std::uint8_t>(TextFit::Inside));
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("v15 files load with text_fit = Auto, so old drawings simply stop colliding") {
    // A real v15 record set: DIM with the 15-field override block + the 3 decoration
    // fields (34 tokens) and its prefix/suffix lines; LEADER with the 15-field block
    // (29 tokens); DIMSTYLE with the name straight after the four element colours.
    const std::string v15 =
        "MUSACAD 15\nLAYER 255 255 255 0 25 1 0 0 0\n"
        // Index 0 is force-named "Standard" after the parse (a long-standing invariant),
        // so the style whose name boundary we are testing has to be the SECOND one --
        // exactly as a real file has it.
        "DIMSTYLE 2.5 2.5 0 0.6 1.25 2 1 25 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 Standard\n"
        "DIMSTYLE 2.5 2.5 0 0.6 1.25 2 1 25 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 ISO 25 Narrow\n"
        "DIM 0 0 0 10 0 5 3 0 0 7 255 255 255 0 25 1 0 2 1 3.5 2.5 0 0 0 0 0 0 0 0 0 0 0 0\n"
        "6X \n TYP\n"
        "LEADER 0 0 5 5 2.5 0 0 7 255 255 255 0 25 2 0 2 1 2.5 4.0 0 0 0 0 0 0 0 0 0\n"
        "note\n\n"
        "LINE 0 0 1 1 0 7 255 255 255 0 25\nEND\n";
    Document doc;
    REQUIRE(parse_native(v15, doc).ok);
    REQUIRE(doc.dims.size() == 1);
    REQUIRE_FALSE(doc.dims[0].overrides.has(DimOverrides::kTextFit)); // ByStyle
    REQUIRE(doc.dims[0].overrides.text_height == 3.5);                // the v8 fields still parse
    REQUIRE(doc.dims[0].prefix == "6X ");                             // the v15 fields still parse
    REQUIRE(doc.dims[0].suffix == " TYP");
    REQUIRE(doc.leaders.size() == 1);
    REQUIRE(doc.leaders[0].overrides.arrow_size == 4.0);
    // The style default is Auto, and its multi-word name did NOT swallow a text_fit
    // token that a v15 file does not have.
    REQUIRE(doc.dimstyles.size() == 2);
    REQUIRE(doc.dimstyles[1].name == "ISO 25 Narrow");
    REQUIRE(doc.dimstyles[1].text_fit == static_cast<std::uint8_t>(TextFit::Auto));
    // The record AFTER everything is still found: no line was swallowed.
    REQUIRE(doc.lines.size() == 1);
}

TEST_CASE("DXF writes the decorated label into the DIMENSION text override (code 1)") {
    Document a;
    a.layers.push_back(Layer{"0", {255, 255, 255}, Linetype::Continuous, 25, true, false, false});
    a.dimstyles.push_back(DimStyle{});
    a.dimstyles[0].name = "Standard";
    a.dimstyles[0].precision = 2;
    DocDim d;
    d.type = static_cast<std::uint8_t>(DimType::Linear);
    d.a = {0, 0};
    d.b = {50, 0};
    d.line_pt = {25, 10};
    d.prefix = "%%c";
    d.suffix = " H7";
    a.dims.push_back(d);

    const std::string dxf = serialize_dxf(a);
    // The composed, code-EXPANDED string, so another CAD shows what Musa CAD draws.
    REQUIRE(dxf.find("\n1\n\xE2\x8C\x80" "50.00 H7\n") != std::string::npos);

    // An undecorated dimension writes NO override, so a reader measures it itself --
    // the correct default, and what every pre-#7 file already relied on. (Checked by
    // the absence of the measured text: "\n1\n" alone also matches $ACADVER's value.)
    Document plain = a;
    plain.dims[0].prefix.clear();
    plain.dims[0].suffix.clear();
    const std::string dxf2 = serialize_dxf(plain);
    REQUIRE(dxf2.find("50.00") == std::string::npos);
}
