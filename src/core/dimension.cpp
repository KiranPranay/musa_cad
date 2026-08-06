// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/dimension.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "musacad/core/text/text_codes.hpp"

namespace musacad::core {

namespace {

Vec2 dim_direction(const DimData& d) {
    if (d.type == DimType::Aligned) {
        const Vec2 v = d.b - d.a;
        return length_squared(v) > 1e-18 ? normalized(v) : Vec2{1, 0};
    }
    return std::abs(d.b.x - d.a.x) >= std::abs(d.b.y - d.a.y) ? Vec2{1, 0} : Vec2{0, 1};
}
Vec2 foot(Vec2 p, Vec2 line_pt, Vec2 dir) { return line_pt + dir * dot(p - line_pt, dir); }
void seg(std::vector<Vec2>& out, Vec2 a, Vec2 b) {
    out.push_back(a);
    out.push_back(b);
}
void tri(std::vector<Vec2>& out, Vec2 a, Vec2 b, Vec2 c) {
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
}

} // namespace

void append_arrowhead(std::vector<Vec2>& fills, std::vector<Vec2>& lines, Vec2 tip, Vec2 along,
                      double size, ArrowType type) {
    const Vec2 u = length_squared(along) > 1e-18 ? normalized(along) : Vec2{1, 0};
    const Vec2 perp{-u.y, u.x};
    const Vec2 base = tip + u * size;
    switch (type) {
    case ArrowType::Filled: {
        tri(fills, tip, base + perp * (size * 0.18), base - perp * (size * 0.18));
        break;
    }
    case ArrowType::Dot: {
        const double r = size * 0.35;
        const Vec2 c = tip;
        const Vec2 a0 = c + perp * r;
        const Vec2 a1 = c + u * r;
        const Vec2 a2 = c - perp * r;
        const Vec2 a3 = c - u * r;
        tri(fills, a0, a1, a2); // a filled diamond approximates the dot
        tri(fills, a0, a2, a3);
        break;
    }
    case ArrowType::Tick: {
        const Vec2 d = (u + perp) * (size * 0.5);
        seg(lines, tip - d, tip + d);
        break;
    }
    case ArrowType::Open: {
        seg(lines, tip, base + perp * (size * 0.25));
        seg(lines, tip, base - perp * (size * 0.25));
        break;
    }
    }
}

double dim_measure(const DimData& d) {
    switch (d.type) {
    case DimType::Aligned:
        return distance(d.a, d.b);
    case DimType::Radius:
        return distance(d.a, d.b); // a = centre, b = point on the circle/arc
    case DimType::Diameter:
        return 2.0 * distance(d.a, d.b);
    case DimType::Angular: {
        // a = vertex, b = point on ray 1, line_pt = point on ray 2.
        const Vec2 u1 = normalized(d.b - d.a);
        const Vec2 u2 = normalized(d.line_pt - d.a);
        const double c = std::clamp(dot(u1, u2), -1.0, 1.0);
        return to_degrees(std::acos(c));
    }
    case DimType::Linear:
        break;
    }
    const Vec2 dir = dim_direction(d);
    return std::abs(dot(d.b - d.a, dir));
}

std::string format_measurement(double value, std::uint8_t precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(precision), value);
    return std::string(buf);
}

namespace {

/// The type-specific decoration the measured value always carries (R, the diameter
/// sign, the degree sign). Independent of the AUTHORED decoration below. `value` is
/// passed in rather than measured here so the Limits mode can format its two limits
/// through exactly the same per-type rule.
std::string measured_text_for(DimType type, const DimStyle& style, double value) {
    const std::string v = format_measurement(value, style.precision);
    switch (type) {
    case DimType::Radius:
        return "R" + v;
    case DimType::Diameter:
        return "⌀" + v; // U+2300
    case DimType::Angular:
        return v + "°"; // U+00B0
    case DimType::Linear:
    case DimType::Aligned:
        break;
    }
    return v;
}

std::string measured_text(const DimData& d, const DimStyle& style) {
    return measured_text_for(d.type, style, dim_measure(d));
}

} // namespace

bool dim_label_quad(const DimGeometry& g, bool second, Vec2 (&out)[4]) {
    const std::string& s = second ? g.label2 : g.label;
    if (s.empty()) {
        return false;
    }
    const Vec2 at = second ? g.label2_pos : g.text_pos;
    const double w = text::text_width(s, g.text_height);
    const double x0 = g.text_justify == text::Justify::Center  ? -w * 0.5
                      : g.text_justify == text::Justify::Right ? -w
                                                               : 0.0;
    const double cs = std::cos(g.text_rotation);
    const double sn = std::sin(g.text_rotation);
    const Vec2 ax{cs, sn};  // along the baseline
    const Vec2 ay{-sn, cs}; // baseline -> cap
    out[0] = at + ax * x0;
    out[1] = at + ax * (x0 + w);
    out[2] = at + ax * (x0 + w) + ay * g.text_height;
    out[3] = at + ax * x0 + ay * g.text_height;
    return true;
}

DimLabel compose_dim_label(const DimData& d, const DimStyle& style, DimTextParts parts) {
    // Prefix/suffix are ordinary text: run them through the ONE control-code pass, so
    // "%%c200" and "6X" work here exactly as they do in a TEXT entity, with no
    // dimension-specific symbol handling. Storage stays raw (derived-not-baked).
    const std::string pre = text::substitute_text(parts.prefix);
    const std::string suf = text::substitute_text(parts.suffix);
    const auto wrap = [&](const std::string& core) { return pre + core + suf; };

    DimLabel out;
    switch (d.tol.mode) {
    case TolMode::Symmetric:
        // One line: the value, then the deviation. ISO 129-1 writes a single
        // deviation as value ± tol.
        out.line1 = wrap(measured_text(d, style) + " ±" +
                         format_measurement(std::abs(d.tol.upper), style.precision));
        break;
    case TolMode::Limits: {
        // Two lines: the actual limit VALUES, upper over lower -- not the deviations.
        // That is what a machinist reads off the drawing.
        const double v = dim_measure(d);
        const double hi = std::max(v + d.tol.upper, v + d.tol.lower);
        const double lo = std::min(v + d.tol.upper, v + d.tol.lower);
        out.line1 = wrap(measured_text_for(d.type, style, hi));
        out.line2 = wrap(measured_text_for(d.type, style, lo));
        break;
    }
    case TolMode::Basic:
        out.line1 = wrap(measured_text(d, style));
        out.boxed = true;
        break;
    case TolMode::Reference:
        out.line1 = "(" + wrap(measured_text(d, style)) + ")";
        break;
    case TolMode::None:
        out.line1 = wrap(measured_text(d, style));
        break;
    }
    return out;
}

// Builds the geometry from an ALREADY-EFFECTIVE style (overrides applied).
static DimGeometry compute_dim_geometry_styled(const DimData& d, const DimStyle& style,
                                               Rgb base_color, DimTextParts parts);

DimGeometry compute_dim_geometry(const DimData& d, const DimStyle& style, Rgb base_color,
                                 DimTextParts parts) {
    // The single resolution point: per-dimension overrides win over the style
    // (the Ph12 ByLayer/override shape), then the body reads the effective style.
    return compute_dim_geometry_styled(d, apply_dim_overrides(style, d.overrides), base_color,
                                       parts);
}

static DimGeometry compute_dim_geometry_styled(const DimData& d, const DimStyle& style,
                                               Rgb base_color, DimTextParts parts) {
    DimGeometry g;
    g.text_height = style.text_height;
    g.lineweight = style.dim_lineweight;
    g.dim_color = style.dim_color.resolve(base_color);
    g.ext_color = style.ext_color.resolve(base_color);
    g.arrow_color = style.arrow_color.resolve(base_color);
    g.text_color = style.text_color.resolve(base_color);
    const auto atype = static_cast<ArrowType>(style.arrow_type);
    // The label is composed ONCE, here, for every dimension type -- the measured value
    // (which is never authorable) plus its authored decoration. Placing it in the same
    // function that builds the geometry is what makes the text participate in layout,
    // bounds and selection instead of floating alongside them.
    const DimLabel label = compose_dim_label(d, style, parts);
    g.label = label.line1;
    g.label2 = label.line2;

    /// Finishes the label once text_pos / rotation / justify are final: places the
    /// second line (Limits mode) and, for a basic dimension, draws the ASME Y14.5
    /// frame around exactly what will be drawn. The frame is dimension-line geometry,
    /// so it takes that colour and is picked and bounded with the dimension.
    const auto finish_label = [&]() {
        const double h = g.text_height;
        const double cs = std::cos(g.text_rotation);
        const double sn = std::sin(g.text_rotation);
        const Vec2 ax{cs, sn};  // along the baseline
        const Vec2 ay{-sn, cs}; // baseline -> cap
        const double line_gap = h * 1.5;
        const double pad = h * 0.4; // ASME draws the frame close around the text
        // A boxed label needs the FRAME, not the text, to clear the dimension line --
        // otherwise the box's lower edge lands exactly on it. Lift before placing the
        // second line so both stay inside the frame.
        if (label.boxed) {
            g.text_pos = g.text_pos + ay * pad;
        }
        if (!g.label2.empty()) {
            g.label2_pos = g.text_pos - ay * line_gap; // the lower limit sits under the upper
        }
        if (!label.boxed) {
            return;
        }
        const double w = std::max(text::text_width(g.label, h), text::text_width(g.label2, h));
        const double x0 = g.text_justify == text::Justify::Center  ? -w * 0.5
                          : g.text_justify == text::Justify::Right ? -w
                                                                   : 0.0;
        const double y_bottom = g.label2.empty() ? 0.0 : -line_gap;
        const Vec2 c0 = g.text_pos + ax * (x0 - pad) + ay * (y_bottom - pad);
        const Vec2 c1 = g.text_pos + ax * (x0 + w + pad) + ay * (y_bottom - pad);
        const Vec2 c2 = g.text_pos + ax * (x0 + w + pad) + ay * (h + pad);
        const Vec2 c3 = g.text_pos + ax * (x0 - pad) + ay * (h + pad);
        seg(g.dim_lines, c0, c1);
        seg(g.dim_lines, c1, c2);
        seg(g.dim_lines, c2, c3);
        seg(g.dim_lines, c3, c0);
    };

    if (d.type == DimType::Radius || d.type == DimType::Diameter) {
        const Vec2 center = d.a;
        const Vec2 edge = d.b;
        const Vec2 u = normalized(edge - center);
        if (d.type == DimType::Radius) {
            seg(g.dim_lines, center, edge);
            append_arrowhead(g.arrow_fills, g.arrow_lines, edge, u * -1.0, style.arrow_size, atype);
        } else {
            const Vec2 other = center - u * distance(center, edge);
            seg(g.dim_lines, other, edge);
            append_arrowhead(g.arrow_fills, g.arrow_lines, edge, u * -1.0, style.arrow_size, atype);
            append_arrowhead(g.arrow_fills, g.arrow_lines, other, u, style.arrow_size, atype);
        }
        g.text_pos = edge + u * (style.text_height * 0.4);
        g.text_rotation = 0.0;
        g.text_justify = text::Justify::Left;
        finish_label();
        return g;
    }

    if (d.type == DimType::Angular) {
        const Vec2 v = d.a;
        const Vec2 u1 = normalized(d.b - v);
        const Vec2 u2 = normalized(d.line_pt - v);
        const double r = std::max(distance(v, d.b), distance(v, d.line_pt)) * 0.8;
        double a0 = std::atan2(u1.y, u1.x);
        double a1 = std::atan2(u2.y, u2.x);
        double sweep = a1 - a0;
        while (sweep <= -kPi) {
            sweep += kTwoPi;
        }
        while (sweep > kPi) {
            sweep -= kTwoPi;
        }
        constexpr int kSteps = 24;
        Vec2 prev{};
        for (int i = 0; i <= kSteps; ++i) {
            const double a = a0 + sweep * (static_cast<double>(i) / kSteps);
            const Vec2 p{v.x + r * std::cos(a), v.y + r * std::sin(a)};
            if (i > 0) {
                seg(g.dim_lines, prev, p);
            }
            prev = p;
        }
        // Arrowheads tangent to the arc at each end.
        const Vec2 e0{v.x + r * std::cos(a0), v.y + r * std::sin(a0)};
        const Vec2 e1{v.x + r * std::cos(a1), v.y + r * std::sin(a1)};
        const double s = sweep >= 0 ? 1.0 : -1.0;
        append_arrowhead(g.arrow_fills, g.arrow_lines, e0,
                         Vec2{std::sin(a0), -std::cos(a0)} * s, style.arrow_size, atype);
        append_arrowhead(g.arrow_fills, g.arrow_lines, e1,
                         Vec2{-std::sin(a1), std::cos(a1)} * s, style.arrow_size, atype);
        const double am = a0 + sweep * 0.5;
        g.text_pos = {v.x + r * std::cos(am), v.y + r * std::sin(am)};
        g.text_rotation = 0.0;
        finish_label();
        return g;
    }

    // Linear / Aligned.
    const Vec2 dir = dim_direction(d);
    const Vec2 fa = foot(d.a, d.line_pt, dir);
    const Vec2 fb = foot(d.b, d.line_pt, dir);
    const auto ext = [&](Vec2 def, Vec2 f) {
        const Vec2 v = f - def;
        const double len = length(v);
        if (len < 1e-9) {
            return;
        }
        const Vec2 n = v / len;
        seg(g.ext_lines, def + n * style.ext_offset, f + n * style.ext_extension);
    };
    ext(d.a, fa);
    ext(d.b, fb);
    seg(g.dim_lines, fa, fb);

    const double span = distance(fa, fb);
    if (span > 1e-9) {
        const Vec2 u = (fb - fa) / span;
        append_arrowhead(g.arrow_fills, g.arrow_lines, fa, u, style.arrow_size, atype);
        append_arrowhead(g.arrow_fills, g.arrow_lines, fb, u * -1.0, style.arrow_size, atype);
    }

    const Vec2 mid = (fa + fb) * 0.5;
    g.text_rotation = std::atan2(dir.y, dir.x);
    if (g.text_rotation > 1.5708 || g.text_rotation < -1.5708) {
        g.text_rotation += kPi;
    }
    // Offset along the text's own baseline->cap direction ("up"), derived from the final
    // text rotation -- NOT the geometric perpendicular. The stroke font grows glyphs from
    // the baseline toward the cap; for a rotated (e.g. vertical) dimension that direction
    // is not the geometric perp, so anchoring to perp inverts Above/Centered. "Centered"
    // straddles the dim line (baseline half a glyph below it); "Above" clears it.
    const double cs = std::cos(g.text_rotation);
    const double sn = std::sin(g.text_rotation);
    const Vec2 text_up{-sn, cs};
    const double off = style.text_above ? style.text_height * 0.4 : -style.text_height * 0.5;
    g.text_pos = mid + text_up * off;
    // A two-line label (Limits) stacks DOWNWARD from text_pos, so lift the block by one
    // line to keep it clear of the dimension line -- otherwise the lower limit lands on it.
    if (!g.label2.empty()) {
        g.text_pos = g.text_pos + text_up * (g.text_height * 1.5);
    }
    finish_label();
    return g;
}

} // namespace musacad::core
