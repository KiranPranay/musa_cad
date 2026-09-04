// Dimension text override (#20) and text position (#21).
//
// Both keep the project's central dimension invariant: the VALUE is always measured
// from the def points. #20 lets the author write the label around it (`<>` = the
// measurement); #21 lets the author move the label. Neither can make the geometry lie
// silently -- the measurement stays computed and inspectable in both cases.

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/properties_registry.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

DimData linear50() {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0, 0};
    d.b = {50, 0};
    d.line_pt = {25, 10};
    return d;
}

DimStyle style2() {
    DimStyle s;
    s.precision = 2;
    return s;
}

std::string label_of(const DimData& d, DimTextParts parts = {}) {
    return compute_dim_geometry(d, style2(), Rgb{}, parts).label;
}

} // namespace

// ---------------------------------------------------------------------------
// #20 -- text override
// ---------------------------------------------------------------------------

TEST_CASE("#20: `<>` expands to the measured value, so an override still tracks geometry") {
    DimData d = linear50();
    REQUIRE(label_of(d) == "50.00"); // no override: unchanged

    REQUIRE(label_of(d, {"", "", "<> H7"}) == "50.00 H7");
    REQUIRE(label_of(d, {"", "", "2x<>"}) == "2x50.00");
    REQUIRE(label_of(d, {"", "", "<> (<>)"}) == "50.00 (50.00)"); // every occurrence

    // ... and it really tracks: move a def point and the override follows.
    d.b = {80, 0};
    REQUIRE(dim_measure(d) == Approx(80.0));
    REQUIRE(label_of(d, {"", "", "<> H7"}) == "80.00 H7");
}

TEST_CASE("#20: an override with no `<>` replaces the value -- the author's explicit choice") {
    const DimData d = linear50();
    REQUIRE(label_of(d, {"", "", "SEE DETAIL A"}) == "SEE DETAIL A");
    // The MEASUREMENT is still computed, so the override can be compared against the
    // geometry and removed. That is what keeps the entity honest rather than silent.
    REQUIRE(dim_measure(d) == Approx(50.0));
}

TEST_CASE("#20: the override carries control codes, like any other text") {
    const DimData d = linear50();
    // %%c and \U+ go through the SAME substitution pass as a TEXT entity -- no
    // dimension-specific symbol handling.
    REQUIRE(label_of(d, {"", "", "%%c<>"}) == "⌀50.00");
    REQUIRE(label_of(d, {"", "", "<> \\U+00B1 0.1"}) == "50.00 ± 0.1");
}

TEST_CASE("#20: the override respects the per-type measured decoration") {
    DimData r;
    r.type = DimType::Radius;
    r.a = {0, 0};
    r.b = {10, 0};
    r.line_pt = {10, 0};
    // `<>` gives the type's own decorated value (R.., the diameter sign, the degree sign).
    REQUIRE(label_of(r, {"", "", "<> TYP"}) == "R10.00 TYP");

    DimData dia = r;
    dia.type = DimType::Diameter;
    REQUIRE(label_of(dia, {"", "", "<> THRU"}) == "⌀20.00 THRU");
}

TEST_CASE("#20: an override supersedes prefix/suffix and the deriving tolerance modes") {
    DimData d = linear50();
    d.tol.mode = TolMode::Symmetric;
    d.tol.upper = 0.1;
    // Without an override the decoration composes as #7 defined it.
    REQUIRE(label_of(d, {"6X ", " TYP", ""}) == "6X 50.00 ±0.10 TYP");
    // WITH one, the author is writing the text: nothing is generated around it.
    REQUIRE(label_of(d, {"6X ", " TYP", "<> H7"}) == "50.00 H7");

    // Basic and Reference still apply, because they FRAME the label rather than saying
    // anything -- a basic dimension stays basic whatever its text reads.
    d.tol.mode = TolMode::Reference;
    REQUIRE(label_of(d, {"", "", "<> H7"}) == "(50.00 H7)");
    d.tol.mode = TolMode::Basic;
    REQUIRE(compute_dim_geometry(d, style2(), Rgb{}, {"", "", "<>"}).label == "50.00");
    // The basic box is real geometry, so it is present in the dim-line list.
    const DimGeometry plain = compute_dim_geometry(linear50(), style2(), Rgb{}, {});
    const DimGeometry boxed = compute_dim_geometry(d, style2(), Rgb{}, {"", "", "<>"});
    REQUIRE(boxed.dim_lines.size() > plain.dim_lines.size());
}

TEST_CASE("#20: the override feeds the ISO 129-1 fit, so a long one moves the text out") {
    // The fit (#12) measures the composed label, so an override that widens it must
    // push the text outside exactly as a fit class does.
    DimData d = linear50();
    d.b = {14, 0};
    d.line_pt = {7, 10};
    const DimGeometry bare = compute_dim_geometry(d, style2(), Rgb{}, {});
    Vec2 q[4];
    REQUIRE(dim_label_quad(bare, false, q));
    REQUIRE(q[0].x < 14.0); // "14.00" fits

    const DimGeometry wide =
        compute_dim_geometry(d, style2(), Rgb{}, {"", "", "<> H7 THRU BOTH SIDES"});
    REQUIRE(dim_label_quad(wide, false, q));
    REQUIRE(q[0].x > 14.0); // the longer override does not
}

// ---------------------------------------------------------------------------
// #21 -- text position
// ---------------------------------------------------------------------------

TEST_CASE("#21: a zero offset is the derived position -- existing drawings are unchanged") {
    const DimData d = linear50();
    const DimGeometry g = compute_dim_geometry(d, style2(), Rgb{}, {});
    REQUIRE_FALSE(g.text_moved);
    DimData moved = d;
    moved.text_offset = {0.0, 0.0};
    REQUIRE(compute_dim_geometry(moved, style2(), Rgb{}, {}).text_pos.x == Approx(g.text_pos.x));
    REQUIRE(compute_dim_geometry(moved, style2(), Rgb{}, {}).text_pos.y == Approx(g.text_pos.y));
}

TEST_CASE("#21: the offset displaces the label and remembers where it came from") {
    const DimData base = linear50();
    const DimGeometry g0 = compute_dim_geometry(base, style2(), Rgb{}, {});

    DimData d = base;
    d.text_offset = {12.0, 7.0};
    const DimGeometry g = compute_dim_geometry(d, style2(), Rgb{}, {});
    REQUIRE(g.text_moved);
    REQUIRE(g.text_pos.x == Approx(g0.text_pos.x + 12.0));
    REQUIRE(g.text_pos.y == Approx(g0.text_pos.y + 7.0));
    // The derived position is kept, which is what "home text" returns to.
    REQUIRE(g.derived_text_pos.x == Approx(g0.text_pos.x));
    REQUIRE(g.derived_text_pos.y == Approx(g0.text_pos.y));
    // The VALUE is untouched -- moving text never changes what it says.
    REQUIRE(g.label == g0.label);
    REQUIRE(dim_measure(d) == Approx(50.0));
}

TEST_CASE("#21: displacement is in the label's own frame, so a vertical dim moves sensibly") {
    DimData v;
    v.type = DimType::Linear;
    v.a = {0, 0};
    v.b = {0, 50}; // vertical -> the label is rotated
    v.line_pt = {10, 25};
    const DimGeometry g0 = compute_dim_geometry(v, style2(), Rgb{}, {});
    REQUIRE(std::abs(g0.text_rotation) > 1.0); // genuinely rotated

    DimData d = v;
    d.text_offset = {10.0, 0.0}; // "along the baseline"
    const DimGeometry g = compute_dim_geometry(d, style2(), Rgb{}, {});
    // Along the baseline of a vertical dimension means along WORLD Y, not world X.
    REQUIRE(std::abs(g.text_pos.y - g0.text_pos.y) == Approx(10.0).margin(1e-9));
    REQUIRE(g.text_pos.x == Approx(g0.text_pos.x).margin(1e-9));
}

TEST_CASE("#21: every dimension type honours the offset (one code path)") {
    for (const DimType t :
         {DimType::Linear, DimType::Aligned, DimType::Radius, DimType::Diameter,
          DimType::Angular}) {
        DimData d;
        d.type = t;
        d.a = {0, 0};
        d.b = {30, 0};
        d.line_pt = t == DimType::Angular ? Vec2{0, 30} : Vec2{15, 12};
        const DimGeometry base = compute_dim_geometry(d, style2(), Rgb{}, {});
        d.text_offset = {5.0, 4.0};
        const DimGeometry moved = compute_dim_geometry(d, style2(), Rgb{}, {});
        INFO("dim type " << static_cast<int>(t));
        REQUIRE(moved.text_moved);
        REQUIRE(distance(moved.text_pos, base.text_pos) == Approx(std::hypot(5.0, 4.0)));
    }
}

TEST_CASE("#21: a far-displaced label gets a connector back to its dimension line") {
    DimData d = linear50();
    const std::size_t plain = compute_dim_geometry(d, style2(), Rgb{}, {}).dim_lines.size();

    // A nudge smaller than a text height is still "next to" the line -- no connector.
    d.text_offset = {0.5, 0.5};
    REQUIRE(compute_dim_geometry(d, style2(), Rgb{}, {}).dim_lines.size() == plain);

    // A real displacement gets one, so the value is never visually orphaned.
    d.text_offset = {30.0, 25.0};
    REQUIRE(compute_dim_geometry(d, style2(), Rgb{}, {}).dim_lines.size() > plain);
}

TEST_CASE("#21: the text grip drags the label and 'home text' restores it") {
    GeometryStore store;
    const EntityHandle h =
        store.add_dimension(DimType::Linear, {0, 0}, {50, 0}, {25, 10}, 0);

    std::vector<Grip> grips;
    grips_of(store, h, grips);
    // The text grip is present and uses the sentinel index, not a contiguous one.
    bool found = false;
    for (const Grip& g : grips) {
        if (g.index == DimData::kTextGripIndex) {
            found = true;
        }
    }
    REQUIRE(found);

    // Dragging it to a point stores an offset that puts the label THERE.
    GeometryStore dragged;
    const EntityHandle h2 = add_command_to_store(
        dragged, edit_for_grip_drag(store, h, DimData::kTextGripIndex, {70.0, 40.0}),
        EntityProps{});
    const DimData* moved = dragged.dimension(h2);
    REQUIRE(moved != nullptr);
    REQUIRE((moved->text_offset.x != 0.0 || moved->text_offset.y != 0.0));
    const DimGeometry g = compute_dim_geometry(*moved, DimStyle{}, Rgb{},
                                               dragged.dim_text_parts(*moved));
    Vec2 q[4];
    REQUIRE(dim_label_quad(g, false, q));
    const Vec2 centre = (q[0] + q[2]) * 0.5;
    REQUIRE(centre.x == Approx(70.0).margin(1e-6));
    REQUIRE(centre.y == Approx(40.0).margin(1e-6));
    // ... and the measurement is still 50, not 70-something.
    REQUIRE(dim_measure(*moved) == Approx(50.0));

    // "Home text" via the PR row: writing false clears the displacement.
    Command c = capture_entity(dragged, h2);
    PropertyValue off;
    off.flag = false;
    write_property(c, PropertyId::DimTextMoved, off);
    GeometryStore homed;
    const EntityHandle h3 = add_command_to_store(homed, c, EntityProps{});
    REQUIRE(homed.dimension(h3)->text_offset.x == Approx(0.0));
    REQUIRE(homed.dimension(h3)->text_offset.y == Approx(0.0));
}

TEST_CASE("#20/#21: both survive capture -> recreate (undo, copy, clipboard)") {
    GeometryStore store;
    const EntityHandle h = store.add_dimension(DimType::Linear, {0, 0}, {50, 0}, {25, 10}, 0, {},
                                               {}, "6X ", " TYP", {}, "<> H7", Vec2{3.0, 4.0});
    GeometryStore rebuilt;
    const EntityHandle h2 = add_command_to_store(rebuilt, capture_entity(store, h), EntityProps{});
    const DimData* b = rebuilt.dimension(h2);
    REQUIRE(b != nullptr);
    REQUIRE(rebuilt.dim_override(*b) == "<> H7");
    REQUIRE(b->text_offset.x == Approx(3.0));
    REQUIRE(b->text_offset.y == Approx(4.0));
    REQUIRE(rebuilt.dim_prefix(*b) == "6X ");
}

TEST_CASE("#20/#21: PR exposes the override and the moved state") {
    GeometryStore store;
    const EntityHandle h = store.add_dimension(DimType::Linear, {0, 0}, {50, 0}, {25, 10}, 0, {},
                                               {}, "", "", {}, "<> H7", Vec2{3.0, 4.0});
    const Command c = capture_entity(store, h);
    REQUIRE(property_applies(PropertyId::DimTextOverride, EntityKind::Dimension));
    REQUIRE(property_applies(PropertyId::DimTextMoved, EntityKind::Dimension));
    // ... and not on unrelated kinds.
    REQUIRE_FALSE(property_applies(PropertyId::DimTextOverride, EntityKind::Line));

    const SelectionSummary s = summarize_selection({c});
    bool saw_override = false;
    bool saw_moved = false;
    for (const PropertyField& f : s.fields) {
        if (f.id == PropertyId::DimTextOverride) {
            saw_override = true;
            REQUIRE(f.value.text == "<> H7");
        }
        if (f.id == PropertyId::DimTextMoved) {
            saw_moved = true;
            REQUIRE(f.value.flag);
        }
    }
    REQUIRE(saw_override);
    REQUIRE(saw_moved);
}

TEST_CASE("#20/#21: v19 round-trips the override and the offset; v18 loads without them") {
    GeometryStore s;
    s.add_dimension(DimType::Linear, {0, 0}, {50, 0}, {25, 10}, 0, {}, {}, "6X ", " TYP", {},
                    "<> H7 SEE NOTE 3", Vec2{3.5, -4.25});
    s.add_dimension(DimType::Aligned, {0, 0}, {30, 40}, {10, 30}, 0);

    const musacad::core::io::Document a = musacad::core::io::document_from_store(s);
    REQUIRE(a.format_version == musacad::core::io::kFormatVersion);
    const auto path =
        (std::filesystem::temp_directory_path() / "musacad_dimtext.musa").string();
    REQUIRE(musacad::core::io::save_native(a, path).ok);
    musacad::core::io::Document b;
    REQUIRE(musacad::core::io::load_native(path, b).ok);
    GeometryStore restored;
    musacad::core::io::populate_store(restored, b);
    REQUIRE(musacad::core::io::document_from_store(restored) == a);
    REQUIRE(b.dims[0].text_override == "<> H7 SEE NOTE 3"); // spaces survive the line
    REQUIRE(b.dims[0].text_offset.x == Approx(3.5));
    REQUIRE(b.dims[0].text_offset.y == Approx(-4.25));
    REQUIRE(b.dims[1].text_override.empty());
    std::filesystem::remove(path);

    // A real v18 record: 35 tokens + prefix/suffix lines, no offset and no override line.
    const std::string v18 =
        "MUSACAD 18\nLAYER 255 255 255 0 25 1 0 0 0\n"
        "DIM 0 0 0 10 0 5 3 0 0 7 255 255 255 0 25 1 0 2 1 3.5 2.5 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
        "6X \n TYP\n"
        "LINE 0 0 1 1 0 7 255 255 255 0 25\nEND\n";
    musacad::core::io::Document doc;
    REQUIRE(musacad::core::io::parse_native(v18, doc).ok);
    REQUIRE(doc.dims.size() == 1);
    REQUIRE(doc.dims[0].text_override.empty());
    REQUIRE(doc.dims[0].text_offset.x == Approx(0.0));
    REQUIRE(doc.dims[0].prefix == "6X "); // the v15 fields still parse
    REQUIRE(doc.dims[0].overrides.text_height == Approx(3.5)); // and the v8 block
    // The record AFTER the dimension is still found: no line was swallowed.
    REQUIRE(doc.lines.size() == 1);
}
