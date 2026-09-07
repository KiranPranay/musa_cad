// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/math.hpp"

// Shared ellipse geometry (AutoCAD ELLIPSE): the one parametric definition every
// consumer uses -- tessellation for drawing/picking/bounds, snap points, grips and the
// command's rubber band -- so nothing is baked and nothing can drift.
//
// An ellipse is centre C, MAJOR half-axis vector M (length = major radius, direction =
// rotation), RATIO r = minor/major in (0,1], and a parameter range [start,end]. The
// point at parameter t is  C + M cos t + N sin t  with N = perp(M) * r, perp = +90 deg,
// so t runs counter-clockwise. A full ellipse is start=0, end=2pi. This is exactly the
// DXF ELLIPSE definition (codes 10/11/40/41/42), so files round-trip with no conversion.

namespace musacad::core::ellipse {

/// The minor half-axis vector N = perp(M) * ratio.
inline Vec2 minor_axis(const EllipseData& e) noexcept {
    return Vec2{-e.major.y * e.ratio, e.major.x * e.ratio};
}

/// The parameter sweep in (0, 2pi]. end == start (or a whole number of turns) is full.
inline double sweep_of(const EllipseData& e) noexcept {
    double s = std::fmod(e.end - e.start, kTwoPi);
    if (s < 0.0) {
        s += kTwoPi;
    }
    if (s <= 1e-12) {
        s = kTwoPi;
    }
    return s;
}

inline bool is_full(const EllipseData& e) noexcept { return sweep_of(e) >= kTwoPi - 1e-9; }

inline Vec2 point_at(const EllipseData& e, double t) noexcept {
    const Vec2 n = minor_axis(e);
    const double c = std::cos(t);
    const double s = std::sin(t);
    return {e.center.x + e.major.x * c + n.x * s, e.center.y + e.major.y * c + n.y * s};
}

/// Parameter of the ray from the centre through p (the AutoCAD grip/snap rule: the
/// parameter whose point lies on that ray), in [0, 2pi).
inline double param_of(const EllipseData& e, Vec2 p) noexcept {
    const double ml = length(e.major);
    if (ml <= 1e-12 || e.ratio <= 1e-12) {
        return 0.0;
    }
    const Vec2 mh{e.major.x / ml, e.major.y / ml};
    const Vec2 u = p - e.center;
    const double x = dot(u, mh) / ml;
    const double y = dot(u, Vec2{-mh.y, mh.x}) / (ml * e.ratio);
    double t = std::atan2(y, x);
    if (t < 0.0) {
        t += kTwoPi;
    }
    return t;
}

/// Convert an ANGLE measured from the major axis (what AutoCAD's ELLIPSE Arc prompts ask
/// for) to the parameter of the point in that direction.
inline double angle_to_param(double angle, double ratio) noexcept {
    if (ratio <= 1e-12) {
        return angle;
    }
    double t = std::atan2(std::sin(angle), ratio * std::cos(angle));
    if (t < 0.0) {
        t += kTwoPi;
    }
    return t;
}

/// Is parameter t inside the ellipse's [start, start+sweep] range (full: always)?
inline bool param_in_range(const EllipseData& e, double t) noexcept {
    if (is_full(e)) {
        return true;
    }
    double d = std::fmod(t - e.start, kTwoPi);
    if (d < 0.0) {
        d += kTwoPi;
    }
    return d <= sweep_of(e) + 1e-9;
}

/// Chord count for `tolerance` (max chord error), bounded, from the major radius --
/// the same rule arcs use, so an ellipse never looks coarser than a circle of its size.
inline std::size_t segment_count(const EllipseData& e, double tolerance) noexcept {
    const double r = length(e.major);
    const double sweep = sweep_of(e);
    if (r <= 0.0) {
        return 1;
    }
    double max_step = kHalfPi;
    if (tolerance > 0.0 && tolerance < r) {
        max_step = 2.0 * std::acos(1.0 - tolerance / r);
    }
    if (!(max_step > 0.0)) {
        max_step = kHalfPi;
    }
    const double n = std::ceil(sweep / max_step);
    return static_cast<std::size_t>(std::clamp(n, 8.0, 4096.0));
}

/// Vertices from start to start+sweep inclusive (a full ellipse ends where it began).
inline void tessellate(const EllipseData& e, double tolerance, std::vector<Vec2>& out) {
    const std::size_t n = segment_count(e, tolerance);
    const double sweep = sweep_of(e);
    out.reserve(out.size() + n + 1);
    for (std::size_t k = 0; k <= n; ++k) {
        const double t = e.start + sweep * (static_cast<double>(k) / static_cast<double>(n));
        out.push_back(point_at(e, t));
    }
}

/// Axis-aligned bounds of the drawn portion.
inline void bounds(const EllipseData& e, Vec2& mn, Vec2& mx) {
    std::vector<Vec2> pts;
    tessellate(e, 0.0, pts); // coarse but bounded: 8..(sweep/90deg) chords -- use finer:
    pts.clear();
    const std::size_t n = 64;
    const double sweep = sweep_of(e);
    for (std::size_t k = 0; k <= n; ++k) {
        pts.push_back(point_at(e, e.start + sweep * (static_cast<double>(k) / static_cast<double>(n))));
    }
    // Include the true extreme points when they fall inside the range, so a full
    // ellipse's box is exact regardless of sampling.
    const double phi = std::atan2(e.major.y, e.major.x);
    const double a = length(e.major);
    const double b = a * e.ratio;
    // dx/dt = 0 at t where tan t = -b sin(phi) / (a cos(phi)); dy/dt = 0 at tan t = b cos(phi)/(a sin(phi)).
    const double tx = std::atan2(-b * std::sin(phi), a * std::cos(phi));
    const double ty = std::atan2(b * std::cos(phi), a * std::sin(phi));
    for (const double t0 : {tx, tx + kPi, ty, ty + kPi}) {
        double t = std::fmod(t0, kTwoPi);
        if (t < 0.0) {
            t += kTwoPi;
        }
        if (param_in_range(e, t)) {
            pts.push_back(point_at(e, t));
        }
    }
    mn = mx = pts.front();
    for (const Vec2& p : pts) {
        mn = {std::min(mn.x, p.x), std::min(mn.y, p.y)};
        mx = {std::max(mx.x, p.x), std::max(mx.y, p.y)};
    }
}

} // namespace musacad::core::ellipse
