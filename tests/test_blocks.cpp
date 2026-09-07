// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #25 (BLOCK / INSERT / WBLOCK / REGEN) and issue #32 (editable geometry in the
// properties palette).

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
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/properties_registry.hpp"

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
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string&) override {}
    std::vector<std::string> lines;
};
struct ProcHarness {
    std::vector<Command> cmds;
    SilentOutput out;
    musacad::command::CommandProcessor proc{
        [this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
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
PropertyValue numv(double n) {
    PropertyValue v;
    v.num = n;
    return v;
}
} // namespace

TEST_CASE("#25 BLOCK from a selection keeps the geometry in place, replaces it with an insert; INSERT places copies; WBLOCK writes it") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddCircleCommand{{5, 5}, 2.0, 2});
    engine.submit(AddPointCommand{{50, 50}, 3, {}}); // a kind a block cannot hold yet
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10 && s.points.size() == 1; }));
    engine.consume_snapshot();
    const Vec2 bmax = engine.snapshot().bounds_max;
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));
    engine.submit(DefineBlockCommand{"SYM", {0, 0}, 4});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Block \"SYM\" defined with 2 objects (1 unsupported left in place).") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(s.block_names.size() == 1);
    REQUIRE(s.block_names[0] == "SYM");
    // The insert draws the same strokes where they were: same extents.
    REQUIRE(s.bounds_max.x == Approx(bmax.x).margin(1e-6));
    REQUIRE(s.bounds_max.y == Approx(bmax.y).margin(1e-6));
    // ...and the selection is now the one insert.
    REQUIRE(s.selection.size() == 1);
    REQUIRE(s.selection[0].kind == EntityKind::Insert);

    // Undo restores the originals (one group).
    engine.submit(UndoLastGroupCommand{});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s2) {
        return s2.selection.size() == 3 && s2.selection[0].kind != EntityKind::Insert;
    }));
    engine.submit(RedoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s2) { return s2.block_names.size() == 1; }));

    // INSERT by name at (100, 0), scale 2: the line's copy ends at x = 100 + 10*2.
    engine.submit(InsertBlockCommand{"SYM", {100, 0}, 2.0, 2.0, 0.0, 5});
    REQUIRE(wait_until(engine, [](const auto& s2) { return s2.bounds_max.x > 119.9; }));
    REQUIRE(engine.snapshot().bounds_max.x == Approx(120.0).margin(0.05));
    engine.submit(InsertBlockCommand{"NOPE", {0, 0}, 1.0, 1.0, 0.0, 6});
    REQUIRE(wait_until(engine, [](const auto& s2) { return s2.status.find("not found") != std::string::npos; }));

    // WBLOCK: the block as its own drawing, base point at the origin.
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_wblock.musa";
    engine.submit(WriteBlockCommand{"SYM", p.string()});
    REQUIRE(wait_until(engine, [](const auto& s2) { return s2.status.rfind("Wrote 2 objects", 0) == 0; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    REQUIRE(doc.lines.size() == 1);
    REQUIRE(doc.circles.size() == 1);
    REQUIRE(doc.circles[0].center == Vec2{5, 5});
    std::filesystem::remove(p);

    engine.submit(RegenCommand{});
    REQUIRE(wait_until(engine, [](const auto& s2) { return s2.status == "Regenerating model."; }));
    engine.stop();
}

TEST_CASE("#25 BLOCK / INSERT / WBLOCK command flows") {
    {
        ProcHarness h;
        h.proc.set_selection_count(2);
        h.proc.submit_line("-BLOCK");
        h.proc.submit_line("TITLE");
        h.proc.submit_line("10,20");
        h.proc.submit_line("");
        const auto* d = h.last<DefineBlockCommand>();
        REQUIRE(d != nullptr);
        REQUIRE(d->name == "TITLE");
        REQUIRE(d->base == Vec2{10, 20});
    }
    {
        ProcHarness h;
        h.proc.set_block_names({"TITLE", "BOLT"});
        h.proc.submit_line("I");
        h.proc.submit_line("BOLT");
        h.proc.submit_line("5,5");
        h.proc.submit_line("2");
        h.proc.submit_line("");
        h.proc.submit_line("90");
        const auto* i = h.last<InsertBlockCommand>();
        REQUIRE(i != nullptr);
        REQUIRE(i->name == "BOLT");
        REQUIRE(i->pos == Vec2{5, 5});
        REQUIRE(i->scale_x == Approx(2.0));
        REQUIRE(i->scale_y == Approx(2.0)); // "use X scale factor"
        REQUIRE(i->rotation == Approx(kHalfPi));
        // An unknown name is refused at the prompt.
        h.proc.submit_line("INSERT");
        h.proc.submit_line("NOPE");
        REQUIRE(h.proc.has_active_command());
    }
    {
        ProcHarness h;
        h.proc.submit_line("WBLOCK");
        h.proc.submit_line("TITLE");
        h.proc.submit_line("/tmp/x.musa");
        const auto* w = h.last<WriteBlockCommand>();
        REQUIRE(w != nullptr);
        REQUIRE(w->name == "TITLE");
        REQUIRE(w->path == "/tmp/x.musa");
        h.proc.submit_line("W");
        h.proc.submit_line("");
        h.proc.submit_line("/tmp/all.musa");
        REQUIRE(h.last<WriteBlockCommand>()->name.empty()); // the whole drawing
    }
}

TEST_CASE("#32 properties palette: Center X/Y, Radius and End X/Y are editable and re-create the entity") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{0, 0}, 5.0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 10; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SetPropertyCommand{PropertyId::GeomCenterX, numv(50.0), 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.x > 54.9; }));
    engine.submit(SetPropertyCommand{PropertyId::GeomRadius, numv(10.0), 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.x > 59.9; }));
    REQUIRE(engine.snapshot().bounds_min.x == Approx(40.0).margin(0.05));
    engine.submit(SetPropertyCommand{PropertyId::GeomRadius, numv(-1.0), 4}); // refused: unchanged
    engine.submit(RegenCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Regenerating model."; }));
    REQUIRE(engine.snapshot().bounds_max.x == Approx(60.0).margin(0.05));

    engine.submit(NewDocumentCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 5});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(SetPropertyCommand{PropertyId::GeomEndX, numv(30.0), 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.x > 29.9; }));
    engine.submit(SetPropertyCommand{PropertyId::GeomStartY, numv(-7.0), 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_min.y < -6.9; }));
    // The summary reports the edited numbers.
    bool saw = false;
    for (const auto& f : engine.snapshot().selection_summary.fields) {
        if (f.id == PropertyId::GeomEndX) {
            saw = f.value.num == Approx(30.0);
        }
    }
    REQUIRE(saw);
    engine.stop();
}
