// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The shared corner/lobe operations (core/polyline_ops.hpp): the fillet and chamfer that
// FILLET, CHAMFER and RECTANGLE's options all run, and the revision-cloud lobes that
// REVCLOUD builds. Tested directly, because every command above trusts them.

#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/polyline_ops.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("polyline_ops: filleting a square corner inserts a quarter-circle bulge") {
    // CCW square; corner 1 at (100,0). A radius-10 fillet replaces it with two tangent
    // points 10 back along each edge and a 90-degree arc between them: bulge = tan(pi/8).
    std::vector<Vec2> pts{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    std::vector<double> bulges;
    REQUIRE(polyline_ops::fillet_corner(pts, bulges, /*closed=*/true, 1, 10.0));
    REQUIRE(pts.size() == 5);
    REQUIRE(pts[1].x == Approx(90.0));
    REQUIRE(pts[1].y == Approx(0.0));
    REQUIRE(pts[2].x == Approx(100.0));
    REQUIRE(pts[2].y == Approx(10.0));
    REQUIRE(std::abs(bulges[1]) == Approx(std::tan(kPi / 8.0)));
    REQUIRE(bulges[1] > 0.0); // CCW corner turns left: a CCW (positive) arc
    REQUIRE(bulges[2] == Approx(0.0));
}

TEST_CASE("polyline_ops: a fillet that does not fit is refused, leaving the corner alone") {
    std::vector<Vec2> pts{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    std::vector<double> bulges;
    REQUIRE(!polyline_ops::fillet_corner(pts, bulges, true, 1, 50.0)); // radius > edge
    REQUIRE(pts.size() == 4);
}

TEST_CASE("polyline_ops: chamfering a corner cuts it at the two distances") {
    std::vector<Vec2> pts{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    REQUIRE(polyline_ops::chamfer_corner(pts, true, 1, 10.0, 20.0));
    REQUIRE(pts.size() == 5);
    REQUIRE(pts[1].x == Approx(90.0)); // 10 back along the incoming edge
    REQUIRE(pts[1].y == Approx(0.0));
    REQUIRE(pts[2].x == Approx(100.0)); // 20 along the outgoing edge
    REQUIRE(pts[2].y == Approx(20.0));
}

TEST_CASE("polyline_ops: a revision cloud's lobes bulge outward and divide edges evenly") {
    // A CCW 100x100 square with 25-unit lobes: 4 lobes per edge, 16 arcs, all positive
    // (CCW arcs bulge to the right of travel, i.e. outward for a CCW loop), 120 degrees.
    std::vector<Vec2> path{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    std::vector<Vec2> v;
    std::vector<double> b;
    polyline_ops::revcloud_from_path(path, true, 25.0, false, v, b);
    REQUIRE(v.size() == 16);
    REQUIRE(b.size() == 16);
    for (double x : b) {
        REQUIRE(x == Approx(std::tan(kPi / 6.0)));
    }
    REQUIRE(v[1].x == Approx(25.0)); // the first edge split into 25-unit chords
    // Reverse direction flips every lobe inward.
    polyline_ops::revcloud_from_path(path, true, 25.0, true, v, b);
    for (double x : b) {
        REQUIRE(x == Approx(-std::tan(kPi / 6.0)));
    }
}

TEST_CASE("polyline_ops: a clockwise path still bulges outward") {
    std::vector<Vec2> path{{0, 0}, {0, 100}, {100, 100}, {100, 0}}; // CW
    std::vector<Vec2> v;
    std::vector<double> b;
    polyline_ops::revcloud_from_path(path, true, 50.0, false, v, b);
    REQUIRE(v.size() == 8);
    for (double x : b) {
        REQUIRE(x == Approx(-std::tan(kPi / 6.0))); // CW arcs are outward for a CW loop
    }
}

TEST_CASE("polyline_ops: an open path keeps its far end as a plain vertex") {
    std::vector<Vec2> path{{0, 0}, {100, 0}};
    std::vector<Vec2> v;
    std::vector<double> b;
    polyline_ops::revcloud_from_path(path, false, 20.0, false, v, b);
    REQUIRE(v.size() == 6); // 5 lobes + the end vertex
    REQUIRE(b.back() == Approx(0.0));
    REQUIRE(v.back() == Vec2{100, 0});
}
