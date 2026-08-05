// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core::text {

/// Horizontal justification of a text run relative to its insertion point.
enum class Justify { Left, Center, Right };

/// A single-stroke (vector) font. Chosen over a glyph atlas/SDF because text in a
/// vector CAD viewport must stay crisp at every zoom and batches naturally with
/// the existing line-segment pipeline -- no texture backend. Letterforms are
/// simplex/Hershey-class (engineering single-stroke); lowercase a-z are real glyphs
/// with true ascenders, x-height and descenders (small capitals remain only as a
/// defensive fallback for a missing glyph). The font is monospace: every glyph
/// shares one advance, including every symbol below -- so adding glyphs can never
/// change text_width, layout, bounds or pick.
///
/// Coverage:
/// * ASCII 0x20-0x7E.
/// * Engineering symbols: U+00B0 degree, U+00B1 plus-minus, U+2300 diameter.
/// * Hole callouts: U+2334 counterbore/spotface, U+21A7 depth, U+2335 countersink.
/// * GD&T characteristics: U+23E4 straightness, U+23E5 flatness, U+25CB circularity,
///   U+232D cylindricity, U+2312 profile of a line, U+2313 profile of a surface,
///   U+2220 angularity, U+27C2 perpendicularity, U+2225 parallelism, U+2316 position,
///   U+25CE concentricity, U+232F symmetry, U+2197 circular runout, U+2330 total runout.
/// * Material condition / modifiers: U+24C2 MMC, U+24C1 LMC, U+24C8 RFS, U+24C5
///   projected zone, U+24BB free state, U+24C9 tangent plane, U+24CA unequally disposed.
/// * Feature form: U+25A1 square, U+2332 conical taper, U+2333 slope.
///
/// Reachable from any text entity through the `%%` codes and the `\U+XXXX` escape
/// (see text_codes.hpp), so they work uniformly in TEXT, MTEXT, LEADER and dimension
/// text. An unmapped code point renders blank (advance only) -- never a wrong glyph.
///
/// `append_text_segments` emits world-space line segments (two Vec2 per segment)
/// for `text`: glyphs sit on a baseline through `origin`, `height` tall, rotated
/// `rotation` radians CCW about `origin`, shifted for `justify`. Screen-space text
/// (UI labels) is just rotation 0 with a y-down caller convention.
void append_text_segments(std::string_view text, Vec2 origin, double height, double rotation,
                          Justify justify, std::vector<Vec2>& out);

/// Total advance width of `text` at `height` (world units). Used for justification
/// and pick bounds.
[[nodiscard]] double text_width(std::string_view text, double height);

} // namespace musacad::core::text
