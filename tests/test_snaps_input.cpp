// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #32: the Insertion, Apparent intersection and Parallel object snaps; ROTATE /
// SCALE [Copy] and [Reference]; -OSNAP; editable frame cells and datum letters.

#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/osnap.hpp"
#include "musacad/core/properties_registry.hpp"
#include "musacad/core/spatial_grid.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {
struct Scene {
    GeometryStore store;
    NativeKernel2D kernel;
    SpatialGrid grid{16.0};
    void index(EntityHandle h) {
        Vec2 lo;
        Vec2 hi;
        if (entity_aabb(store, h, lo, hi)) {
            grid.insert(h, lo, hi);
        }
    }
    SnapResult snap(Vec2 cursor, double radius, std::uint32_t mask, std::optional<Vec2> from = std::nullopt) {
        return compute_snap(store, kernel, grid, cursor, radius, mask, from);
    }
};
struct SilentOutput : musacad::command::CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string&) override {}
    std::vector<std::string> lines;
};
struct StubView : musacad::command::ViewControl {
    std::uint32_t mask = kAllSnaps;
    int dialogs = 0;
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    std::uint32_t snap_mask() const override { return mask; }
    void set_snap_mask(std::uint32_t m) override { mask = m; }
    void osnap_settings_dialog() override { ++dialogs; }
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
} // namespace

TEST_CASE("#32 snaps: Insertion on text and inserts; Apparent intersection of extended lines; Parallel from a point") {
    Scene s;
    const EntityHandle t = s.store.add_text({10, 10}, 2.0, 0.0, 0, "abc");
    s.index(t);
    SnapResult r = s.snap({10.3, 10.2}, 1.0, kAllSnaps);
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Insertion);
    REQUIRE(r.point == Vec2{10, 10});

    // Two lines that stop short of each other, whose extensions cross at (20, 20). Both
    // ends lie inside the aperture: a RUNNING apparent-intersection snap, like AutoCAD's,
    // only considers objects near the cursor.
    Scene a;
    a.index(a.store.add_line({0, 20}, {19, 20}));
    a.index(a.store.add_line({20, 0}, {20, 19}));
    r = a.snap({20.4, 19.6}, 2.0, snap_bit(SnapType::ApparentIntersection));
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::ApparentIntersection);
    REQUIRE(std::abs(r.point.x - 20.0) < 1e-9);
    REQUIRE(std::abs(r.point.y - 20.0) < 1e-9);
    // Not offered when the type is off (it lies on no object).
    r = a.snap({20.4, 19.6}, 2.0, kAllSnaps);
    REQUIRE(!(r.found && r.type == SnapType::ApparentIntersection));

    // Parallel: from (0,0), hovering near the direction of a 45-degree line far away.
    Scene p;
    p.index(p.store.add_line({100, 100}, {110, 110}));
    r = p.snap({105.0, 104.4}, 1.0, snap_bit(SnapType::Parallel), Vec2{0, 0});
    REQUIRE(r.found);
    REQUIRE(r.type == SnapType::Parallel);
    REQUIRE(r.point.x == Approx(r.point.y)); // on the line y = x through the from-point
    REQUIRE(r.point.x == Approx(104.7).margin(1e-6));
    r = p.snap({105.0, 104.4}, 1.0, snap_bit(SnapType::Parallel)); // no from-point: nothing
    REQUIRE(!r.found);
}

TEST_CASE("#32 ROTATE and SCALE: Copy and Reference options") {
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("ROTATE");
        h.proc.submit_line("0,0");
        h.proc.submit_line("C");
        h.proc.submit_line("R");
        h.proc.submit_line("30");  // reference angle
        h.proc.submit_line("120"); // new angle -> rotate by 90
        const auto* r = h.last<RotateSelectionCommand>();
        REQUIRE(r != nullptr);
        REQUIRE(r->copy);
        REQUIRE(r->angle == Approx(kHalfPi));
    }
    {
        ProcHarness h;
        h.proc.set_selection_count(1);
        h.proc.submit_line("SCALE");
        h.proc.submit_line("0,0");
        h.proc.submit_line("R");
        h.proc.submit_line("4"); // reference length
        h.proc.submit_line("6"); // new length -> factor 1.5
        const auto* sc = h.last<ScaleSelectionCommand>();
        REQUIRE(sc != nullptr);
        REQUIRE(!sc->copy);
        REQUIRE(sc->factor == Approx(1.5));
        h.proc.submit_line("SC");
        h.proc.submit_line("0,0");
        h.proc.submit_line("C");
        h.proc.submit_line("2");
        REQUIRE(h.last<ScaleSelectionCommand>()->copy);
        REQUIRE(h.last<ScaleSelectionCommand>()->factor == Approx(2.0));
    }
}

TEST_CASE("#32 -OSNAP sets the running snaps by mode list; OSNAP opens the settings") {
    ProcHarness h;
    h.proc.submit_line("-OSNAP");
    h.proc.submit_line("END,MID,PAR");
    REQUIRE(h.view.mask == (snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint) | snap_bit(SnapType::Parallel)));
    h.proc.submit_line("-OSNAP");
    h.proc.submit_line("NONE");
    REQUIRE(h.view.mask == 0);
    h.proc.submit_line("-OSNAP");
    h.proc.submit_line("ALL");
    REQUIRE((h.view.mask & snap_bit(SnapType::ApparentIntersection)) != 0);
    h.proc.submit_line("-OSNAP");
    h.proc.submit_line("BOGUS");
    REQUIRE(h.proc.has_active_command()); // refused, still at the prompt
    h.proc.submit_line("int");
    REQUIRE(h.view.mask == snap_bit(SnapType::Intersection));
    h.proc.submit_line("OS");
    REQUIRE(h.view.dialogs == 1);
}

TEST_CASE("#32 palette: feature control frame cells and datum letter are editable") {
    Command fcf = AddFcfCommand{{"\\U+2316", "0.1", "A"}, {0, 0}, 0.0, 0, 1};
    PropertyValue v;
    v.text = "\\U+2316 | \\U+2300 0.2 | A | B";
    write_property(fcf, PropertyId::FcfCells, v);
    const auto* f = std::get_if<AddFcfCommand>(&fcf);
    REQUIRE(f != nullptr);
    REQUIRE(f->cells.size() == 4);
    REQUIRE(f->cells[1] == "\\U+2300 0.2");
    REQUIRE(f->cells[3] == "B");
    Command datum = AddDatumCommand{"A", {0, 0}, {5, 5}, 0.0, 0, 1};
    PropertyValue l;
    l.text = "C";
    write_property(datum, PropertyId::DatumLetter, l);
    REQUIRE(std::get_if<AddDatumCommand>(&datum)->letter == "C");
}
