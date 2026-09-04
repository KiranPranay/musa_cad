// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <array>
#include <span>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/table_types.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

// TABLE geometry (issue #22). A table stores its cells, column widths and row heights;
// every drawable thing -- the outer border, the grid lines, each cell rectangle and each
// text position -- is DERIVED here at snapshot/bounds/pick time, never baked. One
// function, so the displayed, picked and bounded geometry cannot diverge, exactly as
// dimensions, blocks, hatches and feature control frames do.

namespace musacad::core {

/// The resolved, drawable geometry of a table.
struct TableGeometry {
    std::vector<Vec2> lines; ///< outer border + grid lines (endpoint pairs)

    /// One entry per VISIBLE cell (a cell covered by a merge produces none), in
    /// row-major order: the four world corners, bottom-left first, counter-clockwise.
    std::vector<std::array<Vec2, 4>> cell_quads;
    /// Parallel to `cell_quads`: the visible (code-substituted) text, where its
    /// baseline starts, and the height it is drawn at.
    std::vector<std::string> cell_text;
    std::vector<Vec2> text_pos;
    std::vector<double> text_height;
    /// Parallel to `cell_quads`: the flat cell index, so pick can map a hit back to the
    /// cell the user clicked (needed for cell editing).
    std::vector<std::uint32_t> cell_index;

    Rgb line_color;
    Rgb text_color;
    std::uint8_t lineweight = 25;
    double rotation = 0.0;

    /// Total size in the table's own frame, so bounds and grips need no re-derivation.
    double width = 0.0;
    double height = 0.0;
};

/// THE geometry definition of a table. `cells` are the RAW cell strings in row-major
/// order (`rows * cols` of them); control codes are expanded here, the one substitution
/// point, so a cell can carry `%%c` and `\U+XXXX` like any other text.
[[nodiscard]] TableGeometry compute_table_geometry(const TableData& t,
                                                   const std::vector<TableCellView>& cells,
                                                   std::span<const double> col_widths,
                                                   std::span<const double> row_heights,
                                                   const TableStyle& style, Rgb base_color);

/// The flat cell index under `p`, or -1. Used by pick and by cell editing.
[[nodiscard]] int table_cell_at(const TableGeometry& g, Vec2 p);

} // namespace musacad::core
