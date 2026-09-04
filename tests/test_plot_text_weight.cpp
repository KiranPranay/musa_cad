// Issue #19: stroke-font text plotted at a cosmetic hairline regardless of the entity's
// lineweight, so dimension text and notes were unreadable on paper while LINE entities
// at the same weight printed correctly.
//
// The fix is in the SNAPSHOT, not the plot renderer: glyph segments used to enter
// line_batches with lineweight 0, so paint_plot's `b.lineweight > 0` rule left them
// cosmetic. These assert the weight reaches the batch -- which is what the plot reads.

#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

/// The lineweights of every batch that carries stroke-TEXT geometry.
std::vector<std::uint8_t> text_batch_weights(const RenderSnapshot& s) {
    std::vector<std::uint8_t> out;
    for (const ColorBatch& b : s.line_batches) {
        if (b.is_text && b.count > 0) {
            out.push_back(b.lineweight);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// The lineweights of every NON-text batch (the LINE references in the issue's repro).
std::vector<std::uint8_t> geometry_batch_weights(const RenderSnapshot& s) {
    std::vector<std::uint8_t> out;
    for (const ColorBatch& b : s.line_batches) {
        if (!b.is_text && b.count > 0) {
            out.push_back(b.lineweight);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

EntityProps at_weight(std::uint8_t lw) {
    EntityProps p;
    p.set_lineweight_by_layer(false);
    p.lineweight = lw;
    return p;
}

} // namespace

TEST_CASE("Issue #19: TEXT carries its entity lineweight into the plotted batch") {
    GeometryStore store;
    NativeKernel2D kernel;
    RenderSnapshot snap;

    // The issue's repro shape: text at several weights, plus LINE references.
    store.add_text({10, 30}, 5.0, 0.0, 0, "LW-25 QUICK BROWN", at_weight(25));
    store.add_text({10, 20}, 5.0, 0.0, 0, "LW-50 QUICK BROWN", at_weight(50));
    store.add_text({10, 10}, 5.0, 0.0, 0, "LW-100 QUICK BROWN", at_weight(100));
    store.add_line({10, 5}, {90, 5}, at_weight(25));
    store.add_line({10, 3}, {90, 3}, at_weight(50));

    build_render_snapshot(store, kernel, snap, 0.1, 1.0);

    // Before the fix every text batch reported 0 -- identical hairlines on paper.
    const std::vector<std::uint8_t> text_w = text_batch_weights(snap);
    REQUIRE(text_w == std::vector<std::uint8_t>{25, 50, 100});
    // The LINE references are unchanged, which is the comparison the report makes.
    const std::vector<std::uint8_t> geom_w = geometry_batch_weights(snap);
    REQUIRE(geom_w == std::vector<std::uint8_t>{25, 50});
    // No text batch may be cosmetic when the author set a real weight.
    for (const std::uint8_t w : text_w) {
        REQUIRE(w > 0);
    }
}

TEST_CASE("Issue #19: MTEXT and leader labels carry their lineweight too") {
    GeometryStore store;
    NativeKernel2D kernel;
    RenderSnapshot snap;

    MTextBlock b;
    b.pos = {10, 30};
    b.height = 5.0;
    b.width = 80.0;
    store.add_mtext(b, "LW-100 QUICK BROWN", at_weight(100));
    store.add_leader({0, 0}, {20, 20}, 3.0, 0, "NOTE", at_weight(60));

    build_render_snapshot(store, kernel, snap, 0.1, 1.0);
    const std::vector<std::uint8_t> text_w = text_batch_weights(snap);
    REQUIRE_FALSE(text_w.empty());
    for (const std::uint8_t w : text_w) {
        REQUIRE(w > 0);
    }
    // Both weights are represented: the label follows its own entity, not a default.
    REQUIRE(std::find(text_w.begin(), text_w.end(), 100) != text_w.end());
    REQUIRE(std::find(text_w.begin(), text_w.end(), 60) != text_w.end());
}

TEST_CASE("Issue #19: ByLayer text resolves through the layer, like every other entity") {
    GeometryStore store;
    NativeKernel2D kernel;
    RenderSnapshot snap;

    Layer heavy;
    heavy.name = "HEAVY";
    heavy.lineweight = 80;
    const std::uint16_t li = store.add_layer(heavy);

    EntityProps p; // fully ByLayer -- the default
    p.layer = li;
    store.add_text({0, 0}, 5.0, 0.0, 0, "BYLAYER", p);

    build_render_snapshot(store, kernel, snap, 0.1, 1.0);
    REQUIRE(text_batch_weights(snap) == std::vector<std::uint8_t>{80});
}

TEST_CASE("Issue #19: dimension text carries the dimension style's lineweight") {
    GeometryStore store;
    NativeKernel2D kernel;
    RenderSnapshot snap;

    DimStyle s;
    s.name = "HEAVY";
    s.dim_lineweight = 70;
    const std::uint16_t si = store.add_dimstyle(s);
    store.add_dimension(DimType::Linear, {0, 0}, {50, 0}, {25, 10}, si);

    build_render_snapshot(store, kernel, snap, 0.1, 1.0);
    const std::vector<std::uint8_t> text_w = text_batch_weights(snap);
    REQUIRE(text_w == std::vector<std::uint8_t>{70});
}

TEST_CASE("Issue #19: the screen-weight tag survives, so the viewport is unaffected") {
    // The Ph31 on-screen text weight keys on ColorBatch::is_text and IGNORES lineweight,
    // so carrying a real weight must not disturb it. Both signals have to be present.
    GeometryStore store;
    NativeKernel2D kernel;
    RenderSnapshot snap;
    store.add_text({0, 0}, 5.0, 0.0, 0, "AB", at_weight(50));
    build_render_snapshot(store, kernel, snap, 0.1, 1.0);

    bool found = false;
    for (const ColorBatch& b : snap.line_batches) {
        if (b.is_text && b.count > 0) {
            found = true;
            REQUIRE(b.lineweight == 50);              // for PLOT
            REQUIRE(b.text_height == Approx(5.0f));   // for the viewport's taper
        }
    }
    REQUIRE(found);
}
