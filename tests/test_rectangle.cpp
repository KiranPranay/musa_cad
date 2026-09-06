// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// RECTANGLE's corner options (issue #33): Chamfer and Fillet, with AutoCAD's prompts,
// session-persistent defaults, "last one set wins", and the square-corner fallback when
// the treatment does not fit. The corner math itself is core::polyline_ops, shared with
// FILLET/CHAMFER and tested there; these cases test the command's use of it.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    [[nodiscard]] const AddPolylineCommand& poly() const {
        return std::get<AddPolylineCommand>(cmds.at(0));
    }
};
bool has_pt(const std::vector<Vec2>& v, Vec2 p) {
    return std::any_of(v.begin(), v.end(), [&](const Vec2& q) { return length(q - p) < 1e-9; });
}
/// Reset the session defaults so cases do not leak into each other.
void reset_defaults() {
    ProcHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("F");
    h.proc.submit_line("0");
    h.proc.cancel();
}
} // namespace

TEST_CASE("#33: RECTANGLE [Fillet] rounds all four corners") {
    reset_defaults();
    ProcHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("F");
    h.proc.submit_line("5");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    REQUIRE(h.cmds.size() == 1);
    const AddPolylineCommand& p = h.poly();
    REQUIRE(p.closed);
    REQUIRE(p.points.size() == 8);   // two tangent points per corner
    REQUIRE(p.bulges.size() == 8);
    int arcs = 0;
    for (double b : p.bulges) {
        if (b != 0.0) {
            REQUIRE(std::abs(b) == Approx(std::tan(kPi / 8.0))); // quarter circles
            ++arcs;
        }
    }
    REQUIRE(arcs == 4);
    REQUIRE(has_pt(p.points, {95, 0}));  // tangent points 5 back from each corner
    REQUIRE(has_pt(p.points, {100, 5}));
    REQUIRE(has_pt(p.points, {5, 50}));
    REQUIRE(has_pt(p.points, {0, 45}));
    reset_defaults();
}

TEST_CASE("#33: RECTANGLE [Chamfer] cuts all four corners at the two distances") {
    reset_defaults();
    ProcHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("C");
    h.proc.submit_line("5");
    h.proc.submit_line("10");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    REQUIRE(h.cmds.size() == 1);
    const AddPolylineCommand& p = h.poly();
    REQUIRE(p.points.size() == 8);
    REQUIRE(p.bulges.empty()); // straight cuts, no arcs
    REQUIRE(has_pt(p.points, {95, 0}));  // 5 back along the incoming edge at (100,0)
    REQUIRE(has_pt(p.points, {100, 10})); // 10 along the outgoing edge
    reset_defaults();
}

TEST_CASE("#33: an oversized fillet falls back to square corners") {
    reset_defaults();
    ProcHarness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("F");
    h.proc.submit_line("40"); // needs 80 of a 50-high side
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    REQUIRE(h.cmds.size() == 1);
    REQUIRE(h.poly().points.size() == 4);
    REQUIRE(h.poly().bulges.empty());
    reset_defaults();
}

TEST_CASE("#33: the fillet radius persists for the next rectangle, and Enter keeps it") {
    reset_defaults();
    {
        ProcHarness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("F");
        h.proc.submit_line("5");
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 8);
    }
    {
        ProcHarness h; // a new command in the same session: no option given
        h.proc.submit_line("REC");
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 8); // still filleted
    }
    {
        ProcHarness h; // Enter at the radius prompt keeps the default
        h.proc.submit_line("REC");
        h.proc.submit_line("F");
        h.proc.submit_line("");
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 8);
    }
    reset_defaults();
}

TEST_CASE("#33: setting a chamfer clears the fillet and vice versa") {
    reset_defaults();
    {
        ProcHarness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("F");
        h.proc.submit_line("5");
        h.proc.submit_line("C");
        h.proc.submit_line("5");
        h.proc.submit_line("5");
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 8);
        REQUIRE(h.poly().bulges.empty()); // chamfered, not filleted
    }
    {
        ProcHarness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("F");
        h.proc.submit_line("0"); // radius 0 = square corners again, and chamfer cleared
        h.proc.submit_line("0,0");
        h.proc.submit_line("100,50");
        REQUIRE(h.poly().points.size() == 4);
    }
    reset_defaults();
}
