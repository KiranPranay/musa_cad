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
