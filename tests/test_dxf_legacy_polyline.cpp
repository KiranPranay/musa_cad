// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// DXF import of the legacy POLYLINE / VERTEX / SEQEND form (#31): plane polylines come
// in as polylines (closed flag, bulges, layer), inside blocks too; 3D polylines and
// meshes are reported as skipped rather than mangled.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
std::string pairs(const std::string& body) {
    // "code value" per line -> DXF's two-line pairs.
    std::string out;
    std::size_t i = 0;
    while (i < body.size()) {
        const std::size_t nl = body.find('\n', i);
        const std::string line = body.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? body.size() : nl + 1;
        if (line.empty()) {
            continue;
        }
        const std::size_t sp = line.find(' ');
        out += line.substr(0, sp) + "\n" + (sp == std::string::npos ? "" : line.substr(sp + 1)) + "\n";
    }
    return out;
}
} // namespace

TEST_CASE("#31 DXF legacy POLYLINE/VERTEX/SEQEND imports as a polyline (closed, bulges, layer)") {
    const std::string dxf = pairs(R"(0 SECTION
2 ENTITIES
0 POLYLINE
8 WALLS
66 1
70 1
10 0
20 0
30 0
0 VERTEX
8 WALLS
10 0
20 0
42 0.5
0 VERTEX
8 WALLS
10 10
20 0
0 VERTEX
8 WALLS
10 10
20 10
0 SEQEND
8 WALLS
0 LINE
8 0
10 0
20 0
11 5
21 5
0 ENDSEC
0 EOF
)");
    Document doc;
    const IoResult r = parse_dxf(dxf, doc);
    REQUIRE(r.ok);
    REQUIRE(doc.polylines.size() == 1);
    const DocPolyline& pl = doc.polylines[0];
    CHECK(pl.closed);
    REQUIRE(pl.points.size() == 3);
    CHECK(pl.points[1] == Vec2{10, 0});
    REQUIRE(pl.bulges.size() == 3);
    CHECK(pl.bulges[0] == 0.5);
    CHECK(pl.bulges[1] == 0.0);
    REQUIRE(doc.lines.size() == 1); // the entity after SEQEND is unaffected
    CHECK(r.message.find("POLYLINE") == std::string::npos); // nothing skipped
}

TEST_CASE("#31 DXF legacy POLYLINE: spline-frame vertices are dropped; 3D polylines are skipped and reported") {
    const std::string dxf = pairs(R"(0 SECTION
2 ENTITIES
0 POLYLINE
8 0
66 1
70 4
0 VERTEX
8 0
70 16
10 -5
20 -5
0 VERTEX
8 0
70 8
10 0
20 0
0 VERTEX
8 0
70 8
10 1
20 1
0 SEQEND
0 POLYLINE
8 0
66 1
70 8
0 VERTEX
8 0
70 32
10 0
20 0
30 5
0 SEQEND
0 ENDSEC
0 EOF
)");
    Document doc;
    const IoResult r = parse_dxf(dxf, doc);
    REQUIRE(r.ok);
    REQUIRE(doc.polylines.size() == 1);
    CHECK(doc.polylines[0].points.size() == 2); // the frame control point is not a vertex
    CHECK_FALSE(doc.polylines[0].closed);
    CHECK(r.message.find("POLYLINE (3D/mesh)") != std::string::npos);
}

TEST_CASE("#31 DXF legacy POLYLINE inside a BLOCK lands in the block definition") {
    const std::string dxf = pairs(R"(0 SECTION
2 BLOCKS
0 BLOCK
2 FRAME
10 0
20 0
0 POLYLINE
8 0
66 1
70 1
0 VERTEX
8 0
10 0
20 0
0 VERTEX
8 0
10 4
20 0
0 VERTEX
8 0
10 4
20 3
0 SEQEND
0 ENDBLK
0 ENDSEC
0 SECTION
2 ENTITIES
0 INSERT
8 0
2 FRAME
10 1
20 1
0 ENDSEC
0 EOF
)");
    Document doc;
    REQUIRE(parse_dxf(dxf, doc).ok);
    REQUIRE(doc.block_defs.size() == 1);
    REQUIRE(doc.block_defs[0].polylines.size() == 1);
    CHECK(doc.block_defs[0].polylines[0].points.size() == 3);
    CHECK(doc.block_defs[0].polylines[0].closed);
    CHECK(doc.inserts.size() == 1);
}
