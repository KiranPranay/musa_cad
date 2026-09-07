// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Multi-level undo/redo across edits that re-create an entity. An undone edit re-creates
// its entity under a NEW handle; the history items of neighbouring edits must follow, or
// the next undo misses its remove and leaves a duplicate.

#include <chrono>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
} // namespace

TEST_CASE("Undo twice after move then rotate leaves exactly the original line; redo twice re-applies both") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(MoveSelectionCommand{{0, 5}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_min.y > 4.9; }));
    engine.submit(RotateSelectionCommand{{0, 5}, kHalfPi, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.bounds_max.y > 14.9; }));

    // Two undos, queued back to back: the rotate's create item must be remapped to the
    // handle the first undo... no -- the ROTATE undo re-creates the moved line under a
    // new handle, and the MOVE group's create item must follow it, or the second undo
    // cannot remove it.
    engine.submit(UndoLastGroupCommand{});
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.line_vertices.size() == 2 && s.bounds_max.y < 0.1 && s.bounds_max.x > 9.9;
    }));
    // Exactly one line: not the original plus a leftover moved copy.
    REQUIRE(engine.snapshot().line_vertices.size() == 2);

    engine.submit(RedoLastGroupCommand{});
    engine.submit(RedoLastGroupCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.line_vertices.size() == 2 && s.bounds_max.y > 14.9;
    }));
    REQUIRE(engine.snapshot().line_vertices.size() == 2);
    engine.stop();
}
