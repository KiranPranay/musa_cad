<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Changelog

All notable changes to Musa CAD are recorded here. This project aims to follow
[Semantic Versioning](https://semver.org/).

## v0.2.0 — the annotation release

Full notes: [`docs/release-notes/v0.2.0.md`](docs/release-notes/v0.2.0.md). **Linux only**;
the Windows installer is built and verified separately on real hardware (#6).

### Added
- **Dimension tolerances, fit classes, prefixes and the basic box** (#7) — symmetric,
  limits (stacked), basic (boxed) and reference modes. The measured value is still computed
  from the definition points and can never be authored.
- **Dimension text override** (#20) — AutoCAD's `<>` field: `<> H7` tracks the geometry;
  an override without `<>` replaces the value, shown explicitly rather than silently.
- **Dimension text position** (#21) — a grip on the text of every dimension type, a
  connector leader when the label leaves its dimension line, and "home text" in the
  Properties palette.
- **ISO 129-1 narrow-dimension fallback** (#12) — the value moves outside and the
  arrowheads flip inward when they no longer fit, decided independently for text and arrows.
- **GD&T** (#8) — feature control frames and datum feature symbols sharing DIMSTYLE, so
  GD&T annotation matches the drawing's dimensions automatically. `TOLERANCE`, `DATUM`.
- **26 drafting symbols in the stroke font** (#9) — hole callouts, the GD&T characteristic
  set, material-condition modifiers, feature-form symbols; reachable via `%%b`/`%%h`/`%%v`
  and the general `\U+XXXX` escape.
- **TABLE entity + TABLESTYLE** (#22) — BOMs, revision blocks, hole schedules; merged
  cells, per-cell alignment, style-driven text heights. `TABLE`/`TB`.
- **Command line** (#11) — `musacad <drawing>`, `--check`, and headless `--plot` with
  paper/orientation/scale/window options. Exit codes 0/1/2/3. See `docs/CLI.md`.
- **Raster IMAGE entity** (#10, **partial**) — external or base64-embedded payloads;
  selectable, movable and plotted. Viewport display, IMAGEATTACH/IMAGECLIP and DXF deferred.
- `docs/ROADMAP.md` — a grouped survey of what is not yet implemented (issues #20–#33).

### Fixed
- **Plotted text ignored its lineweight** (#19). **Behaviour change:** text now prints at
  its resolved weight instead of a hairline; on-screen appearance is unchanged.
- A **hatch loaded from a file was never spatially indexed**, so it could not be picked,
  hovered, window-selected or erased.
- **Fifteen printable ASCII characters had no glyph**, so e.g. `50% FULL` plotted as
  `50 FULL`.
- The **basic-dimension frame** drew its lower edge on the dimension line.

### Compatibility
Native format **v20** (was v14); every older version still loads, each with a regression
test. Files written by v0.2.0 cannot be opened by v0.1.0. GD&T, tables and images are
native-only — the DXF gaps are stated, not faked.

---

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
