// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/app/plot_cli.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/io_result.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"
#include "musacad/ui/plot.hpp"
#include "musacad/ui/qt_font_engine.hpp"
#include "musacad/ui/qt_image_decoder.hpp"

namespace musacad::app {

int run_plot(const CliOptions& o, std::string& error) {
    // 1. Load through the SAME core loaders the engine's OpenDocumentCommand uses.
    core::io::Document doc;
    const core::io::IoResult r = o.input_is_dxf ? core::io::load_dxf(o.input, doc)
                                                : core::io::load_native(o.input, doc);
    if (!r.ok) {
        error = o.input + ": " + r.message;
        return kExitLoad;
    }

    // 2. The store, with the same Qt font engine the GUI injects -- so TTF/OTF text
    //    plots as real outlines headlessly instead of silently falling back to strokes.
    core::GeometryStore store;
    const ui::QtFontEngine fonts;
    store.set_font_engine(&fonts);
    // The same raster decoder the GUI injects, through the same IImageDecoder seam --
    // so an embedded logo or an external pictorial reaches paper headlessly.
    const ui::QtImageDecoder decoder;
    store.set_image_decoder(&decoder);
    core::io::populate_store(store, doc);

    // 3. The sheet.
    ui::PlotSpec spec;
    if (!ui::resolve_paper(o.plot.paper, o.plot.landscape, spec.paper_w_mm, spec.paper_h_mm)) {
        error = "unknown paper size: " + o.plot.paper;
        return kExitUsage;
    }
    spec.paper = o.plot.paper;
    spec.landscape = o.plot.landscape;
    spec.fit = o.plot.fit;
    spec.scale_num = o.plot.scale_num;
    spec.scale_den = o.plot.scale_den;
    spec.center = true;
    spec.style = o.plot.monochrome ? ui::PlotSpec::Style::Monochrome : ui::PlotSpec::Style::None;
    spec.target = "PDF";

    // 4. The plot area. Extents comes from a probe snapshot's bounds -- the same
    //    snapshot field ZOOM extents and the GUI's Extents plot read.
    core::NativeKernel2D kernel;
    core::RenderSnapshot snap;
    core::Vec2 amin;
    core::Vec2 amax;
    if (o.plot.area == PlotRequest::Area::Window) {
        amin = {o.plot.win[0], o.plot.win[1]};
        amax = {o.plot.win[2], o.plot.win[3]};
        spec.area = ui::PlotSpec::Area::Window;
        spec.win_min = amin;
        spec.win_max = amax;
    } else {
        core::build_render_snapshot(store, kernel, snap, core::kDefaultTessTolerance,
                                    store.ltscale());
        if (!snap.has_bounds) {
            error = o.input + ": nothing to plot (the drawing has no geometry)";
            return kExitLoad;
        }
        amin = snap.bounds_min;
        amax = snap.bounds_max;
        spec.area = ui::PlotSpec::Area::Extents;
    }

    // 5. The plot-resolution snapshot: curves tessellated to the SHARED plot tolerance
    //    (~0.3 px at 300 DPI over the plotted region), then the shared PDF writer.
    const double tol = ui::plot_tolerance(amin, amax, spec.paper_w_mm, spec.paper_h_mm);
    core::build_render_snapshot(store, kernel, snap, tol, store.ltscale());
    // External image sources resolve relative to the DRAWING's directory (and may not
    // escape it), so a .musa that references ./logo.png works wherever it is opened from.
    const std::filesystem::path drawing_dir =
        std::filesystem::absolute(std::filesystem::path(o.input)).parent_path();
    const std::unique_ptr<ui::ImageSource> images =
        ui::make_store_image_source(store, &decoder, drawing_dir.string());
    if (!ui::write_plot_pdf(o.plot.output, snap, spec, amin, amax, error, images.get())) {
        return kExitOutput;
    }
    std::printf("%s -> %s (%s %s, %s, %zu line vertices)\n", o.input.c_str(),
                o.plot.output.c_str(), spec.paper.c_str(),
                spec.landscape ? "landscape" : "portrait", spec.fit ? "fit" : "scaled",
                snap.line_vertices.size());
    return kExitOk;
}

} // namespace musacad::app
