// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// REFEDIT / REFSET / REFCLOSE (#25): in-place block editing through a working set.

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;

namespace {
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
struct StubView : musacad::command::ViewControl {
    void zoom_extents() override {}
    void zoom_scale(double) override {}
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    StubView view;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    template <class T>
    const T* last() const {
        const T* found = nullptr;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<T>(&c)) {
                found = p;
            }
        }
        return found;
    }
};
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    e.consume_snapshot();
    return pred(e.snapshot());
}
} // namespace

TEST_CASE("#25 REFEDIT: the working set edits in place; REFCLOSE Save rewrites the definition in its own frame") {
    GeometryEngine engine;
    engine.start();
    // SYM = a line and a circle; one reference at (100, 0), scale 2, rotated 90 degrees.
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddCircleCommand{{0, 0}, 5.0, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(DefineBlockCommand{"SYM", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.block_names.size() == 1; }));
    engine.submit(EraseSelectionCommand{3}); // the in-place insert BLOCK left behind
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(InsertBlockCommand{"SYM", {100, 0}, 2.0, 2.0, kPi / 2.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 2; }));
    const std::size_t one_ref = engine.snapshot().line_vertices.size(); // line + circle facets

    // Open it: the members become the selection (a line (100,0)-(100,20), a circle r 10).
    engine.submit(RefEditCommand{{100, 10}, 1.0, 5});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 2 && s.selection[0].kind != EntityKind::Insert;
    }));
    // Edit: erase the circle, add a line and put it into the working set.
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{110, 0}, 0.5, false}); // the circle's rightmost point
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Circle;
    }));
    engine.submit(EraseSelectionCommand{6});
    engine.submit(AddLineCommand{{100, 0}, {80, 0}, 7});
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{90, 0}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Line;
    }));
    engine.submit(RefSetCommand{true, 8});
    engine.submit(RefCloseCommand{true, 9});
    // Saved: the reference is back (the selection), drawn from the new definition --
    // two lines and no circle: 4 line vertices.
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Insert &&
               s.line_vertices.size() == 4;
    }));
    // The definition itself changed: a fresh reference at scale 1 shows the two lines in
    // the block's frame. The world line (100,0)-(80,0) maps back through the inverse of
    // "scale 2, rotate 90" to (0,0)-(0,10); the kept one to (0,0)-(10,0).
    engine.submit(InsertBlockCommand{"SYM", {200, 0}, 1.0, 1.0, 0.0, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    bool has_up_line = false;
    bool has_right_line = false;
    const auto& lv = engine.snapshot().line_vertices;
    for (std::size_t i = 0; i + 1 < lv.size(); i += 2) {
        const Vec2 a = lv[i];
        const Vec2 b = lv[i + 1];
        if (std::abs(a.x - 200.0) < 1e-6 && std::abs(b.x - 200.0) < 1e-6 &&
            std::abs(std::max(a.y, b.y) - 10.0) < 1e-6) {
            has_up_line = true;
        }
        if (std::abs(a.y) < 1e-6 && std::abs(b.y) < 1e-6 && std::abs(std::max(a.x, b.x) - 210.0) < 1e-6) {
            has_right_line = true;
        }
    }
    CHECK(has_up_line);
    CHECK(has_right_line);

    // Undo the save: the working set comes back and the reference goes (the new
    // reference at 200 stays: it was a later group).
    engine.submit(UndoLastGroupCommand{}); // the insert at 200
    engine.submit(UndoLastGroupCommand{}); // the save
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    CHECK(one_ref > 4);
    engine.stop();
}

TEST_CASE("#25 REFCLOSE Discard drops the working-set changes and restores the reference; guards") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddLineCommand{{0, 0}, {0, 10}, 1});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(DefineBlockCommand{"L2", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Insert;
    }));
    engine.submit(RefCloseCommand{true, 3}); // nothing open: refused, nothing changes
    engine.submit(RefEditCommand{{5, 0}, 1.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(RefEditCommand{{5, 0}, 1.0, 5}); // already editing: refused
    engine.submit(EraseSelectionCommand{6});       // wipe the whole working set
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(RefCloseCommand{false, 7});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.line_vertices.size() == 4 && s.selection.size() == 1 &&
               s.selection[0].kind == EntityKind::Insert;
    }));
    // A non-uniformly scaled reference cannot be opened.
    engine.submit(InsertBlockCommand{"L2", {50, 0}, 2.0, 1.0, 0.0, 8});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    engine.submit(RefEditCommand{{60, 0}, 1.0, 9});
    engine.submit(AddLineCommand{{100, 0}, {110, 0}, 10});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 10; }));
    engine.stop();
}

TEST_CASE("#25 commands: REFEDIT picks, REFSET Add/Remove with a selection phase, REFCLOSE Save/Discard") {
    ProcHarness h;
    h.proc.submit_line("REFEDIT");
    h.proc.submit_line("12,3");
    const auto* re = h.last<RefEditCommand>();
    REQUIRE(re != nullptr);
    CHECK(re->pick == Vec2{12, 3});

    h.proc.set_selection_count(1);
    h.proc.submit_line("REFSET");
    h.proc.submit_line("R");
    h.proc.submit_line("");
    const auto* rs = h.last<RefSetCommand>();
    REQUIRE(rs != nullptr);
    CHECK_FALSE(rs->add);
    h.proc.submit_line("REFSET");
    h.proc.submit_line("");
    h.proc.submit_line("4,4"); // picks add to the selection
    h.proc.submit_line("");
    const auto* rs2 = h.last<RefSetCommand>();
    REQUIRE(rs2 != nullptr);
    CHECK(rs2->add);
    CHECK(h.last<SelectPickCommand>() != nullptr);

    h.proc.submit_line("REFCLOSE");
    h.proc.submit_line("D");
    const auto* rc = h.last<RefCloseCommand>();
    REQUIRE(rc != nullptr);
    CHECK_FALSE(rc->save);
    h.proc.submit_line("REFCLOSE");
    h.proc.submit_line("");
    CHECK(h.last<RefCloseCommand>()->save);
}
