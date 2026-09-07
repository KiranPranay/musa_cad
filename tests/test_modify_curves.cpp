// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #27, the curve half: TRIM and EXTEND on polyline entities (open, closed, and
// with arc segments), and FILLET between a line and a circle/arc or between two
// circles/arcs. All verified through what the engine publishes.

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
bool eq(Vec2 p, Vec2 q, double eps) { return std::abs(p.x - q.x) < eps && std::abs(p.y - q.y) < eps; }
bool has_segment(const RenderSnapshot& s, Vec2 a, Vec2 b, double eps = 1e-6) {
    for (std::size_t i = 0; i + 1 < s.line_vertices.size(); i += 2) {
        const Vec2 p = s.line_vertices[i];
        const Vec2 q = s.line_vertices[i + 1];
        if ((eq(p, a, eps) && eq(q, b, eps)) || (eq(p, b, eps) && eq(q, a, eps))) {
            return true;
        }
    }
    return false;
}
bool has_vertex(const RenderSnapshot& s, Vec2 a, double eps = 1e-3) {
    for (const Vec2& p : s.line_vertices) {
        if (eq(p, a, eps)) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("#27 TRIM an open polyline: the span between the pick's crossings goes") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}}, false, 1});
    engine.submit(AddLineCommand{{5, -5}, {5, 5}, 2}); // cuts the first segment at (5,0)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));

    engine.submit(TrimPickCommand{{2, 0}, 1.0, 10}); // the piece from the start to the cut
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {5, 0}, {10, 0}) && !has_segment(s, {0, 0}, {10, 0});
    }));
    REQUIRE(has_segment(engine.snapshot(), {10, 0}, {10, 10}));
    REQUIRE(!has_vertex(engine.snapshot(), {0, 0}));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {0, 0}, {10, 0}); }));
    engine.stop();
}

TEST_CASE("#27 TRIM a polyline between two crossings leaves two polylines") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {15, 0}, {30, 0}}, false, 1});
    engine.submit(AddLineCommand{{10, -5}, {10, 5}, 2});
    engine.submit(AddLineCommand{{20, -5}, {20, 5}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    engine.submit(TrimPickCommand{{15, 0}, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {0, 0}, {10, 0}) && has_segment(s, {20, 0}, {30, 0});
    }));
    REQUIRE(!has_vertex(engine.snapshot(), {15, 0}));
    engine.stop();
}

TEST_CASE("#27 TRIM a closed polyline: one open polyline remains (the circle rule)") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true, 1});
    engine.submit(AddLineCommand{{5, -5}, {5, 15}, 2}); // crosses bottom (5,0) and top (5,10)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 10; }));
    engine.submit(TrimPickCommand{{2, 0}, 1.0, 10}); // the left half goes
    REQUIRE(wait_until(engine, [](const auto& s) {
        return has_segment(s, {5, 0}, {10, 0}) && has_segment(s, {10, 10}, {5, 10}) &&
               !has_segment(s, {0, 0}, {10, 0});
    }));
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_segment(s, {10, 0}, {10, 10}));
    REQUIRE(!has_segment(s, {0, 10}, {0, 0}));
    REQUIRE(!has_vertex(s, {0, 10}));
    engine.stop();
}

TEST_CASE("#27 TRIM a polyline's ARC segment: the cut lands on the arc and the rest stays an exact arc") {
    // (0,0)->(10,0) with bulge 1: a semicircle below the chord, centre (5,0) r 5.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {10, 0}}, false, 1, {}, {1.0, 0.0}});
    engine.submit(AddLineCommand{{5, -10}, {5, 10}, 2}); // cuts the arc at (5,-5)
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    // Pick the right-hand quarter (angle 315 deg from the centre).
    engine.submit(TrimPickCommand{{5 + 5 * std::cos(-kPi / 4), 5 * std::sin(-kPi / 4)}, 1.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return !has_vertex(s, {10, 0}, 1e-6); }));
    const RenderSnapshot& s = engine.snapshot();
    // What remains: the quarter arc from (0,0) down to (5,-5), still on the circle.
    REQUIRE(has_vertex(s, {0, 0}, 1e-3));
    REQUIRE(has_vertex(s, {5, -5}, 1e-3));
    for (const Vec2& p : s.line_vertices) {
        if (std::abs(p.x - 5.0) < 1e-9 && std::abs(std::abs(p.y) - 10.0) < 1e-9) {
            continue; // the cutting line's own ends
        }
        REQUIRE(p.x <= 5.0 + 1e-3);
        REQUIRE(p.y >= -5.0 - 1e-3);
        REQUIRE(std::abs(std::hypot(p.x - 5.0, p.y) - 5.0) < 1e-3); // on the arc
    }
    engine.stop();
}

TEST_CASE("#27 EXTEND an open polyline's end to a line, and to a polyline boundary") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {5, 0}, {5, 5}}, false, 1});
    engine.submit(AddLineCommand{{0, 10}, {10, 10}, 2}); // boundary above
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));
    engine.submit(ExtendPickCommand{{5, 4.5}, 1.0, 10}); // near the (5,5) end
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {5, 0}, {5, 10}); }));
    REQUIRE(has_segment(engine.snapshot(), {0, 0}, {5, 0})); // the rest is untouched

    // A closed polyline is a boundary too: extend the START end left to the box.
    engine.submit(AddPolylineCommand{{{-30, -5}, {-20, -5}, {-20, 5}, {-30, 5}}, true, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {-20, -5}, {-20, 5}); }));
    engine.submit(ExtendPickCommand{{0.5, 0}, 1.0, 11}); // near the (0,0) end
    REQUIRE(wait_until(engine, [](const auto& s) { return has_segment(s, {-20, 0}, {5, 0}); }));
    engine.stop();
}

TEST_CASE("#27 FILLET a line and a circle: the line is trimmed to the tangent point, the circle stays whole") {
    // Line y=0, circle centre (0,10) r 5, fillet r 3 on the LEFT: the fillet centre is
    // on the offset line y=3 at distance 8 from (0,10): x = -sqrt(64-49).
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{-20, 0}, {20, 0}, 1});
    engine.submit(AddCircleCommand{{0, 10}, 5.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    engine.submit(FilletPickCommand{{-8, 0}, {-3.5, 6.5}, 3.0, 1.5, 10});
    const double cx = -std::sqrt(64.0 - 49.0);
    REQUIRE(wait_until(engine, [&](const auto& s) { return has_segment(s, {-20, 0}, {cx, 0}, 1e-6); }));
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(!has_segment(s, {-20, 0}, {20, 0}));
    REQUIRE(!has_vertex(s, {20, 0}, 1e-6)); // the far end of the line is gone (pick side kept)
    // The circle is still whole: its top is still drawn (a tessellation sample lies
    // within half a chord step of the exact top, which is not itself a sample).
    REQUIRE(has_vertex(s, {0, 15}, 0.4));
    // The tangent point on the circle: (0,10) + (C - (0,10)) * 5/8.
    const Vec2 t2{cx * 5.0 / 8.0, 10.0 + (3.0 - 10.0) * 5.0 / 8.0};
    REQUIRE(has_vertex(s, t2, 1e-2));
    engine.stop();
}

TEST_CASE("#27 FILLET two circles: the rounding arc is externally tangent to both") {
    // Circles r 5 at (0,0) and (20,0); fillet r 6 picked on their facing upper sides.
    // Centre: on both offset circles (r 11): x = 10, y = sqrt(121 - 100).
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 5.0, 1});
    engine.submit(AddCircleCommand{{20, 0}, 5.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    engine.submit(FilletPickCommand{{4, 3}, {16, 3}, 6.0, 1.5, 10});
    const double cy = std::sqrt(121.0 - 100.0);
    const Vec2 t1{10.0 * 5.0 / 11.0, cy * 5.0 / 11.0};
    REQUIRE(wait_until(engine, [&](const auto& s) { return has_vertex(s, t1, 1e-2); }));
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_vertex(s, {20.0 - 10.0 * 5.0 / 11.0, cy * 5.0 / 11.0}, 1e-2));
    // The minor arc dips between the circles: its lowest point is (10, cy - 6).
    REQUIRE(has_vertex(s, {10.0, cy - 6.0}, 2e-2));
    // Both circles remain whole (their tops are drawn; see the line/circle case).
    REQUIRE(has_vertex(s, {0, 5}, 0.4));
    REQUIRE(has_vertex(s, {25, 0}, 1e-2));
    engine.stop();
}

TEST_CASE("#27 FILLET a line and an ARC keeps the arc's picked side; radius 0 on a curve is refused") {
    GeometryEngine engine;
    engine.start();
    // Arc: centre (0,0) r 10 from 0 to 180 deg (upper half). Line x = 15 vertical.
    engine.submit(AddArcCommand{{0, 0}, 10.0, 0.0, kPi, 1});
    engine.submit(AddLineCommand{{15, -20}, {15, 20}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    engine.submit(FilletPickCommand{{15, 8}, {8, 6}, 0.0, 1.5, 9});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("radius greater than 0") != std::string::npos;
    }));
    // Radius 3, picks on the line's upper part and the arc's right part: the fillet
    // centre is at x = 12 (offset line) and distance 13 from the origin: y = 5.
    engine.submit(FilletPickCommand{{15, 8}, {8, 6}, 3.0, 1.5, 10});
    const Vec2 t_line{15.0, 5.0};
    const Vec2 t_arc{12.0 * 10.0 / 13.0, 5.0 * 10.0 / 13.0};
    REQUIRE(wait_until(engine, [&](const auto& s) { return has_vertex(s, t_line, 1e-2) && has_vertex(s, t_arc, 1e-2); }));
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(has_segment(s, {15, 5}, {15, 20}, 1e-6)); // the line's upper (picked) side kept
    REQUIRE(!has_vertex(s, {15, -20}, 1e-6));
    REQUIRE(has_vertex(s, {-10, 0}, 1e-2)); // the arc still reaches its far end
    REQUIRE(!has_vertex(s, {10, 0}, 1e-3)); // but no longer its trimmed end
    engine.stop();
}
