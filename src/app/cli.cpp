// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/app/cli.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/version.hpp"

namespace musacad::app {

namespace {

/// Options we claim. Everything else that starts with a SINGLE dash belongs to
/// Qt (`-platform`, `-style`, `-qwindowgeometry`, …) and is forwarded untouched;
/// an unknown DOUBLE-dash option is a usage error, because Qt's are single-dash.
bool is_ours(std::string_view a) {
    return a == "--help" || a == "-h" || a == "--version" || a == "-v" || a == "--check";
}

/// The Qt options that consume a FOLLOWING value (per the Qt 6 QGuiApplication /
/// QApplication documented command line). We must know these, or `musacad -platform
/// offscreen part.musa` would read "offscreen" as the drawing. Anything else starting
/// with a single dash is treated as a valueless flag and forwarded as-is.
bool qt_option_takes_value(std::string_view a) {
    constexpr std::string_view kWithValue[] = {
        "-display",      "-geometry",         "-graphicssystem",  "-platform",
        "-platformpluginpath", "-platformtheme", "-plugin",       "-qmljsdebugger",
        "-qwindowgeometry",    "-qwindowicon",   "-qwindowtitle", "-session",
        "-style",        "-stylesheet",
    };
    for (const std::string_view k : kWithValue) {
        if (a == k) {
            return true;
        }
    }
    return false;
}

} // namespace

bool path_is_dxf(const std::string& path) {
    if (path.size() < 4) {
        return false;
    }
    std::string tail = path.substr(path.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".dxf";
}

CliOptions parse_cli(int argc, const char* const* argv) {
    CliOptions o;
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        o.qt_args.emplace_back(argv[0]);
    }
    bool want_check = false;
    bool have_input = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i] != nullptr ? argv[i] : "";
        if (a == "--help" || a == "-h") {
            o.mode = CliOptions::Mode::Help;
            return o; // an explicit help request wins over anything else on the line
        }
        if (a == "--version" || a == "-v") {
            o.mode = CliOptions::Mode::Version;
            return o;
        }
        if (a == "--check") {
            want_check = true;
            continue;
        }
        if (a.size() > 2 && a.substr(0, 2) == "--" && !is_ours(a)) {
            o.error = "unknown option: " + std::string(a);
            return o;
        }
        if (!a.empty() && a[0] == '-' && a != "-") {
            // A Qt option; carry it -- and its value, when it takes one -- across untouched,
            // so the value is never mistaken for the drawing argument.
            o.qt_args.emplace_back(a);
            if (qt_option_takes_value(a) && i + 1 < argc && argv[i + 1] != nullptr) {
                o.qt_args.emplace_back(argv[i + 1]);
                ++i;
            }
            continue;
        }
        if (have_input) {
            o.error = "unexpected extra argument: " + std::string(a);
            return o;
        }
        o.input = std::string(a);
        have_input = true;
    }

    if (want_check) {
        if (!have_input) {
            o.error = "--check needs a drawing file";
            return o;
        }
        o.mode = CliOptions::Mode::Check;
    }
    o.input_is_dxf = path_is_dxf(o.input);
    return o;
}

std::string help_text() {
    return "Musa CAD -- 2D CAD engine\n"
           "\n"
           "Usage:\n"
           "  musacad [<drawing>]              open a drawing in the GUI\n"
           "  musacad --check <drawing>        parse a drawing and report errors, then exit\n"
           "  musacad --help                   show this help\n"
           "  musacad --version                show the version\n"
           "\n"
           "<drawing> is a .musa file, or a .dxf file (loaded through the DXF importer).\n"
           "\n"
           "Exit codes:\n"
           "  0  success\n"
           "  1  usage error (bad command line)\n"
           "  2  the drawing could not be read or parsed\n"
           "  3  the output could not be written\n"
           "\n"
           "Single-dash options are passed through to Qt (e.g. -platform offscreen).\n";
}

std::string version_text() {
    return std::string(core::app_name()) + " " + std::string(core::version_string());
}

int check_drawing(const std::string& path, bool dxf, std::string& message) {
    // The SAME loaders GeometryEngine::load_document_replace calls -- one parse
    // path, so `--check` can never disagree with what opening the file would do.
    core::io::Document doc;
    const core::io::IoResult r =
        dxf ? core::io::load_dxf(path, doc) : core::io::load_native(path, doc);
    message = r.message;
    return r.ok ? kExitOk : kExitLoad;
}

} // namespace musacad::app
