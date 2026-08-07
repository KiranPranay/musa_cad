// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

// Geometric tolerancing (GD&T): feature control frames and datum feature symbols.
//
// Both follow the rule the rest of the model follows -- the entity stores only its
// parameters (cells, insertion point, rotation, dimstyle) and ALL drawable geometry is
// derived here at snapshot/bounds/pick time, never baked. Each entity has exactly ONE
// geometry function, so the displayed, picked and bounded geometry cannot diverge.

namespace musacad::core {

/// The resolved, drawable geometry of a feature control frame. Segment lists are
/// endpoint pairs; `fills` are triangles (3 Vec2 each). Cell rectangles are returned
/// so pick and bounds can use exactly the boxes that were drawn.
struct FcfGeometry {
    std::vector<Vec2> lines; ///< outer border + the dividers between cells
    std::vector<Vec2> fills; ///< (unused by the frame; kept for symmetry with datums)

    /// One entry per cell, in order: the four corners of its rectangle
    /// (bottom-left, bottom-right, top-right, top-left) in world space.
    std::vector<std::array<Vec2, 4>> cell_quads;
    /// The visible (code-substituted) text of each cell and where its baseline starts.
    /// Text is CENTRED in its cell, per ASME Y14.5.
    std::vector<std::string> cell_text;
    std::vector<Vec2> text_pos;

    Rgb line_color;
    Rgb text_color;
    std::uint8_t lineweight = 25;
    double text_height = 2.5;
    double rotation = 0.0;
};

/// The resolved geometry of a datum feature symbol: the boxed letter, the leader from
/// the box down to the feature, and the filled triangle at the tip.
struct DatumGeometry {
    std::vector<Vec2> lines; ///< the box + the leader
    std::vector<Vec2> fills; ///< the filled triangle (3 Vec2 per triangle)

    std::array<Vec2, 4> box{}; ///< the letter box, for pick and bounds
    std::string text;          ///< the visible datum letter(s)
    Vec2 text_pos{};

    Rgb line_color;
    Rgb text_color;
    std::uint8_t lineweight = 25;
    double text_height = 2.5;
    double rotation = 0.0;
};

/// THE geometry definition of a feature control frame. Cell height is uniform at
/// ~2x the text height and padding is ~0.5x each side (ASME Y14.5 proportions), both
/// derived from the effective text height -- so a dimstyle edit re-lays out every
/// frame on the next snapshot and cell proportions cannot drift between frames.
///
/// `cells` are the RAW cell strings; control codes are expanded here (the one
/// substitution point), so `\U+2316` and `%%c` work in a cell exactly as in any text.
/// Called by the snapshot, entity bounds, the kernel's pick path and the preview.
[[nodiscard]] FcfGeometry compute_fcf_geometry(const FcfData& f,
                                               const std::vector<std::string_view>& cells,
                                               const DimStyle& style, Rgb base_color);

/// THE geometry definition of a datum feature symbol. The triangle is FILLED geometry
/// and is routed into the existing fill channel, exactly as arrowheads are.
[[nodiscard]] DatumGeometry compute_datum_geometry(const DatumData& d, std::string_view letter,
                                                   const DimStyle& style, Rgb base_color);

} // namespace musacad::core
