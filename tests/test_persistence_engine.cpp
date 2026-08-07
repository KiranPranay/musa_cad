// Persistence through the geometry-thread message pipeline: Save / New / Open as
// single store operations, dirty tracking, and fail-safe load.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;

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
std::string temp_path(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}
} // namespace

TEST_CASE("Save / New / Open round-trip through the engine; dirty tracking") {
    GeometryEngine engine;
    engine.start();
    const std::string path = temp_path("musacad_engine.musa");

    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    engine.submit(AddCircleCommand{{5, 5}, 3.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.dirty && !s.line_vertices.empty(); }));
    const std::size_t verts = engine.snapshot().line_vertices.size();

    // Save clears the dirty flag.
    engine.submit(SaveDocumentCommand{path, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.dirty && s.status == "Saved 2 entities."; }));

    // New empties the drawing (one op) and stays clean.
    engine.submit(NewDocumentCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty() && !s.dirty; }));

    // Open restores exactly what was saved.
    engine.submit(OpenDocumentCommand{path, false});
    REQUIRE(wait_until(engine, [verts](const auto& s) {
        return s.line_vertices.size() == verts && !s.dirty;
    }));
    REQUIRE(engine.snapshot().status == "Opened 2 entities.");

    engine.stop();
    std::filesystem::remove(path);
}

TEST_CASE("Opening a corrupt file fails gracefully, store unchanged") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    const std::string bad = temp_path("musacad_corrupt.musa");
    {
        std::ofstream f(bad);
        f << "this is not a musa file\n%%%garbage%%%\n";
    }
    engine.submit(OpenDocumentCommand{bad, false});
    // The error is reported and the existing drawing is left intact.
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.status.empty() && s.status != "Saved"; }));
    REQUIRE(engine.snapshot().line_vertices.size() == 2);

    engine.stop();
    std::filesystem::remove(bad);
}

TEST_CASE("Open is one undoable-clearing op: prior history does not resurrect old geometry") {
    GeometryEngine engine;
    engine.start();
    const std::string path = temp_path("musacad_hist.musa");

    engine.submit(AddLineCommand{{0, 0}, {1, 0}, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));
    engine.submit(SaveDocumentCommand{path, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.dirty; }));

    // Add more, then open the saved file (replaces the drawing).
    engine.submit(AddLineCommand{{2, 2}, {3, 3}, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 4; }));
    engine.submit(OpenDocumentCommand{path, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.size() == 2; }));

    // Undo after a load must not bring back pre-load geometry (history was reset).
    engine.submit(UndoLastGroupCommand{});
    bool resurrected = wait_until(engine, [](const auto& s) { return s.line_vertices.size() > 2; });
    REQUIRE_FALSE(resurrected);

    engine.stop();
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Regression: all_live() feeds the load-time spatial-index rebuild, SelectAll and
// ERASE All. It listed only the arenas that existed when it was written, so a HATCH
// loaded from a file was never indexed and could not be picked, hovered,
// window-selected or erased. A hatch created in-session worked (create_indexed inserts
// it directly), which is exactly why it went unnoticed. Found while adding the GD&T
// arenas, which would have inherited the same gap.
// ---------------------------------------------------------------------------
TEST_CASE("Every entity kind is indexed after a load, so a loaded entity is pickable") {
    const std::string path = temp_path("musacad_all_live.musa");

    GeometryEngine engine;
    engine.start();
    // One entity per late-added arena, well separated so a pick is unambiguous.
    engine.submit(AddHatchCommand{
        {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}, "SOLID", 1.0, 0.0, {0, 0}, 1});
    engine.submit(AddFcfCommand{{"⌖", "0.1", "A"}, {100.0, 0.0}, 0.0, 0, 2});
    engine.submit(AddDatumCommand{"A", {200.0, -10.0}, {200.0, 0.0}, 0.0, 0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return !s.line_vertices.empty() && !s.fill_vertices.empty();
    }));

    engine.submit(SaveDocumentCommand{path, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.dirty; }));

    // Reload into a clean document -- this is the path that used to lose the index.
    engine.submit(NewDocumentCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.line_vertices.empty(); }));
    engine.submit(OpenDocumentCommand{path, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));

    // SelectAll is driven by the same all_live(), so it must reach all three kinds.
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 3; }));

    // And the hatch is individually pickable, which REQUIRES a spatial-index entry --
    // this is the assertion that fails without the all_live() fix.
    engine.submit(ClearSelectionCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.empty(); }));
    engine.submit(SelectPickCommand{{5.0, 5.0}, 1.0, false}); // inside the hatch
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.selection.size() == 1 && s.selection[0].kind == EntityKind::Hatch;
    }));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
