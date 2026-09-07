// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// ELLIPSE (issue #23): a real ellipse entity -- centre, major half-axis, ratio and a
// counter-clockwise parameter range -- with the AutoCAD command's every method. The
// tests pin the geometric rules (parameter <-> angle, axis swap, mirror reversing the
// range), the editing machinery, persistence in both formats, and the prompts.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/ellipse.hpp"
#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

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
    const AddEllipseCommand* only() const {
        const AddEllipseCommand* found = nullptr;
        int n = 0;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<AddEllipseCommand>(&c)) {
                found = p;
                ++n;
            }
        }
        return n == 1 ? found : nullptr;
    }
};
EllipseData make(Vec2 c, Vec2 major, double ratio, double s = 0.0, double e = kTwoPi) {
    EllipseData d;
    d.center = c;
    d.major = major;
    d.ratio = ratio;
    d.start = s;
    d.end = e;
    return d;
}
bool near(Vec2 a, Vec2 b, double tol = 1e-6) {
    return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
}
} // namespace

TEST_CASE("#23 ELLIPSE: the parametric definition, angle<->parameter, and bounds") {
    const EllipseData e = make({10, 5}, {20, 0}, 0.5); // a=20, b=10, axis-aligned
    REQUIRE(near(ellipse::point_at(e, 0.0), {30, 5}));
    REQUIRE(near(ellipse::point_at(e, kHalfPi), {10, 15})); // counter-clockwise
    REQUIRE(ellipse::is_full(e));
    REQUIRE(ellipse::sweep_of(e) == Approx(kTwoPi));

    // The parameter of a point is that of the centre->point ray (not its angle).
    REQUIRE(ellipse::param_of(e, {30, 5}) == Approx(0.0).margin(1e-9));
    REQUIRE(ellipse::param_of(e, {10, 100}) == Approx(kHalfPi));
    // Angle 45 deg from the major axis with ratio 0.5: tan t = tan 45 / 0.5 = 2.
    REQUIRE(ellipse::angle_to_param(kPi / 4.0, 0.5) == Approx(std::atan(2.0)));
    // ...and the point at that parameter really lies on the 45-degree ray.
    const Vec2 p = ellipse::point_at(e, ellipse::angle_to_param(kPi / 4.0, 0.5));
    REQUIRE((p.y - e.center.y) == Approx(p.x - e.center.x));

    Vec2 mn;
    Vec2 mx;
    ellipse::bounds(e, mn, mx);
    REQUIRE(near(mn, {-10, -5}));
    REQUIRE(near(mx, {30, 15}));
    // A rotated ellipse's box comes from its true extreme points, not just samples.
    const double c45 = std::cos(kPi / 4.0);
    const EllipseData r = make({0, 0}, {20 * c45, 20 * c45}, 0.5);
    ellipse::bounds(r, mn, mx);
    const double half = std::sqrt(20.0 * 20.0 * c45 * c45 + 10.0 * 10.0 * c45 * c45);
    REQUIRE(mx.x == Approx(half).epsilon(1e-6));
    REQUIRE(mx.y == Approx(half).epsilon(1e-6));

    // An arc's tessellation runs from its start to its end.
    const EllipseData arc = make({0, 0}, {10, 0}, 0.5, 0.0, kHalfPi);
    std::vector<Vec2> pts;
    ellipse::tessellate(arc, 0.01, pts);
    REQUIRE(near(pts.front(), {10, 0}));
    REQUIRE(near(pts.back(), {0, 5}));
    REQUIRE(!ellipse::is_full(arc));
    REQUIRE(ellipse::param_in_range(arc, kPi / 4.0));
    REQUIRE(!ellipse::param_in_range(arc, kPi));
}

TEST_CASE("#23 ELLIPSE: drawn, bounded, pickable at a quadrant and window-selectable") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddEllipseCommand{{0, 0}, {20, 0}, 0.5, 0.0, kTwoPi, 1, {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() >= 16; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().bounds_max.x == Approx(20.0).margin(1e-6));
    REQUIRE(engine.snapshot().bounds_max.y == Approx(10.0).margin(1e-6));

    engine.submit(SelectPickCommand{{0.0, 10.1}, 0.5, false, false}); // the top quadrant
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    REQUIRE(engine.snapshot().selection[0].kind == EntityKind::Ellipse);
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));
    engine.submit(SelectWindowCommand{{-30, -30}, {30, 30}, false, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.stop();
}

TEST_CASE("#23 ELLIPSE: move, rotate, scale keep the shape; mirror reverses an arc's range") {
    // Verified through what the engine PUBLISHES (the drawn extents), the same thing a
    // user sees. Quarter arc from (20,0) to (0,10), counter-clockwise, a=20, b=10.
    GeometryEngine engine;
    engine.start();
    engine.submit(AddEllipseCommand{{0, 0}, {20, 0}, 0.5, 0.0, kHalfPi, 1, {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    const auto bmax = [&]() { return engine.snapshot().bounds_max; };
    const auto bmin = [&]() { return engine.snapshot().bounds_min; };

    engine.submit(MoveSelectionCommand{{5, 5}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.x > 24.9; }));
    REQUIRE(bmax().y == Approx(15.0).margin(0.05)); // (0,10) -> (5,15)

    engine.submit(RotateSelectionCommand{{5, 5}, kHalfPi, 3});
    // The arc (5..25 in x, 5..15 in y) rotated a quarter turn about (5,5): now the major
    // axis points up: x in -5..5, y in 5..25.
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.y > 24.9; }));
    REQUIRE(bmin().x == Approx(-5.0).margin(0.05));
    REQUIRE(bmax().x == Approx(5.0).margin(0.05));

    engine.submit(ScaleSelectionCommand{{5, 5}, 2.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.y > 44.9; }));
    REQUIRE(bmin().x == Approx(-15.0).margin(0.05)); // ratio unchanged: b doubles too

    // Undo the three edits, then mirror across the x-axis with the source erased: the arc
    // that ran from (20,0) up to (0,10) must now run from (20,0) down to (0,-10).
    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.bounds_max.x > 19.9 && s.bounds_max.x < 20.1 && s.bounds_max.y < 10.1 &&
               s.bounds_min.x > -0.1;
    }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(MirrorSelectionCommand{{0, 0}, {1, 0}, true, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_min.y < -9.9; }));
    REQUIRE(bmax().y == Approx(0.0).margin(0.05));   // nothing above the axis any more
    REQUIRE(bmax().x == Approx(20.0).margin(0.05));  // the (20,0) end is still there
    REQUIRE(bmin().x == Approx(0.0).margin(0.05));   // and the arc still spans a quarter
    engine.stop();
}

TEST_CASE("#23 ELLIPSE: grips -- five on a full ellipse, four on an arc; dragging works") {
    GeometryStore s;
    const EntityHandle full = s.add_ellipse({0, 0}, {20, 0}, 0.5, 0.0, kTwoPi);
    std::vector<Grip> g;
    grips_of(s, full, g);
    REQUIRE(g.size() == 5);
    REQUIRE(g[0].kind == GripKind::Move);
    REQUIRE(near(g[1].pos, {20, 0}));
    REQUIRE(near(g[3].pos, {0, 10}));

    // Drag the major end to (0,30): the major axis now points up with length 30 and the
    // minor radius (10) is kept, so the ratio becomes 1/3.
    const Command c = edit_for_grip_drag(s, full, 1, {0, 30});
    const auto* e = std::get_if<AddEllipseCommand>(&c);
    REQUIRE(e != nullptr);
    REQUIRE(near(e->major, {0, 30}));
    REQUIRE(e->ratio == Approx(1.0 / 3.0));
    // Drag a minor end to (5,0)... the minor radius becomes 5 of major 20 -> ratio 0.25.
    const Command c2 = edit_for_grip_drag(s, full, 3, {0, 5});
    REQUIRE(std::get_if<AddEllipseCommand>(&c2)->ratio == Approx(0.25));

    const EntityHandle arc = s.add_ellipse({0, 0}, {20, 0}, 0.5, 0.0, kHalfPi);
    g.clear();
    grips_of(s, arc, g);
    REQUIRE(g.size() == 4);
    REQUIRE(near(g[1].pos, {20, 0}));
    REQUIRE(near(g[2].pos, {0, 10}));
    // Dragging the end grip to the negative x-axis extends the arc to a half.
    const Command c3 = edit_for_grip_drag(s, arc, 2, {-50, 0});
    REQUIRE(std::get_if<AddEllipseCommand>(&c3)->end == Approx(kPi));
}

TEST_CASE("#23 ELLIPSE: round-trips through the native format and DXF; DXF import is a real entity") {
    GeometryStore store;
    store.add_ellipse({1, 2}, {10, 5}, 0.4, 0.0, kTwoPi);
    store.add_ellipse({3, 4}, {0, 8}, 0.75, 0.5, 2.0);
    io::Document doc = io::document_from_store(store);
    REQUIRE(doc.ellipses.size() == 2);

    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_ellipse_rt.musa";
    REQUIRE(io::save_native(doc, p.string()).ok);
    io::Document back;
    REQUIRE(io::load_native(p.string(), back).ok);
    REQUIRE(back.ellipses == doc.ellipses);
    std::filesystem::remove(p);

    const std::filesystem::path d = std::filesystem::temp_directory_path() / "musacad_ellipse.dxf";
    REQUIRE(io::save_dxf(doc, d.string()).ok);
    io::Document dback;
    REQUIRE(io::load_dxf(d.string(), dback).ok);
    REQUIRE(dback.ellipses.size() == 2);
    REQUIRE(dback.polylines.empty()); // no longer tessellated
    bool saw_arc = false;
    for (const io::DocEllipse& e : dback.ellipses) {
        if (e.ratio == Approx(0.75)) {
            REQUIRE(e.start == Approx(0.5));
            REQUIRE(e.end == Approx(2.0));
            REQUIRE(near(e.major, {0, 8}));
            saw_arc = true;
        }
    }
    REQUIRE(saw_arc);
    std::filesystem::remove(d);
}

TEST_CASE("#23 ELLIPSE command: axis-endpoint method, Center method, Rotation, and the axis swap") {
    { // Axis endpoints (0,0)-(20,0), other half-axis 5 -> centre (10,0), a=10, ratio .5
        ProcHarness h;
        h.proc.submit_line("EL");
        h.proc.submit_line("0,0");
        h.proc.submit_line("20,0");
        h.proc.submit_line("5");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(near(e->center, {10, 0}));
        REQUIRE(near(e->major, {10, 0}));
        REQUIRE(e->ratio == Approx(0.5));
        REQUIRE(e->end - e->start == Approx(kTwoPi));
    }
    { // Distance to the other axis given as a point: measured from the centre.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("0,0");
        h.proc.submit_line("20,0");
        h.proc.submit_line("10,3");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(e->ratio == Approx(0.3));
    }
    { // Center method.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("C");
        h.proc.submit_line("10,10");
        h.proc.submit_line("10,30"); // axis endpoint: half-axis 20 along +y
        h.proc.submit_line("8");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(near(e->center, {10, 10}));
        REQUIRE(near(e->major, {0, 20}));
        REQUIRE(e->ratio == Approx(0.4));
    }
    { // The second axis is LONGER: the axes swap so the stored major is the longer one.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("0,0");
        h.proc.submit_line("10,0"); // first half-axis 5 along x
        h.proc.submit_line("8");    // other half-axis 8 -> major is along y, length 8
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(near(e->major, {0, 8}));
        REQUIRE(e->ratio == Approx(5.0 / 8.0));
    }
    { // Rotation: the circle seen at 60 degrees -> ratio cos 60 = 0.5.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("0,0");
        h.proc.submit_line("20,0");
        h.proc.submit_line("R");
        h.proc.submit_line("60");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(e->ratio == Approx(0.5));
        REQUIRE(near(e->major, {10, 0}));
    }
    { // A rotation of 89.4 or more is refused; the command stays at the prompt.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("0,0");
        h.proc.submit_line("20,0");
        h.proc.submit_line("R");
        h.proc.submit_line("89.5");
        REQUIRE(h.only() == nullptr);
        REQUIRE(h.proc.has_active_command());
    }
}

TEST_CASE("#23 ELLIPSE command: Arc by angle, by parameter, and by included angle") {
    { // Start angle 0, end angle 90 (from the first axis): parameters 0 .. pi/2.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("A");
        h.proc.submit_line("0,0");
        h.proc.submit_line("20,0");
        h.proc.submit_line("5");
        h.proc.submit_line("0");
        h.proc.submit_line("90");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(e->start == Approx(0.0).margin(1e-9));
        REQUIRE(e->end == Approx(kHalfPi));
    }
    { // An ANGLE of 45 degrees is not the parameter 45 degrees on a squashed ellipse.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("A");
        h.proc.submit_line("0,0");
        h.proc.submit_line("20,0");
        h.proc.submit_line("5");
        h.proc.submit_line("45");
        h.proc.submit_line("180");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(e->start == Approx(std::atan(2.0))); // tan t = tan45 / 0.5
        REQUIRE(e->end == Approx(kPi));
    }
    { // Parameter mode takes the values verbatim.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("A");
        h.proc.submit_line("0,0");
        h.proc.submit_line("20,0");
        h.proc.submit_line("5");
        h.proc.submit_line("P");
        h.proc.submit_line("45");
        h.proc.submit_line("135");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(e->start == Approx(kPi / 4.0));
        REQUIRE(e->end == Approx(3.0 * kPi / 4.0));
    }
    { // Included angle: start 0, included 180 -> end parameter pi.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("A");
        h.proc.submit_line("C");
        h.proc.submit_line("0,0");
        h.proc.submit_line("10,0");
        h.proc.submit_line("5");
        h.proc.submit_line("0");
        h.proc.submit_line("I");
        h.proc.submit_line("180");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        REQUIRE(e->end == Approx(kPi));
    }
    { // Swapped axes on an arc: angles are still measured from the FIRST axis (x here),
      // so a start angle of 0 must land on the first axis endpoint (5,0), which on the
      // stored (major along y) ellipse is parameter 3pi/2.
        ProcHarness h;
        h.proc.submit_line("ELLIPSE");
        h.proc.submit_line("A");
        h.proc.submit_line("-5,0");
        h.proc.submit_line("5,0");
        h.proc.submit_line("8");
        h.proc.submit_line("0");
        h.proc.submit_line("90");
        const auto* e = h.only();
        REQUIRE(e != nullptr);
        EllipseData d;
        d.center = e->center;
        d.major = e->major;
        d.ratio = e->ratio;
        REQUIRE(near(ellipse::point_at(d, e->start), {5, 0}));
        REQUIRE(near(ellipse::point_at(d, e->end), {0, 8}));
    }
}
