<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Changelog

All notable changes to Musa CAD are recorded here. This project aims to follow
[Semantic Versioning](https://semver.org/).

## v0.1.0 — first public preview

An **honest, early v0.1.0**: a capable AutoCAD-style 2D drafting application built on a
multi-threaded, GPU-accelerated core — useful for real 2D work, but young. Not "stable" or
"feature-complete." Licensed under **LGPL-3.0-or-later** (see [`LICENSE`](LICENSE)).

### What Musa CAD does
- **Draw:** line, polyline (with per-vertex arc bulges), circle, arc, rectangle.
- **Modify:** erase, move, copy, mirror, offset, rotate, scale, array (rectangular + polar),
  trim, extend, fillet (incl. polyline-corner arcs), chamfer.
- **Precision:** object snaps (OSNAP), ortho/polar tracking, grid, and **grip** direct
  manipulation; dynamic input (DYN) mirroring the command line.
- **Layers & properties:** layer manager (on/freeze/lock, colour/linetype/lineweight,
  ByLayer), current-layer control, and a context-sensitive **Properties palette**.
- **Annotation:** single-line TEXT, paragraph **MTEXT**, **QLEADER**; dimensions
  (linear, aligned, radius, diameter, angular, and a smart `DIM`) with editable dimension
  styles and per-dimension overrides; **TrueType/OpenType fonts** plus SHX-name → stroke/TTF
  substitution.
- **Drafting symbols & GD&T glyphs:** the stroke font carries the hole-callout symbols (counterbore/spotface, depth, countersink), the GD&T characteristic set, the material-condition modifiers and the feature-form symbols, reachable from any text entity via `%%b`/`%%h`/`%%v` and the general `\U+XXXX` escape.
- **Dimension tolerances:** prefix/suffix plus a tolerance mode (symmetric / limits / basic / reference) on the dimension itself, so a fit class or limit pair travels with the dimension it qualifies. The measured value is still computed from the definition points and can never be authored.
- **ISO 129-1 narrow dimensions:** a value that will not fit between its extension lines is now placed outside and the arrowheads flip to point inward, instead of colliding with the extension line. Independently resolved for text and arrows; `Text fit` overrides it per dimension.
- **Dimension text override (#20):** a dimension can carry an AutoCAD-style text override where `<>` stands in for the measurement — `<> H7`, `2x<>`, `⌀<> THRU` — so the text tracks the geometry. An override without `<>` replaces the value, which the Properties palette shows explicitly. The measurement is always still computed.
- **Dimension text position (#21):** every dimension now has a grip on its text. Drag it anywhere; the value stays measured, a connector leader is drawn when the label leaves its dimension line, and the Properties palette's `Text moved` row restores it ("home text").
- **Fixed — plotted text ignored its lineweight (#19):** stroke-font TEXT/MTEXT/leader/dimension glyphs always printed as a cosmetic hairline regardless of the entity's lineweight, making dimension text and notes unreadable on paper. Text now plots at its resolved weight like every other stroked entity. **Behaviour change:** text that relied on the old hairline output will now print at its actual (often ByLayer 0.25 mm) weight. On-screen appearance is unchanged.
- **Raster images (partial):** a `.musa` can now carry an image — a logo, a shaded pictorial — either as an external path relative to the drawing or base64-embedded so the file stays self-contained. Images are selectable, movable, bounds-correct and **plot** at output resolution. Viewport display, the IMAGEATTACH/IMAGECLIP commands and DXF interop are deferred and documented.
- **GD&T:** feature control frames (`TOLERANCE`) and datum feature symbols (`DATUM`) as real entities — selectable, movable and re-laid-out as a unit, with cell proportions derived from the dimension style so GD&T annotation matches the drawing's dimensions automatically. Native format only; DXF interop is a stated gap.
- **Command line:** `musacad <drawing>` opens a file, `--check` validates one (non-zero exit on a parse error), and `--plot <drawing> <out.pdf>` plots headlessly with no display — same loaders, snapshot builder and plot renderer as the GUI. See [`docs/CLI.md`](docs/CLI.md).
- **Blocks:** block definitions + `INSERT` references, including nested blocks.
- **Files:** native `.musa` format (round-trips every entity family); **DXF import/export**;
  **DWG import/export** via an external converter (see below); **SPLINE** and **ELLIPSE** now
  import from DXF/DWG (de Boor NURBS evaluation).
- **Plot:** vector **PDF** output and physical **printer** support — paper/orientation/area
  (Display/Extents/Window)/scale/lineweights/CTB plot styles (None/Mono/Grayscale)/copies,
  with saved page setups persisted in the drawing.
- **Branding/About:** application/window icon and a Help → About dialog.

### Known limitations & staged work (honest scope)
- **DWG/DXF import fidelity:** unsupported entities are **catalogued and reported**, not
  silently dropped — currently skipped: `HATCH`, `SOLID`, `POINT`, dimensions/leaders inside
  blocks, and proxy/exotic entities. (`SPLINE`/`ELLIPSE` are now imported.)
- **Fonts:** SHX fonts render via a faithful TTF/stroke **substitution**; true SHX
  shape-file parsing is staged. A first-class text-style table is staged.
- **MTEXT:** inline per-character formatting is flattened to plain text on import.
- **Plot:** model space only (no paper-space layouts/viewports); built-in CTB styles only
  (no editable `.ctb` pen tables); no plot stamp / batch publish / raster output.
- **Properties / dialogs:** some deep property groups and per-command modal/dynamic-input
  dialogs are staged; a few numeric-geometry edits are read-only.
- **Scope:** Musa CAD is a **2D** engine; 3D B-rep is not part of this release.

See [`docs/TODO.md`](docs/TODO.md) for the full deferred-work backlog (with rationale).

### DWG support & licensing
DWG import/export is performed by an **external converter** (LibreDWG `dwg2dxf` or the ODA
File Converter) invoked as a **separate process** — no DWG library is linked into or shipped
with Musa CAD, which keeps Musa CAD LGPL-clean. Install a converter separately to enable DWG
(see [`docs/BUILD.md`](docs/BUILD.md)). Dependency licenses and the GPL-boundary evidence are
in [`docs/THIRD_PARTY_LICENSES.md`](docs/THIRD_PARTY_LICENSES.md).

### Build
Build from source per [`docs/BUILD.md`](docs/BUILD.md) (CMake + a C++23 compiler + Qt 6;
optional external DWG converter for DWG support).
