// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// EXPLODE (issue #25, partial). What each compound object becomes follows AutoCAD's
// table; simple objects are refused and counted; the whole thing is one undo group.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include <filesystem>

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
bool has_vertex_near(const RenderSnapshot& s, Vec2 p, double tol) {
    return std::any_of(s.line_vertices.begin(), s.line_vertices.end(),
                       [&](const Vec2& v) { return length(v - p) <= tol; });
}
bool has_text(const RenderSnapshot& s, const std::string& t) {
    return std::any_of(s.text_edit_targets.begin(), s.text_edit_targets.end(),
                       [&](const TextEditTarget& x) { return x.content == t; });
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
};
} // namespace

TEST_CASE("#25: a polyline explodes into lines and arcs") {
    // Open polyline with a semicircular bulge on its second segment.
    GeometryEngine engine;
    engine.start();
    AddPolylineCommand pc;
    pc.points = {{0, 0}, {100, 0}, {100, 50}};
    pc.bulges = {0.0, 1.0, 0.0}; // bulge 1 = 180 degrees, CCW from (100,0) to (100,50)
    pc.group = 1;
    engine.submit(pc);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(ExplodeSelectionCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Exploded 1 object into 2.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(s.selection.size() == 2); // the two components stay selected
    REQUIRE(has_vertex_near(s, {0, 0}, 1e-9));    // the line
    REQUIRE(has_vertex_near(s, {100, 0}, 1e-9));
    // Bulge +1 is a CCW half circle from (100,0) to (100,50): its apex is to the RIGHT of
    // the upward chord, at (125,25) -- the same side an AutoCAD positive bulge lands on.
    REQUIRE(has_vertex_near(s, {125, 25}, 0.6));
    engine.stop();
}

TEST_CASE("#25: a dimension explodes into lines, arrowhead solids and text") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                      {0, 0}, {100, 0}, {50, 20}, 0, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));

    engine.submit(ExplodeSelectionCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Exploded 1 object into") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(s.selection.size() >= 4);     // ext lines, dim line, arrows, text at least
    REQUIRE(has_text(s, "100.00"));       // the label is now a TEXT entity
    REQUIRE(!s.fill_vertices.empty());    // filled arrowheads became SOLID hatches
    engine.stop();
}

TEST_CASE("#25: a SOLID hatch explodes into its boundary; a pattern hatch into lines") {
    GeometryEngine engine;
    engine.start();
    AddHatchCommand solid;
    solid.loops = {{{0, 0}, {100, 0}, {100, 50}, {0, 50}}};
    solid.pattern_name = "SOLID";
    solid.group = 1;
    engine.submit(solid);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.fill_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ExplodeSelectionCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Exploded 1 object into 1.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().fill_vertices.empty());     // the fill is gone
    REQUIRE(engine.snapshot().line_vertices.size() == 8); // a closed 4-vertex boundary

    engine.submit(NewDocumentCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    AddHatchCommand pat;
    pat.loops = {{{0, 0}, {100, 0}, {100, 50}, {0, 50}}};
    pat.pattern_name = "ANSI31";
    pat.pattern_scale = 5.0;
    pat.group = 3;
    engine.submit(pat);
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    const std::size_t drawn = engine.snapshot().line_vertices.size();
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ExplodeSelectionCommand{4});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Exploded 1 object into") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().selection.size() > 1);            // one LINE per pattern stroke
    REQUIRE(engine.snapshot().line_vertices.size() == drawn);    // the same strokes, now lines
    engine.stop();
}

TEST_CASE("#25: MTEXT explodes into one TEXT per laid-out line") {
    GeometryEngine engine;
    engine.start();
    AddMTextCommand mc;
    mc.block.pos = {0, 100};
    mc.block.height = 5.0;
    mc.content = "FIRST\nSECOND";
    mc.group = 1;
    engine.submit(mc);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.text_edit_targets.size() == 1; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ExplodeSelectionCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Exploded 1 object into 2.") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_text(engine.snapshot(), "FIRST"));
    REQUIRE(has_text(engine.snapshot(), "SECOND"));
    engine.stop();
}

TEST_CASE("#25: a table explodes into its grid lines and cell text") {
    GeometryEngine engine;
    engine.start();
    AddTableCommand tc;
    tc.rows = 2;
    tc.cols = 2;
    tc.cells.assign(4, TableCell{});
    tc.texts = {"ITEM", "", "", "QTY"};
    tc.col_widths = {40.0, 40.0};
    tc.row_heights = {8.0, 8.0};
    tc.pos = {0.0, 100.0};
    tc.group = 1;
    engine.submit(tc);
    REQUIRE(wait_until(engine, [](const auto& s) { return s.text_edit_targets.size() == 4; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ExplodeSelectionCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Exploded 1 object into") != std::string::npos;
    }));
    engine.consume_snapshot();
    REQUIRE(has_text(engine.snapshot(), "ITEM"));
    REQUIRE(has_text(engine.snapshot(), "QTY"));
    REQUIRE(engine.snapshot().selection.size() >= 8); // 6 grid lines + 2 texts at least
    engine.stop();
}

TEST_CASE("#25: a block reference explodes one level into its members, transformed") {
    // A block "B" of one line (0,0)-(10,0) with base (0,0), inserted at (100,50) at scale 2
    // rotated 90 degrees: the exploded line runs from (100,50) to (100,70).
    io::Document doc;
    io::DocBlockDef bd;
    bd.name = "B";
    bd.lines.push_back(io::DocLine{{0, 0}, {10, 0}, EntityProps{}, 1.0});
    doc.block_defs.push_back(bd);
    doc.inserts.push_back(io::DocInsert{"B", {100, 50}, 2.0, 2.0, kPi / 2.0, EntityProps{}});
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "musacad_explode_block.musa";
    REQUIRE(io::save_native(doc, path.string()).ok);

    GeometryEngine engine;
    engine.start();
    engine.submit(OpenDocumentCommand{path.string(), false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ExplodeSelectionCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Exploded 1 object into 1.") != std::string::npos;
    }));
    engine.consume_snapshot();
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(s.selection.size() == 1);
    REQUIRE(s.selection[0].kind == EntityKind::Line); // a LINE now, not an INSERT
    REQUIRE(has_vertex_near(s, {100, 50}, 1e-6));
    REQUIRE(has_vertex_near(s, {100, 70}, 1e-6));
    engine.stop();
    std::filesystem::remove(path);
}

TEST_CASE("#25: simple objects are refused, and a mixed selection reports both counts") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ExplodeSelectionCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("only simple objects") != std::string::npos;
    }));

    engine.submit(AddPolylineCommand{{{0, 10}, {10, 10}, {10, 20}}, false, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 6; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(ExplodeSelectionCommand{4});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status.find("Exploded 1 object into 2. 1 could not be exploded.") !=
               std::string::npos;
    }));
    engine.stop();
}

TEST_CASE("#25: EXPLODE is one undo group") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddPolylineCommand{{{0, 0}, {100, 0}, {100, 50}, {0, 50}}, true, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 8; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(ExplodeSelectionCommand{2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 4; }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.line_vertices.size() == 8 && s.selection.size() <= 1;
    }));
    engine.stop();
}

TEST_CASE("#25: the EXPLODE flows -- Select objects + Enter, and a pre-selected set") {
    {
        ProcHarness h;
        h.proc.set_selection_count(0);
        h.proc.submit_line("X");
        REQUIRE(h.proc.in_selection_phase());
        h.proc.set_selection_count(2);
        h.proc.submit_line("");
        REQUIRE(std::get_if<ExplodeSelectionCommand>(&h.cmds.back()) != nullptr);
        REQUIRE(!h.proc.has_active_command());
    }
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("EXPLODE");
        REQUIRE(std::get_if<ExplodeSelectionCommand>(&h.cmds.back()) != nullptr);
        REQUIRE(!h.proc.has_active_command());
    }
    {
        ProcHarness h;
        h.proc.set_selection_count(0);
        h.proc.submit_line("X");
        h.proc.submit_line("");
        REQUIRE(h.cmds.empty());
        REQUIRE(!h.proc.has_active_command());
    }
}
