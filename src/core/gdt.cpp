// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/gdt.hpp"

#include <algorithm>
#include <cmath>

#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"

namespace musacad::core {

namespace {

// ASME Y14.5 frame proportions, all expressed as multiples of the text height so the
// frame scales with the dimstyle and can never drift between frames.
constexpr double kCellHeight = 2.0; ///< frame height = 2h
constexpr double kPad = 0.5;        ///< clear space each side of a cell's text = 0.5h
constexpr double kMinCell = 1.6;    ///< a lone symbol still gets a readable box

void seg(std::vector<Vec2>& out, Vec2 a, Vec2 b) {
    out.push_back(a);
    out.push_back(b);
}
void tri(std::vector<Vec2>& out, Vec2 a, Vec2 b, Vec2 c) {
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
}

/// Local frame -> world: rotate about the origin, then translate to `pos`.
struct Frame {
    Vec2 origin;
    Vec2 ax; ///< local +x
    Vec2 ay; ///< local +y
    [[nodiscard]] Vec2 to_world(double x, double y) const {
        return {origin.x + ax.x * x + ay.x * y, origin.y + ax.y * x + ay.y * y};
    }
};

Frame frame_of(Vec2 pos, double rotation) {
    const double cs = std::cos(rotation);
    const double sn = std::sin(rotation);
    return Frame{pos, Vec2{cs, sn}, Vec2{-sn, cs}};
}

} // namespace

FcfGeometry compute_fcf_geometry(const FcfData& f, const std::vector<std::string_view>& cells,
                                 const DimStyle& raw_style, Rgb base_color) {
    // Per-entity overrides win over the style, resolved through the SAME
    // apply_dim_overrides dimensions and leaders use -- one resolution path.
    const DimStyle style = apply_dim_overrides(raw_style, f.overrides);

    FcfGeometry g;
    g.text_height = style.text_height;
    g.lineweight = style.dim_lineweight;
    g.line_color = style.dim_color.resolve(base_color);
    g.text_color = style.text_color.resolve(base_color);
    g.rotation = f.rotation;

    const double h = style.text_height;
    const double box_h = kCellHeight * h;
    const double pad = kPad * h;
    const Frame fr = frame_of(f.pos, f.rotation);

    // Expand control codes ONCE, here, so the measured width and the drawn glyphs are
    // the same string (the entity keeps the raw cells -- derived-not-baked).
    g.cell_text.reserve(cells.size());
    std::vector<double> widths;
    widths.reserve(cells.size());
    for (const std::string_view c : cells) {
        std::string vis = text::substitute_text(c);
        widths.push_back(std::max(text::text_width(vis, h) + 2.0 * pad, kMinCell * h));
        g.cell_text.push_back(std::move(vis));
    }
    if (widths.empty()) {
        return g; // a frame with no cells draws nothing rather than a stray box
    }

    double total = 0.0;
    for (const double w : widths) {
        total += w;
    }

    // Outer border.
    const Vec2 bl = fr.to_world(0.0, 0.0);
    const Vec2 br = fr.to_world(total, 0.0);
    const Vec2 tr = fr.to_world(total, box_h);
    const Vec2 tl = fr.to_world(0.0, box_h);
    seg(g.lines, bl, br);
    seg(g.lines, br, tr);
    seg(g.lines, tr, tl);
    seg(g.lines, tl, bl);

    // Cell rectangles, dividers and centred text. The divider is drawn on each cell's
    // right edge except the last, whose edge is the outer border.
    double x = 0.0;
    g.cell_quads.reserve(widths.size());
    g.text_pos.reserve(widths.size());
    for (std::size_t i = 0; i < widths.size(); ++i) {
        const double w = widths[i];
        g.cell_quads.push_back({fr.to_world(x, 0.0), fr.to_world(x + w, 0.0),
                                fr.to_world(x + w, box_h), fr.to_world(x, box_h)});
        if (i + 1 < widths.size()) {
            seg(g.lines, fr.to_world(x + w, 0.0), fr.to_world(x + w, box_h));
        }
        // Text centred horizontally, and vertically on the cell's mid-height (the
        // baseline sits half a cap-height below centre).
        const double tw = text::text_width(g.cell_text[i], h);
        g.text_pos.push_back(fr.to_world(x + (w - tw) * 0.5, (box_h - h) * 0.5));
        x += w;
    }
    return g;
}

DatumGeometry compute_datum_geometry(const DatumData& d, std::string_view letter,
                                     const DimStyle& raw_style, Rgb base_color) {
    const DimStyle style = apply_dim_overrides(raw_style, d.overrides);

    DatumGeometry g;
    g.text_height = style.text_height;
    g.lineweight = style.dim_lineweight;
    g.line_color = style.dim_color.resolve(base_color);
    g.text_color = style.text_color.resolve(base_color);
    g.rotation = d.rotation;
    g.text = text::substitute_text(letter);

    const double h = style.text_height;
    const double box_h = kCellHeight * h;
    const double pad = kPad * h;
    const double box_w = std::max(text::text_width(g.text, h) + 2.0 * pad, kMinCell * h);
    const Frame fr = frame_of(d.pos, d.rotation);

    // The boxed letter.
    g.box = {fr.to_world(0.0, 0.0), fr.to_world(box_w, 0.0), fr.to_world(box_w, box_h),
             fr.to_world(0.0, box_h)};
    seg(g.lines, g.box[0], g.box[1]);
    seg(g.lines, g.box[1], g.box[2]);
    seg(g.lines, g.box[2], g.box[3]);
    seg(g.lines, g.box[3], g.box[0]);
    const double tw = text::text_width(g.text, h);
    g.text_pos = fr.to_world((box_w - tw) * 0.5, (box_h - h) * 0.5);

    // The leader runs from the box's nearest edge midpoint to the tip, and the filled
    // triangle sits at the tip, its base perpendicular to the leader -- so the symbol
    // reads correctly whichever side of the feature the box is placed on.
    const Vec2 box_bottom = fr.to_world(box_w * 0.5, 0.0);
    const Vec2 box_top = fr.to_world(box_w * 0.5, box_h);
    const Vec2 anchor =
        distance(box_bottom, d.tip) <= distance(box_top, d.tip) ? box_bottom : box_top;

    const Vec2 v = d.tip - anchor;
    const double len = length(v);
    if (len < 1e-9) {
        return g; // degenerate: box sitting on the feature, no leader to draw
    }
    const Vec2 u = v / len;
    const Vec2 perp{-u.y, u.x};
    // Stop the leader at the triangle's base so the line does not show through the fill.
    const double t_len = style.arrow_size;
    // ASME draws the datum triangle FILLED and roughly equilateral -- noticeably squatter
    // than a dimension arrowhead (which is 0.18 x size half-width). At the arrowhead's
    // proportion it reads as an arrow pointing at the feature, not as a datum symbol;
    // the plotted sheet is what showed this.
    const double t_half = t_len * 0.55;
    const Vec2 base = d.tip - u * t_len;
    seg(g.lines, anchor, base);
    tri(g.fills, d.tip, base + perp * t_half, base - perp * t_half);
    return g;
}

} // namespace musacad::core
