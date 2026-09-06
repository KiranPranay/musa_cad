// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// REVCLOUD (issue #33). A cloud is a closed polyline of outward arcs; Rectangular and
// Polygonal build it from points, Object converts an existing curve (replacing it), and
// Reverse direction flips the lobes. Lobe geometry is core::polyline_ops, tested there.

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
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
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
};
} // namespace

TEST_CASE("#33: REVCLOUD Rectangular makes a closed cloud of outward arcs") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("A");   // arc length
    h.proc.submit_line("25");
    h.proc.submit_line("25");
    h.proc.submit_line("R");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,50");
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() == 12); // 4 + 2 + 4 + 2 lobes of 25
    REQUIRE(p->bulges.size() == 12);
    for (double b : p->bulges) {
        REQUIRE(b == Approx(std::tan(kPi / 6.0))); // outward (CCW arcs) for a CCW loop
    }
}

TEST_CASE("#33: REVCLOUD Polygonal closes on Enter and needs three points") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("A");
    h.proc.submit_line("20");
    h.proc.submit_line("20");
    h.proc.submit_line("P");
    h.proc.submit_line("0,0");
    h.proc.submit_line("100,0");
    h.proc.submit_line("");   // only two points: refused, still open
    REQUIRE(h.last<AddPolylineCommand>() == nullptr);
    REQUIRE(h.proc.has_active_command());
    h.proc.submit_line("50,80");
    h.proc.submit_line("");   // closes
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
    REQUIRE(p->points.size() > 3);
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#33: a point at the main prompt starts a clicked path (the Freehand type)") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("A");
    h.proc.submit_line("30");
    h.proc.submit_line("30");
    h.proc.submit_line("0,0");
    h.proc.submit_line("60,0");
    h.proc.submit_line("60,60");
    h.proc.submit_line("");
    const auto* p = h.last<AddPolylineCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->closed);
}

TEST_CASE("#33: REVCLOUD Object converts a circle into a closed cloud and can reverse it") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 50.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    RevcloudObjectCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.arc_len = 30.0;
    c.group = 2;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Converted to a revision cloud of") != std::string::npos;
    }));
    engine.consume_snapshot();
    // The lobes reach outside the original circle: the drawn geometry extends past r=50.
    const auto max_r = [](const RenderSnapshot& s) {
        double m = 0.0;
        for (const Vec2& v : s.line_vertices) {
            m = std::max(m, length(v));
        }
        return m;
    };
    REQUIRE(max_r(engine.snapshot()) > 51.0);
    REQUIRE(engine.snapshot().selection.size() == 1); // the cloud is selected

    engine.submit(RevcloudReverseCommand{3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Reversed the arc direction.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(max_r(engine.snapshot()) < 50.5); // lobes now point inward

    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return std::abs(s.bounds_max.x - 50.0) < 0.5 && std::abs(s.bounds_min.x + 50.0) < 0.5;
    }));
    engine.stop();
}

TEST_CASE("#33: REVCLOUD Object on a line makes an open cloud; a miss says so") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    RevcloudObjectCommand miss;
    miss.pick = {500.0, 500.0};
    miss.pick_radius = 1.0;
    miss.arc_len = 20.0;
    miss.group = 2;
    engine.submit(miss);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("no object under the pick") != std::string::npos;
    }));

    RevcloudObjectCommand c;
    c.pick = {50.0, 0.0};
    c.pick_radius = 1.0;
    c.arc_len = 20.0;
    c.group = 3;
    engine.submit(c);
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Converted to a revision cloud of") != std::string::npos;
    }));
    engine.consume_snapshot();
    double top = 0.0;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        top = std::max(top, std::abs(v.y));
    }
    REQUIRE(top > 1.0); // lobes stand off the line
    engine.stop();
}

TEST_CASE("#33: the REVCLOUD Object flow asks about reversing, and Enter defaults to No") {
    ProcHarness h;
    h.proc.submit_line("REVCLOUD");
    h.proc.submit_line("");     // <Object> default
    h.proc.submit_line("50,0"); // pick
    REQUIRE(h.last<RevcloudObjectCommand>() != nullptr);
    REQUIRE(h.proc.has_active_command()); // "Reverse direction [Yes/No] <No>:"
    h.proc.submit_line("");
    REQUIRE(h.last<RevcloudReverseCommand>() == nullptr);
    REQUIRE(!h.proc.has_active_command());

    ProcHarness g;
    g.proc.submit_line("REVCLOUD");
    g.proc.submit_line("O");
    g.proc.submit_line("50,0");
    g.proc.submit_line("Y");
    REQUIRE(g.last<RevcloudReverseCommand>() != nullptr);
}

TEST_CASE("#33: REVCLOUD arc lengths are validated and remembered") {
    {
        ProcHarness h;
        h.proc.submit_line("REVCLOUD");
        h.proc.submit_line("A");
        h.proc.submit_line("-1"); // refused
        REQUIRE(h.proc.has_active_command());
        h.proc.submit_line("10");
        h.proc.submit_line("5"); // below the minimum: refused
        h.proc.submit_line("10");
        h.proc.submit_line("R");
        h.proc.submit_line("0,0");
        h.proc.submit_line("40,10");
        const auto* p = h.last<AddPolylineCommand>();
        REQUIRE(p != nullptr);
        REQUIRE(p->points.size() == 10); // 4 + 1 + 4 + 1 lobes of 10
    }
    {
        ProcHarness h; // the session keeps the last arc length
        h.proc.submit_line("REVCLOUD");
        h.proc.submit_line("R");
        h.proc.submit_line("0,0");
        h.proc.submit_line("40,10");
        REQUIRE(h.last<AddPolylineCommand>()->points.size() == 10);
    }
}
