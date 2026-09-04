// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/table.hpp"

#include <algorithm>
#include <cmath>

#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"

namespace musacad::core {

namespace {

void seg(std::vector<Vec2>& out, Vec2 a, Vec2 b) {
    out.push_back(a);
    out.push_back(b);
}

/// Local (x right, y DOWN from the top-left insertion point) -> world. Rows grow
/// downward because that is how a table is read and how AutoCAD places one.
struct Frame {
    Vec2 origin;
    double cs;
    double sn;
    [[nodiscard]] Vec2 to_world(double x, double y) const {
        return {origin.x + x * cs + y * sn, origin.y + x * sn - y * cs};
    }
};

/// The text height a row is drawn at: title row, header row, then data rows.
double row_text_height(const TableData& t, const TableStyle& s, std::uint16_t row) {
    if (t.has_title && row == 0) {
        return s.title_height;
    }
    const std::uint16_t header_row = t.has_title ? 1 : 0;
    if (t.has_header && row == header_row) {
        return s.header_height;
    }
    return s.data_height;
}

} // namespace

TableGeometry compute_table_geometry(const TableData& t, const std::vector<TableCellView>& cells,
                                     std::span<const double> col_widths,
                                     std::span<const double> row_heights, const TableStyle& style,
                                     Rgb base_color) {
    TableGeometry g;
    g.lineweight = style.lineweight;
    g.line_color = style.line_color.resolve(base_color);
    g.text_color = style.text_color.resolve(base_color);
    g.rotation = t.rotation;

    const std::size_t rows = t.rows;
    const std::size_t cols = t.cols;
    if (rows == 0 || cols == 0 || col_widths.size() < cols || row_heights.size() < rows ||
        cells.size() < rows * cols) {
        return g; // a malformed table draws nothing rather than a stray box
    }

    // Cumulative edges, so a cell's rectangle and the grid lines come from one place.
    std::vector<double> x(cols + 1, 0.0);
    for (std::size_t c = 0; c < cols; ++c) {
        x[c + 1] = x[c] + std::max(col_widths[c], 0.0);
    }
    std::vector<double> y(rows + 1, 0.0);
    for (std::size_t r = 0; r < rows; ++r) {
        y[r + 1] = y[r] + std::max(row_heights[r], 0.0);
    }
    g.width = x[cols];
    g.height = y[rows];

    const double cs = std::cos(t.rotation);
    const double sn = std::sin(t.rotation);
    const Frame fr{t.pos, cs, sn};

    // Outer border.
    const Vec2 tl = fr.to_world(0.0, 0.0);
    const Vec2 tr = fr.to_world(g.width, 0.0);
    const Vec2 br = fr.to_world(g.width, g.height);
    const Vec2 bl = fr.to_world(0.0, g.height);
    seg(g.lines, tl, tr);
    seg(g.lines, tr, br);
    seg(g.lines, br, bl);
    seg(g.lines, bl, tl);

    const auto cell = [&](std::size_t r, std::size_t c) -> const TableCellView& {
        return cells[r * cols + c];
    };

    // Interior grid. A merged cell suppresses only the edges INSIDE its span, so a merge
    // reads as one box while every other boundary keeps its line -- drawing the full grid
    // and then erasing would be the other way round, and would fight the fill order.
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            const TableCellView& cv = cell(r, c);
            if (cv.span_cols == 0) {
                continue; // covered by a merge: its edges belong to the spanning cell
            }
            const std::size_t c1 = std::min(cols, c + std::max<std::size_t>(cv.span_cols, 1));
            const std::size_t r1 = std::min(rows, r + std::max<std::size_t>(cv.span_rows, 1));
            // Right edge (unless it is the outer border, already drawn).
            if (c1 < cols) {
                seg(g.lines, fr.to_world(x[c1], y[r]), fr.to_world(x[c1], y[r1]));
            }
            // Bottom edge.
            if (r1 < rows) {
                seg(g.lines, fr.to_world(x[c], y[r1]), fr.to_world(x[c1], y[r1]));
            }

            // The cell rectangle + its text.
            const double h = row_text_height(t, style, static_cast<std::uint16_t>(r));
            const double pad = style.margin * h;
            g.cell_quads.push_back({fr.to_world(x[c], y[r1]), fr.to_world(x[c1], y[r1]),
                                    fr.to_world(x[c1], y[r]), fr.to_world(x[c], y[r])});
            g.cell_index.push_back(static_cast<std::uint32_t>(r * cols + c));

            // Expand control codes ONCE, here, so the measured width and the drawn glyphs
            // are the same string and the entity keeps its raw cells.
            std::string vis = text::substitute_text(cv.text);
            const double tw = text::text_width(vis, h);
            const double avail = x[c1] - x[c];
            double tx = x[c] + pad;
            switch (static_cast<CellAlign>(cv.align)) {
            case CellAlign::Center:
                tx = x[c] + (avail - tw) * 0.5;
                break;
            case CellAlign::Right:
                tx = x[c1] - pad - tw;
                break;
            case CellAlign::Left:
                break;
            }
            // Vertically centred in the row: the baseline sits half a cap height below
            // the cell's middle.
            const double ty = y[r] + ((y[r1] - y[r]) + h) * 0.5;
            g.text_pos.push_back(fr.to_world(tx, ty));
            g.text_height.push_back(h);
            g.cell_text.push_back(std::move(vis));
        }
    }
    return g;
}

int table_cell_at(const TableGeometry& g, Vec2 p) {
    for (std::size_t i = 0; i < g.cell_quads.size(); ++i) {
        const std::array<Vec2, 4>& q = g.cell_quads[i];
        bool pos = false;
        bool neg = false;
        for (int k = 0; k < 4; ++k) {
            const Vec2 e =
                q[static_cast<std::size_t>((k + 1) % 4)] - q[static_cast<std::size_t>(k)];
            const Vec2 r = p - q[static_cast<std::size_t>(k)];
            const double cr = e.x * r.y - e.y * r.x;
            pos = pos || cr > 1e-12;
            neg = neg || cr < -1e-12;
        }
        if (!(pos && neg)) {
            return static_cast<int>(g.cell_index[i]);
        }
    }
    return -1;
}

} // namespace musacad::core
