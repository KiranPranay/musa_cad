// STRETCH (issue #24): move the stored points inside a crossing window, leaving the rest
// of each entity anchored. The behaviour that distinguishes it from MOVE is that a
// PARTLY-enclosed entity is deformed, not translated -- and that a dimension whose def
// points move RE-MEASURES, which this model gets for free.

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
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
} // namespace

TEST_CASE("#24: engine stretch moves only the enclosed endpoint") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    // Crossing window over the right end only; drag it +20 in x.
    engine.submit(StretchSelectionCommand{{90, -10}, {110, 10}, {20, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& snap = engine.snapshot();
    REQUIRE(snap.line_vertices.size() == 2);
    // The anchored end stayed; the enclosed end moved.
    double minx = 1e9;
    double maxx = -1e9;
    for (const Vec2& v : snap.line_vertices) {
        minx = std::min(minx, v.x);
        maxx = std::max(maxx, v.x);
    }
    REQUIRE(minx == Approx(0.0));
    REQUIRE(maxx == Approx(120.0));
}

TEST_CASE("#24: a fully-enclosed entity moves entirely") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(StretchSelectionCommand{{-5, -5}, {15, 5}, {0, 30}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched") != std::string::npos;
    }));
    engine.consume_snapshot();
    for (const Vec2& v : engine.snapshot().line_vertices) {
        REQUIRE(v.y == Approx(30.0)); // both endpoints moved
    }
}

TEST_CASE("#24: an entity outside the window is untouched, and says so") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(StretchSelectionCommand{{500, 500}, {600, 600}, {5, 5}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Nothing in the crossing window") != std::string::npos;
    }));
    engine.consume_snapshot();
    // Geometry unchanged -- no churn, and the engine reported the truth rather than
    // echoing a guessed success (the Ph10.1 honest-feedback rule).
    for (const Vec2& v : engine.snapshot().line_vertices) {
        REQUIRE(v.y == Approx(0.0));
    }
}

TEST_CASE("#24: a circle MOVES when its centre is enclosed and is never deformed") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 10.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t before = engine.snapshot().line_vertices.size();

    engine.submit(StretchSelectionCommand{{-1, -1}, {1, 1}, {50, 0}, 2}); // encloses the centre
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s2 = engine.snapshot();
    // Same tessellation count => still a circle of the same radius, just relocated.
    REQUIRE(s2.line_vertices.size() == before);
    double cx = 0.0;
    for (const Vec2& v : s2.line_vertices) {
        cx += v.x;
    }
    cx /= static_cast<double>(s2.line_vertices.size());
    REQUIRE(cx == Approx(50.0).margin(0.5));
}

TEST_CASE("#24: a stretched feature's dimension follows it (def points move)") {
    // The headline reason STRETCH matters here: a dimension stores DEF POINTS and computes
    // its value from them (Ph13/Ph15), so moving the enclosed def point re-measures with no
    // special case in the stretch code. Observed through the snapshot, because the engine
    // deliberately does not expose the store.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {100, 0}, {50, -10}, 0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().bounds_max.x == Approx(100.0).margin(2.0));

    // The window encloses the right end of BOTH the line and the dimension.
    engine.submit(StretchSelectionCommand{{90, -20}, {130, 20}, {25, 0}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched 2") != std::string::npos; // line + dimension
    }));
    engine.consume_snapshot();
    // The dimension's own geometry now reaches the new extent, i.e. its def point moved
    // with the feature rather than staying behind.
    REQUIRE(engine.snapshot().bounds_max.x == Approx(125.0).margin(2.0));
}

TEST_CASE("#24: stretch is ONE undo group -- Ctrl+Z restores everything") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {100, 0}, 1});
    engine.submit(AddLineCommand{{0, 10}, {100, 10}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));

    engine.submit(StretchSelectionCommand{{90, -20}, {130, 20}, {30, 0}, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched 2") != std::string::npos;
    }));
    engine.consume_snapshot();
    double maxx = -1e9;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        maxx = std::max(maxx, v.x);
    }
    REQUIRE(maxx == Approx(130.0));

    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        double m = -1e9;
        for (const Vec2& v : s.line_vertices) {
            m = std::max(m, v.x);
        }
        return s.line_vertices.size() == 4 && m < 100.5;
    }));
}

// ---------------------------------------------------------------------------
// The GUI path (issue #24 follow-up). The engine rules above were always correct;
// what broke STRETCH in the app was the PICK path in front of them. A crossing
// window is a screen region, not geometry, so AutoCAD applies neither object snap
// nor ortho/polar to its corners. Musa CAD applied both:
//
//   * ORTHO collapsed the second corner onto an axis through the first, so the
//     window had ZERO AREA and caught nothing -- the user drags a box over half a
//     rectangle and nothing happens. This is the defect the tests below pin.
//   * OSNAP yanked a corner onto a vertex of the very object being windowed,
//     silently changing which vertices fell inside.
//
// Driven through CommandProcessor (the same entry point the viewport uses) so the
// window corners are exercised exactly as a mouse pick delivers them.
// ---------------------------------------------------------------------------

#include "musacad/command/command_processor.hpp"

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

    /// The last StretchSelectionCommand the processor emitted.
    [[nodiscard]] const StretchSelectionCommand& stretch() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* s = std::get_if<StretchSelectionCommand>(&*it)) {
                return *s;
            }
        }
        throw std::runtime_error("no StretchSelectionCommand was submitted");
    }
};
} // namespace

TEST_CASE("#24: ORTHO must not collapse the crossing window") {
    ProcHarness h;
    h.proc.set_ortho(true); // the setting that silently broke STRETCH
    h.proc.submit_line("S");
    // Drag a window over the right half of a 0,0..100,50 rectangle. Under the old
    // code the second corner became {130, -10} (y forced to the first corner's y),
    // giving a zero-height window.
    h.proc.pick_point(Vec2{50.0, -10.0}, std::nullopt);
    h.proc.pick_point(Vec2{130.0, 60.0}, std::nullopt);
    h.proc.pick_point(Vec2{100.0, 0.0}, std::nullopt); // base
    h.proc.pick_point(Vec2{130.0, 0.0}, std::nullopt); // displacement (+30 in x)

    const StretchSelectionCommand& s = h.stretch();
    REQUIRE(s.win_min.x == Approx(50.0));
    REQUIRE(s.win_min.y == Approx(-10.0));
    REQUIRE(s.win_max.x == Approx(130.0));
    REQUIRE(s.win_max.y == Approx(60.0)); // the corner ortho used to flatten
    REQUIRE((s.win_max.y - s.win_min.y) > 1.0); // the window has real area
    REQUIRE(s.delta.x == Approx(30.0));
    REQUIRE(s.delta.y == Approx(0.0)); // ortho DOES still apply to the displacement
}

TEST_CASE("#24: OSNAP must not pull a crossing-window corner onto geometry") {
    ProcHarness h;
    h.proc.submit_line("S");
    // The viewport offers a snap point for each corner (a vertex of the rectangle
    // being windowed). Window corners must ignore it and take the raw cursor.
    h.proc.pick_point(Vec2{50.0, -10.0}, Vec2{100.0, 0.0});
    h.proc.pick_point(Vec2{130.0, 60.0}, Vec2{100.0, 50.0});
    h.proc.pick_point(Vec2{100.0, 0.0}, std::nullopt);
    h.proc.pick_point(Vec2{130.0, 0.0}, std::nullopt);

    const StretchSelectionCommand& s = h.stretch();
    REQUIRE(s.win_min == Vec2{50.0, -10.0});
    REQUIRE(s.win_max == Vec2{130.0, 60.0});
}

TEST_CASE("#24: the base and displacement picks still honour OSNAP") {
    // The bypass is scoped to the two window corners -- it must not leak into the
    // coordinate picks that follow, or STRETCH would lose snapping entirely.
    ProcHarness h;
    h.proc.submit_line("S");
    h.proc.pick_point(Vec2{50.0, -10.0}, std::nullopt);
    h.proc.pick_point(Vec2{130.0, 60.0}, std::nullopt);
    h.proc.pick_point(Vec2{99.0, 1.0}, Vec2{100.0, 0.0});  // snaps to the corner
    h.proc.pick_point(Vec2{131.0, 1.0}, Vec2{130.0, 0.0}); // snaps to a target
    REQUIRE(h.stretch().delta == Vec2{30.0, 0.0});
}

TEST_CASE("#24: half a rectangle stretches, the other half stays -- end to end") {
    // The exact scenario STRETCH is judged by: window the right half of a closed
    // rectangle, drag +30 in x, and the left edge must not move while the right
    // edge lands at 130. Run through the processor AND the engine together.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {100, 0}, {100, 50}, {0, 50}}, true, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    ProcHarness h;
    h.proc.set_ortho(true); // the realistic drafting setup that used to defeat it
    h.proc.submit_line("S");
    h.proc.pick_point(Vec2{50.0, -10.0}, std::nullopt);
    h.proc.pick_point(Vec2{130.0, 60.0}, std::nullopt);
    h.proc.pick_point(Vec2{100.0, 0.0}, std::nullopt);
    h.proc.pick_point(Vec2{130.0, 0.0}, std::nullopt);
    engine.submit(h.stretch());
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Stretched") != std::string::npos;
    }));
    engine.consume_snapshot();

    double minx = 1e9;
    double maxx = -1e9;
    double miny = 1e9;
    double maxy = -1e9;
    for (const Vec2& v : engine.snapshot().line_vertices) {
        minx = std::min(minx, v.x);
        maxx = std::max(maxx, v.x);
        miny = std::min(miny, v.y);
        maxy = std::max(maxy, v.y);
    }
    REQUIRE(minx == Approx(0.0));   // the un-windowed half stayed put
    REQUIRE(maxx == Approx(130.0)); // the windowed half moved to the picked point
    REQUIRE(miny == Approx(0.0));   // height untouched: it was a horizontal drag
    REQUIRE(maxy == Approx(50.0));
}
