// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Block attributes (#25): ATTDEF entities, BLOCK folding them into attributes, INSERT
// values, ATTDISP, ATTEDIT, EXPLODE, and the native / DXF forms.

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

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
AddAttDefCommand attdef(Vec2 pos, const char* tag, const char* prompt, const char* def,
                        std::uint8_t flags, std::uint64_t group) {
    AddAttDefCommand c;
    c.text.pos = pos;
    c.text.height = 2.5;
    c.text.content = tag;
    c.prompt = prompt;
    c.def = def;
    c.flags = flags;
    c.group = group;
    return c;
}
} // namespace

TEST_CASE("#25 store: ATTDEF entity strings and INSERT attribute values pack and unpack") {
    GeometryStore s;
    const EntityHandle a = s.add_attdef({1, 2}, 3.0, 0.0, 0, "NO", "Drawing number", "D-000", kAttVerify);
    REQUIRE(s.attdef(a) != nullptr);
    CHECK(s.string_of(s.attdef(a)->text) == "NO");
    CHECK(s.attdef_prompt(*s.attdef(a)) == "Drawing number");
    CHECK(s.attdef_default(*s.attdef(a)) == "D-000");
    CHECK(s.attdef(a)->flags == kAttVerify);
    CHECK(s.text_like(a) == &s.attdef(a)->text);
    CHECK(s.props(a) == &s.attdef(a)->text.props);
    CHECK(s.is_valid(a));
    Vec2 lo;
    Vec2 hi;
    CHECK(entity_aabb(s, a, lo, hi)); // the text paths serve the definition too
    CHECK(lo.x == 1.0);

    std::vector<BlockDef> blocks(1);
    blocks[0].name = "TB";
    s.set_block_table(std::move(blocks));
    const EntityHandle in = s.add_insert(0, {0, 0}, 1, 1, 0, {}, {"D-100", "", "rev B"});
    CHECK(s.insert_attribs(*s.insert(in)) == std::vector<std::string>{"D-100", "", "rev B"});
    CHECK(s.set_insert_attribs(in, {"D-101"}));
    CHECK(s.insert_attribs(*s.insert(in)) == std::vector<std::string>{"D-101"});
    const EntityHandle plain = s.add_insert(0, {5, 5}, 1, 1, 0);
    CHECK(s.insert_attribs(*s.insert(plain)).empty());
    CHECK(s.remove(a));
    CHECK_FALSE(s.is_valid(a));
}

TEST_CASE("#25 engine: BLOCK folds ATTDEFs into attributes; INSERT draws values; ATTDISP, ATTEDIT, EXPLODE, undo") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {60, 0}, 1});
    engine.submit(attdef({2, 2}, "NO", "Drawing number", "D-000", 0, 1));
    engine.submit(attdef({2, 8}, "REV", "Revision", "A", kAttConstant, 1));
    engine.submit(attdef({2, 14}, "NOTE", "Hidden note", "secret", kAttInvisible, 1));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 4; }));
    const std::size_t model_lines = engine.snapshot().line_vertices.size(); // line + 3 tags

    engine.submit(DefineBlockCommand{"TB", {0, 0}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.block_names.size() == 1 && s.block_attdefs.size() == 1 && s.block_attdefs[0].size() == 3;
    }));
    CHECK(engine.snapshot().block_attdefs[0][0].tag == "NO");
    CHECK(engine.snapshot().block_attdefs[0][0].prompt == "Drawing number");
    CHECK(engine.snapshot().block_attdefs[0][1].flags == kAttConstant);
    // The in-place insert BLOCK leaves shows the defaults: "D-000" and "A"; the
    // invisible one is not drawn. Fewer glyph strokes than the three tags + line.
    const std::size_t defaults_lines = engine.snapshot().line_vertices.size();
    CHECK(defaults_lines > 2);
    CHECK(defaults_lines != model_lines);

    InsertBlockCommand ins;
    ins.name = "TB";
    ins.pos = {100, 0};
    ins.group = 3;
    ins.attribs = {"D-100-LONG-NUMBER", "A", "secret"};
    engine.submit(ins);
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() > defaults_lines; }));
    const std::size_t two_inserts = engine.snapshot().line_vertices.size();

    // ATTDISP OFF hides every value (line work drops to the two block lines).
    engine.submit(SetAttDispCommand{2});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() == 4; }));
    // ATTDISP ON shows even the invisible ones.
    engine.submit(SetAttDispCommand{1});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() > two_inserts; }));
    engine.submit(SetAttDispCommand{0});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() == two_inserts; }));

    // ATTEDIT: a longer value on the second insert adds strokes; undo takes it back.
    engine.submit(SetInsertAttribCommand{{100, 0}, 1.0, "NO", "D-100-EVEN-LONGER-NUMBER-HERE", 4});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() > two_inserts; }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() == two_inserts; }));
    engine.submit(SetInsertAttribCommand{{100, 0}, 1.0, "BOGUS", "x", 5});
    engine.submit(AddLineCommand{{200, 0}, {210, 0}, 6});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.line_vertices.size() == two_inserts + 2; }));

    // EXPLODE the second insert: the attributes come back as definitions (tags show).
    engine.submit(ClearSelectionCommand{});
    engine.submit(SelectPickCommand{{130, 0}, 1.0, false});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Insert;
    }));
    engine.submit(ExplodeSelectionCommand{7}); // the parts become the selection
    REQUIRE(wait_until(engine, [](const auto& s) {
        int attdefs = 0;
        for (const EntityHandle h : s.selection) {
            attdefs += h.kind == EntityKind::AttDef ? 1 : 0;
        }
        return attdefs == 3;
    }));
    engine.stop();
}

TEST_CASE("#25 native v28: ATTDEF entities, block attributes and INSERT values round-trip") {
    Document doc;
    DocAttDef a;
    a.text.pos = {1, 2};
    a.text.content = "NO";
    a.prompt = "Drawing number";
    a.def = "D 000"; // spaces survive in the default and in values
    a.flags = kAttVerify | kAttInvisible;
    doc.attdefs.push_back(a);
    DocBlockDef b;
    b.name = "TB";
    b.attdefs.push_back(a);
    DocAttDef rev = a;
    rev.text.content = "REV";
    rev.prompt.clear();
    rev.def = "A";
    rev.flags = kAttConstant;
    b.attdefs.push_back(rev);
    doc.block_defs.push_back(b);
    DocInsert in;
    in.block_name = "TB";
    in.pos = {10, 10};
    in.attribs = {DocAttrib{"NO", "D 100"}, DocAttrib{"REV", "A"}};
    doc.inserts.push_back(in);
    doc.attdisp = 2;

    Document rt;
    REQUIRE(parse_native(serialize_native(doc), rt).ok);
    CHECK(rt.attdefs == doc.attdefs);
    CHECK(rt.block_defs == doc.block_defs);
    CHECK(rt.inserts == doc.inserts);
    CHECK(rt.attdisp == 2);

    // Through the store: values are matched by tag and kept in attdef order.
    GeometryStore store;
    populate_store(store, rt);
    REQUIRE(store.attdefs().live_count() == 1);
    CHECK(store.attdisp() == 2);
    const Document again = document_from_store(store);
    REQUIRE(again.inserts.size() == 1);
    CHECK(again.inserts[0].attribs == in.attribs);
    CHECK(again.attdefs == doc.attdefs);
    REQUIRE(again.block_defs.size() == 1);
    CHECK(again.block_defs[0].attdefs == b.attdefs);
}

TEST_CASE("#25 DXF: ATTDEF entities and INSERT + ATTRIB + SEQEND both ways") {
    Document doc;
    DocAttDef a;
    a.text.pos = {1, 2};
    a.text.height = 3.0;
    a.text.content = "NO";
    a.prompt = "Drawing number";
    a.def = "D-000";
    a.flags = kAttVerify;
    doc.attdefs.push_back(a);
    DocBlockDef b;
    b.name = "TB";
    b.base = {0, 0};
    b.attdefs.push_back(a);
    b.lines.push_back(DocLine{{0, 0}, {50, 0}});
    doc.block_defs.push_back(b);
    DocInsert in;
    in.block_name = "TB";
    in.pos = {100, 0};
    in.scale_x = 2.0;
    in.scale_y = 2.0;
    in.attribs = {DocAttrib{"NO", "D-100"}};
    doc.inserts.push_back(in);

    const std::string dxf = serialize_dxf(doc);
    CHECK(dxf.find("\nATTDEF\n") != std::string::npos);
    CHECK(dxf.find("\nATTRIB\n") != std::string::npos);
    CHECK(dxf.find("\nSEQEND\n") != std::string::npos);
    CHECK(dxf.find("Drawing number") != std::string::npos);
    Document rt;
    const IoResult r = parse_dxf(dxf, rt);
    REQUIRE(r.ok);
    REQUIRE(rt.attdefs.size() == 1);
    CHECK(rt.attdefs[0].text.content == "NO");
    CHECK(rt.attdefs[0].prompt == "Drawing number");
    CHECK(rt.attdefs[0].def == "D-000");
    CHECK(rt.attdefs[0].flags == kAttVerify);
    CHECK(rt.attdefs[0].text.height == 3.0);
    REQUIRE(rt.block_defs.size() == 1);
    REQUIRE(rt.block_defs[0].attdefs.size() == 1);
    CHECK(rt.block_defs[0].attdefs[0].text.content == "NO");
    REQUIRE(rt.inserts.size() == 1);
    REQUIRE(rt.inserts[0].attribs.size() == 1);
    CHECK(rt.inserts[0].attribs[0].tag == "NO");
    CHECK(rt.inserts[0].attribs[0].value == "D-100");
    CHECK(r.message.find("ATTRIB") == std::string::npos); // nothing skipped
}

TEST_CASE("#25 commands: ATTDEF flow, INSERT asks for values (Constant/Preset skipped), ATTDISP, ATTEDIT") {
    ProcHarness h;
    h.proc.submit_line("ATTDEF");
    h.proc.submit_line("V");   // toggle Verify
    h.proc.submit_line("");    // done with modes
    h.proc.submit_line("no");  // tag (upper-cased)
    h.proc.submit_line("Drawing number");
    h.proc.submit_line("D-000");
    h.proc.submit_line("5,5");
    h.proc.submit_line("3");
    h.proc.submit_line("");
    const auto* a = h.last<AddAttDefCommand>();
    REQUIRE(a != nullptr);
    CHECK(a->text.content == "NO");
    CHECK(a->prompt == "Drawing number");
    CHECK(a->def == "D-000");
    CHECK(a->flags == kAttVerify);
    CHECK(a->text.pos == Vec2{5, 5});
    CHECK(a->text.height == 3.0);

    h.proc.set_block_names({"TB"});
    h.proc.set_block_attdefs({{BlockAttDefInfo{"NO", "Drawing number", "D-000", 0},
                               BlockAttDefInfo{"REV", "", "A", kAttConstant},
                               BlockAttDefInfo{"SHEET", "Sheet", "1", kAttPreset},
                               BlockAttDefInfo{"BY", "", "", 0}}});
    h.proc.submit_line("INSERT");
    h.proc.submit_line("TB");
    h.proc.submit_line("0,0");
    h.proc.submit_line(""); // X scale
    h.proc.submit_line(""); // Y scale
    h.proc.submit_line(""); // rotation
    REQUIRE_FALSE(h.out.prompts.empty());
    CHECK(h.out.prompts.back() == "Drawing number <D-000>: ");
    h.proc.submit_line("D-100");
    CHECK(h.out.prompts.back() == "BY: "); // REV (Constant) and SHEET (Preset) were skipped
    h.proc.submit_line("PK");
    const auto* ins = h.last<InsertBlockCommand>();
    REQUIRE(ins != nullptr);
    CHECK(ins->name == "TB");
    CHECK(ins->attribs == std::vector<std::string>{"D-100", "A", "1", "PK"});

    h.proc.submit_line("ATTDISP");
    h.proc.submit_line("OFF");
    const auto* d = h.last<SetAttDispCommand>();
    REQUIRE(d != nullptr);
    CHECK(d->mode == 2);

    h.proc.submit_line("ATTEDIT");
    h.proc.submit_line("3,4");
    h.proc.submit_line("no");
    h.proc.submit_line("D-200");
    const auto* e = h.last<SetInsertAttribCommand>();
    REQUIRE(e != nullptr);
    CHECK(e->pick == Vec2{3, 4});
    CHECK(e->tag == "NO");
    CHECK(e->value == "D-200");
}
