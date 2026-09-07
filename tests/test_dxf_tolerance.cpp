// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Issue #31: GD&T feature control frames through DXF TOLERANCE, both directions.

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"

using namespace musacad::core;
using Catch::Approx;

TEST_CASE("#31 DXF TOLERANCE: a frame's cells, symbols, style and rotation round-trip; a datum becomes a one-cell frame") {
    io::Document doc;
    DimStyle st;
    st.name = "GDT";
    doc.dimstyles.push_back(st); // a fresh Document already holds "Standard" at index 0
    io::DocFcf f;
    f.cells = {"\\U+2316", "\\U+2300 0.1 \\U+24C2", "A", "B"}; // position, dia 0.1 MMC, datums
    f.pos = {10, 20};
    f.rotation = kHalfPi;
    f.style = static_cast<std::uint16_t>(doc.dimstyles.size() - 1);
    doc.fcfs.push_back(f);
    io::DocDatum d;
    d.letter = "C";
    d.tip = {0, 0};
    d.pos = {5, 5};
    doc.datums.push_back(d);

    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_fcf.dxf";
    REQUIRE(io::save_dxf(doc, p.string()).ok);
    {
        std::ifstream in(p);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(all.find("TOLERANCE") != std::string::npos);
        REQUIRE(all.find("{\\Fgdt;j}%%v{\\Fgdt;n} 0.1 {\\Fgdt;m}%%vA%%vB") != std::string::npos);
    }
    io::Document back;
    REQUIRE(io::load_dxf(p.string(), back).ok);
    REQUIRE(back.fcfs.size() == 2); // the frame + the datum's one-cell frame
    const io::DocFcf& g = back.fcfs[0];
    REQUIRE(g.cells.size() == 4);
    REQUIRE(g.cells[0] == "\\U+2316");
    REQUIRE(g.cells[1] == "\\U+2300 0.1 \\U+24C2");
    REQUIRE(g.cells[2] == "A");
    REQUIRE(g.pos == Vec2{10, 20});
    REQUIRE(g.rotation == Approx(kHalfPi));
    REQUIRE(back.dimstyles.at(g.style).name == "GDT");
    REQUIRE(back.fcfs[1].cells.size() == 1);
    REQUIRE(back.fcfs[1].cells[0] == "C");
    std::filesystem::remove(p);
}
