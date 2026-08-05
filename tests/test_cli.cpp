// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

// The shipped binary's command line (issue #11). The parser is a Qt-free library so
// these assertions run in the normal unit-test binary; the end-to-end exit codes of
// the real executable are asserted separately by tests/cli_check.cmake.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/app/cli.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad;
using musacad::app::CliOptions;

namespace {

/// parse_cli takes (argc, argv); build one from a vector of literals.
CliOptions parse(const std::vector<const char*>& args) {
    return app::parse_cli(static_cast<int>(args.size()), args.data());
}

std::filesystem::path temp_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

} // namespace

TEST_CASE("parse_cli: bare invocation opens the GUI with no drawing") {
    const CliOptions o = parse({"musacad"});
    REQUIRE(o.error.empty());
    REQUIRE(o.mode == CliOptions::Mode::Gui);
    REQUIRE(o.input.empty());
    REQUIRE(o.qt_args.size() == 1); // argv[0] only
}

TEST_CASE("parse_cli: a positional file is the drawing to open") {
    const CliOptions o = parse({"musacad", "part.musa"});
    REQUIRE(o.error.empty());
    REQUIRE(o.mode == CliOptions::Mode::Gui);
    REQUIRE(o.input == "part.musa");
    REQUIRE_FALSE(o.input_is_dxf);

    const CliOptions d = parse({"musacad", "legacy.DXF"});
    REQUIRE(d.input_is_dxf); // extension detection is case-insensitive
}

TEST_CASE("parse_cli: --help / --version / --check select their modes") {
    REQUIRE(parse({"musacad", "--help"}).mode == CliOptions::Mode::Help);
    REQUIRE(parse({"musacad", "-h"}).mode == CliOptions::Mode::Help);
    REQUIRE(parse({"musacad", "--version"}).mode == CliOptions::Mode::Version);
    REQUIRE(parse({"musacad", "-v"}).mode == CliOptions::Mode::Version);

    const CliOptions c = parse({"musacad", "--check", "part.musa"});
    REQUIRE(c.error.empty());
    REQUIRE(c.mode == CliOptions::Mode::Check);
    REQUIRE(c.input == "part.musa");

    // --help wins over anything else on the line (so a broken line can still ask for help).
    REQUIRE(parse({"musacad", "--check", "--help"}).mode == CliOptions::Mode::Help);
}

TEST_CASE("parse_cli: malformed lines are usage errors, not silent defaults") {
    REQUIRE_FALSE(parse({"musacad", "--check"}).error.empty());              // no file
    REQUIRE_FALSE(parse({"musacad", "--nonsense"}).error.empty());           // unknown option
    REQUIRE_FALSE(parse({"musacad", "a.musa", "b.musa"}).error.empty());     // two drawings
}

TEST_CASE("parse_cli: single-dash options are forwarded to Qt untouched") {
    // Qt owns the single-dash namespace (-platform, -style, -qwindowgeometry…). Rejecting
    // them would break `musacad -platform offscreen`; claiming them would shadow Qt.
    const CliOptions o = parse({"musacad", "-platform", "offscreen", "part.musa"});
    REQUIRE(o.error.empty());
    REQUIRE(o.input == "part.musa");
    REQUIRE(o.qt_args.size() == 3); // argv[0], -platform, offscreen
    REQUIRE(o.qt_args[1] == "-platform");
    REQUIRE(o.qt_args[2] == "offscreen");
}

TEST_CASE("help/version text is non-empty and names the exit codes") {
    const std::string h = app::help_text();
    REQUIRE(h.find("--check") != std::string::npos);
    REQUIRE(h.find("Exit codes") != std::string::npos);
    REQUIRE(app::version_text().find("Musa CAD") == 0);
}

TEST_CASE("check_drawing accepts a real serialized drawing and rejects a broken one") {
    // A genuine document through the real writer -- not a hand-typed fixture, so this
    // cannot rot when the format version bumps.
    core::io::Document doc;
    doc.layers.push_back(core::Layer{"0", {255, 255, 255}, core::Linetype::Continuous, 25, true,
                                     false, false});
    doc.lines.push_back(core::io::DocLine{{0.0, 0.0}, {100.0, 0.0}, {}});
    doc.lines.push_back(core::io::DocLine{{100.0, 0.0}, {100.0, 50.0}, {}});

    const std::filesystem::path good = temp_file("musacad_cli_good.musa");
    {
        std::ofstream out(good, std::ios::binary);
        out << core::io::serialize_native(doc);
    }

    std::string message;
    REQUIRE(app::check_drawing(good.string(), false, message) == app::kExitOk);
    REQUIRE_FALSE(message.empty()); // the loader reports what it read

    const std::filesystem::path bad = temp_file("musacad_cli_bad.musa");
    {
        std::ofstream out(bad, std::ios::binary);
        out << "MUSACAD 14\nLINE not a number\nEND\n";
    }
    REQUIRE(app::check_drawing(bad.string(), false, message) == app::kExitLoad);
    REQUIRE_FALSE(message.empty()); // and says why

    // A missing file is a load failure, not a crash or a silent success.
    REQUIRE(app::check_drawing(temp_file("musacad_cli_absent.musa").string(), false, message) ==
            app::kExitLoad);

    std::error_code ec;
    std::filesystem::remove(good, ec);
    std::filesystem::remove(bad, ec);
}
