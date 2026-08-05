// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>
#include <vector>

// The shipped binary's command line. Parsed BEFORE any Qt application object
// exists, so `--help`, `--version` and `--check` work with no display and no
// windowing system. Qt-free (core only) and its own target, so the parser is
// unit-testable without linking the GUI.

namespace musacad::app {

/// Process exit codes. Documented in docs/CLI.md -- scripts depend on them, so
/// they are part of the interface, not an implementation detail.
enum ExitCode : int {
    kExitOk = 0,     ///< success
    kExitUsage = 1,  ///< malformed command line
    kExitLoad = 2,   ///< the drawing could not be read or parsed
    kExitOutput = 3, ///< the output could not be written
};

/// The parsed command line. `mode` selects what `main` does; `error` being
/// non-empty means the line was malformed (print it to stderr, exit kExitUsage).
struct CliOptions {
    enum class Mode { Gui, Help, Version, Check };

    Mode mode = Mode::Gui;
    std::string input;         ///< drawing to open (Gui) or validate (Check); "" = none
    bool input_is_dxf = false; ///< derived from the input's extension
    std::string error;         ///< non-empty => usage error

    /// Arguments we did not claim, forwarded verbatim to Qt (single-dash options
    /// like `-platform offscreen`, value included). argv[0] is always element 0.
    std::vector<std::string> qt_args;
};

/// Parses the command line. Never touches the filesystem and never constructs a
/// Qt object, so it is safe to call as the first statement of `main`.
///
/// Grammar (double-dash options are ours; single-dash ones belong to Qt):
///   musacad [<file>]              open a drawing in the GUI ("" = empty drawing)
///   musacad --check <file>        parse the drawing and exit (validator)
///   musacad --help | -h
///   musacad --version | -v
[[nodiscard]] CliOptions parse_cli(int argc, const char* const* argv);

/// The `--help` text (also the source of truth for docs/CLI.md).
[[nodiscard]] std::string help_text();

/// The `--version` line, e.g. "Musa CAD 0.1.0".
[[nodiscard]] std::string version_text();

/// Parses `path` through the SAME core loader the engine uses and returns
/// kExitOk, or kExitLoad with the parser's message in `message`. No Qt.
[[nodiscard]] int check_drawing(const std::string& path, bool dxf, std::string& message);

/// True when `path` ends in ".dxf" (case-insensitive) -- the one place the CLI
/// decides which loader a path implies.
[[nodiscard]] bool path_is_dxf(const std::string& path);

} // namespace musacad::app
