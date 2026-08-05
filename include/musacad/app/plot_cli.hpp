// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>

#include "musacad/app/cli.hpp"

// The `--plot` runner. Separated from cli.cpp because it needs Qt (QPdfWriter /
// QPainter via ui::write_plot_pdf) while the parser stays Qt-free and unit-testable.
// The caller must have constructed a QGuiApplication first.

namespace musacad::app {

/// Load `o.input`, resolve the plot area, build a plot-resolution snapshot and write
/// the PDF -- all through the SAME loaders, snapshot builder and plot renderer the GUI
/// uses. Returns an ExitCode; on failure `error` says why.
///
/// Requires a live QGuiApplication (QPdfWriter needs one). No GL context is created
/// and the renderer is never touched.
[[nodiscard]] int run_plot(const CliOptions& o, std::string& error);

} // namespace musacad::app
