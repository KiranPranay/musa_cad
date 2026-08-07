// ISO 129-1 narrow-dimension fallback (issue #12): when the value and/or the
// arrowheads do not fit between the extension lines, the value goes OUTSIDE and the
// arrowheads flip to point inward from beyond them.
//
// The headline case is the ladder: one text height, foot separations from 100 mm down
// to 0.25 mm, and at EVERY rung the label must not touch an extension line or an
// arrowhead -- while the reported measurement stays exactly what it was.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/properties.hpp"
#include "musacad/core/text/stroke_font.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

/// Separating-axis test for two CONVEX polygons. Both shapes here are convex (the label
/// quad, an arrowhead triangle, and an extension line treated as a degenerate 2-point
/// polygon), so SAT is exact -- and it handles the containment case that a naive
/// edge-crossing test misses (a short extension line entirely inside the label box).
bool convex_overlap(const std::vector<Vec2>& a, const std::vector<Vec2>& b, double slack) {
    const auto axes_of = [](const std::vector<Vec2>& p, std::vector<Vec2>& out) {
        for (std::size_t i = 0; i < p.size(); ++i) {
            const Vec2 e = p[(i + 1) % p.size()] - p[i];
            const double len = length(e);
            if (len > 1e-12) {
                out.push_back(Vec2{-e.y, e.x} / len);
            }
        }
    };
    std::vector<Vec2> axes;
    axes_of(a, axes);
    axes_of(b, axes);
    for (const Vec2& ax : axes) {
        double amin = 1e300;
        double amax = -1e300;
        double bmin = 1e300;
        double bmax = -1e300;
        for (const Vec2& p : a) {
            amin = std::min(amin, dot(p, ax));
            amax = std::max(amax, dot(p, ax));
        }
        for (const Vec2& p : b) {
            bmin = std::min(bmin, dot(p, ax));
            bmax = std::max(bmax, dot(p, ax));
        }
        // A gap on ANY axis proves separation. `slack` shrinks the shapes slightly so a
        // legitimate exact touch (shared boundary) is not reported as an overlap.
        if (amin > bmax - slack || bmin > amax - slack) {
            return false;
        }
    }
    return true;
}

std::vector<Vec2> quad_of(const DimGeometry& g, bool second) {
    Vec2 q[4];
    if (!dim_label_quad(g, second, q)) {
        return {};
    }
    return {q[0], q[1], q[2], q[3]};
}

/// The extension lines as 2-point degenerate polygons (they are drawn as segments).
std::vector<std::vector<Vec2>> ext_segments(const DimGeometry& g) {
    std::vector<std::vector<Vec2>> out;
    for (std::size_t i = 0; i + 1 < g.ext_lines.size(); i += 2) {
        out.push_back({g.ext_lines[i], g.ext_lines[i + 1]});
    }
    return out;
}

/// The filled arrowheads as triangles.
std::vector<std::vector<Vec2>> arrow_triangles(const DimGeometry& g) {
    std::vector<std::vector<Vec2>> out;
    for (std::size_t i = 0; i + 2 < g.arrow_fills.size(); i += 3) {
        out.push_back({g.arrow_fills[i], g.arrow_fills[i + 1], g.arrow_fills[i + 2]});
    }
    return out;
}

DimData linear(double span) {
    DimData d;
    d.type = DimType::Linear;
    d.a = {0.0, 0.0};
    d.b = {span, 0.0};
    d.line_pt = {span * 0.5, 12.0};
    return d;
}

DimStyle iso_style() {
    DimStyle s;
    s.text_height = 2.5; // the height ISO 129-1 recommends for dimensions
    s.arrow_size = 2.5;
    s.precision = 2;
    return s;
}

} // namespace

TEST_CASE("Ladder: the label never touches an extension line or an arrowhead") {
    const DimStyle style = iso_style();
    // 100 mm down to 0.25 mm. The issue's worked example (a 15 mm feature at 1:5 leaving
    // a 3.0 mm gap, with "15" exactly 3.0 mm wide) sits inside this range.
    const std::vector<double> spans = {100.0, 60.0, 40.0, 25.0, 18.0, 15.0, 12.0, 10.0,
                                       8.0,   6.0,  5.0,  4.0,  3.0,  2.5,  2.0,  1.5,
                                       1.0,   0.75, 0.5,  0.3,  0.25};
    for (const double span : spans) {
        const DimData d = linear(span);
        const DimGeometry g = compute_dim_geometry(d, style, Rgb{255, 255, 255});
        INFO("foot separation = " << span);

        // 1. The measured value is untouched by the fit treatment -- the whole point of
        //    the issue is that the dimension keeps telling the truth about the geometry.
        REQUIRE(dim_measure(d) == Approx(span));
        REQUIRE(g.label == format_measurement(span, style.precision));

        const std::vector<Vec2> label = quad_of(g, false);
        REQUIRE(label.size() == 4);

        // 2. The label does not intersect either extension line.
        for (const std::vector<Vec2>& e : ext_segments(g)) {
            REQUIRE_FALSE(convex_overlap(label, e, 1e-9));
        }
        // 3. The label does not overlap an arrowhead.
        for (const std::vector<Vec2>& t : arrow_triangles(g)) {
            REQUIRE_FALSE(convex_overlap(label, t, 1e-9));
        }
    }
}

TEST_CASE("Ladder: wide dimensions keep the value inside, narrow ones push it out") {
    const DimStyle style = iso_style();
    // The label is centred between the feet only while it fits; the transition must
    // actually happen somewhere in the ladder, or the test above would pass vacuously
    // by never exercising the outside branch.
    const auto label_center_x = [&](double span) {
        const DimGeometry g = compute_dim_geometry(linear(span), style, Rgb{});
        const std::vector<Vec2> q = quad_of(g, false);
        double sum = 0.0;
        for (const Vec2& p : q) {
            sum += p.x;
        }
        return sum / 4.0;
    };
    // Wide: centred over the span.
    REQUIRE(label_center_x(100.0) == Approx(50.0).margin(1.0));
    // Narrow: pushed clear of the right-hand extension line.
    REQUIRE(label_center_x(1.0) > 1.0);
    REQUIRE(label_center_x(0.25) > 0.25);
}

TEST_CASE("Text and arrows are fitted independently, giving all four states") {
    const DimStyle style = iso_style(); // text 2.5, arrows 2.5
    // Arrows need 2*2.5 + 1.5 = 6.5; the label "NN.NN" is 5 glyphs * 0.62 * 2.5 = 7.75
    // plus 2 * 1.0 gap = 9.75. So there is a band where the arrows fit and the text
    // does not -- which is exactly the state a single binary test would get wrong.
    const auto arrows_outside = [&](double span) {
        const DimGeometry g = compute_dim_geometry(linear(span), style, Rgb{});
        // With arrows outside the dimension line is extended past both feet, so it is
        // longer than the span; inside, it runs foot to foot.
        REQUIRE(g.dim_lines.size() >= 2);
        return length(g.dim_lines[1] - g.dim_lines[0]) > span + 1e-6;
    };
    const auto text_outside = [&](double span) {
        const DimGeometry g = compute_dim_geometry(linear(span), style, Rgb{});
        const std::vector<Vec2> q = quad_of(g, false);
        return q[0].x > span; // starts past the far extension line
    };

    // Both inside.
    REQUIRE_FALSE(arrows_outside(100.0));
    REQUIRE_FALSE(text_outside(100.0));
    // Arrows still fit, text does not: the independent-resolution case.
    REQUIRE_FALSE(arrows_outside(8.0));
    REQUIRE(text_outside(8.0));
    // Both outside.
    REQUIRE(arrows_outside(2.0));
    REQUIRE(text_outside(2.0));
}

TEST_CASE("Arrows outside are the SAME arrowhead, reversed -- not a second code path") {
    const DimStyle style = iso_style();
    const DimGeometry wide = compute_dim_geometry(linear(100.0), style, Rgb{});
    const DimGeometry narrow = compute_dim_geometry(linear(1.0), style, Rgb{});
    // Same arrowhead count and the same triangle vertex count: only the orientation and
    // the dimension line's extent differ.
    REQUIRE(narrow.arrow_fills.size() == wide.arrow_fills.size());

    // The apex of each head still sits ON its extension-line foot; the body is on the
    // other side. Vertex 0 of each triangle is the tip (see append_arrowhead).
    REQUIRE(narrow.arrow_fills.size() >= 6);
    REQUIRE(narrow.arrow_fills[0].x == Approx(0.0).margin(1e-9));
    REQUIRE(narrow.arrow_fills[3].x == Approx(1.0).margin(1e-9));
    // Inside, the first head's body extends toward +x; outside, away from it.
    const double wide_body = wide.arrow_fills[1].x - wide.arrow_fills[0].x;
    const double narrow_body = narrow.arrow_fills[1].x - narrow.arrow_fills[0].x;
    REQUIRE(wide_body > 0.0);
    REQUIRE(narrow_body < 0.0);
}

TEST_CASE("text_fit forces the treatment; Auto is the default so old drawings just fit") {
    DimStyle style = iso_style();
    const auto text_starts_past_span = [&](const DimData& d, const DimStyle& s) {
        const DimGeometry g = compute_dim_geometry(d, s, Rgb{});
        const std::vector<Vec2> q = quad_of(g, false);
        return q[0].x > dim_measure(d);
    };

    // Auto (the default) on a wide dimension: inside.
    REQUIRE_FALSE(text_starts_past_span(linear(100.0), style));

    // Forced Outside puts it out even though it fits comfortably.
    DimData forced_out = linear(100.0);
    forced_out.overrides.set(DimOverrides::kTextFit, true);
    forced_out.overrides.text_fit = static_cast<std::uint8_t>(TextFit::Outside);
    REQUIRE(text_starts_past_span(forced_out, style));

    // Forced Inside keeps it in even though it collides -- the author asked for it.
    DimData forced_in = linear(1.0);
    forced_in.overrides.set(DimOverrides::kTextFit, true);
    forced_in.overrides.text_fit = static_cast<std::uint8_t>(TextFit::Inside);
    REQUIRE_FALSE(text_starts_past_span(forced_in, style));
    // ... and that IS the colliding placement, i.e. the override really does override.
    const DimGeometry g = compute_dim_geometry(forced_in, style, Rgb{});
    bool touches = false;
    for (const std::vector<Vec2>& e : ext_segments(g)) {
        touches = touches || convex_overlap(quad_of(g, false), e, 1e-9);
    }
    REQUIRE(touches);

    // The STYLE default flows through apply_dim_overrides like every other field.
    style.text_fit = static_cast<std::uint8_t>(TextFit::Outside);
    REQUIRE(text_starts_past_span(linear(100.0), style));
}

TEST_CASE("The fit test measures the DECORATED label, not the bare value") {
    // A dimension that fits bare must go outside once a fit class widens it -- this is
    // why #12 had to land after #7 and why the fit test reads the composed label.
    GeometryStore store;
    DimStyle style = iso_style();

    DimData d = linear(14.0);
    const DimGeometry bare = compute_dim_geometry(d, style, Rgb{});
    const std::vector<Vec2> bare_q = quad_of(bare, false);
    REQUIRE(bare_q[0].x < 14.0); // "14.00" fits in 14 mm

    const DimTextParts parts{"⌀", " H7 ±0.02"};
    const DimGeometry decorated = compute_dim_geometry(d, style, Rgb{}, parts);
    const std::vector<Vec2> dec_q = quad_of(decorated, false);
    REQUIRE(text::text_width(decorated.label, style.text_height) >
            text::text_width(bare.label, style.text_height));
    REQUIRE(dec_q[0].x > 14.0); // the decorated label no longer does

    // Two-line Limits uses the WIDEST line, not the first.
    d.tol.mode = TolMode::Limits;
    d.tol.upper = 0.046;
    d.tol.lower = 0.0;
    const DimGeometry lim = compute_dim_geometry(d, style, Rgb{});
    REQUIRE_FALSE(lim.label2.empty());
    for (const std::vector<Vec2>& e : ext_segments(lim)) {
        REQUIRE_FALSE(convex_overlap(quad_of(lim, false), e, 1e-9));
        REQUIRE_FALSE(convex_overlap(quad_of(lim, true), e, 1e-9));
    }
}

TEST_CASE("A vertical narrow dimension is fitted in its own frame, not in world x") {
    // The fit runs along the dimension's direction, so a vertical dimension must behave
    // exactly like a horizontal one -- a fit test written in world x would silently do
    // nothing here.
    const DimStyle style = iso_style();
    DimData d;
    d.type = DimType::Linear;
    d.a = {0.0, 0.0};
    d.b = {0.0, 1.0}; // 1 mm tall: far too narrow for "1.00"
    d.line_pt = {12.0, 0.5};
    const DimGeometry g = compute_dim_geometry(d, style, Rgb{});
    REQUIRE(dim_measure(d) == Approx(1.0));
    for (const std::vector<Vec2>& e : ext_segments(g)) {
        REQUIRE_FALSE(convex_overlap(quad_of(g, false), e, 1e-9));
    }
    for (const std::vector<Vec2>& t : arrow_triangles(g)) {
        REQUIRE_FALSE(convex_overlap(quad_of(g, false), t, 1e-9));
    }
}
