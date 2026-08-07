# Night report — 2026-08-05/08 unattended run

**Five of six issues closed and merged to local `main`, each with a PR open. One issue
(#10, the raster IMAGE entity) is deliberately parked — not started.**

Every merge passed the full gate: `dev` (ASan/UBSan) clean with **zero warnings** and all
tests green, `release` clean with zero warnings, and `tsan` green. The tree on `main` is
green right now.

---

## Status table

| Issue | State | Branch | PR | Format |
|---|---|---|---|---|
| **#11** No CLI | **Merged** | `feat/issue-11-cli` | [#13](https://github.com/MusaCAD/MusaCAD/pull/13) | — |
| **#9** Drafting symbols | **Merged** | `feat/issue-9-drafting-symbols` | [#14](https://github.com/MusaCAD/MusaCAD/pull/14) | — |
| **#7** Dimension tolerances | **Merged** | `feat/issue-7-dim-tolerances` | [#15](https://github.com/MusaCAD/MusaCAD/pull/15) | v15 |
| **#12** Narrow dimensions | **Merged** | `feat/issue-12-narrow-dims` | [#16](https://github.com/MusaCAD/MusaCAD/pull/16) | v16 |
| **#8** GD&T entities | **Merged** | `feat/issue-8-gdt` | [#17](https://github.com/MusaCAD/MusaCAD/pull/17) | v17 |
| **#10** Raster IMAGE | **Parked — not started** | — | — | (would be v18) |

The PRs **stack**: each branch was cut from `main` after the previous one merged, exactly
as the brief's work order requires, so each PR's diff against `origin/main` includes its
predecessors. Merge them in issue order **#13 → #14 → #15 → #16 → #17** and each collapses
to its own commits. Every PR body names its stack parent.

Artifacts for coffee: `artifacts/issue-{7,8,9,11,12}.pdf`. **If you only look at one, make
it `issue-12.pdf`** — the 21-rung ladder shows the whole narrow-dimension behaviour at a
glance.

---

## Per issue

### #11 — CLI (merged, PR #13)

`musacad drawing.musa` opens a file, `--check` validates one (non-zero exit + error on
stderr), `--plot <in> <out.pdf>` plots headlessly, plus `--help`/`--version`. Exit codes
0/1/2/3. Documented in new `docs/CLI.md`.

The command line is parsed **before any Qt object exists**, which is what lets
`--help`/`--version`/`--check` run with no display. `musacad <file>` submits the existing
`OpenDocumentCommand`; `--check` calls the same `io::load_native`/`load_dxf` the engine
calls. No second load path anywhere.

**The issue's actual complaint** was that consumers re-implement plotting against internal
APIs and "none of that effort lands back in the product". `ui::paint_plot` was already
shared, but three things around it were **copy-pasted**: the tessellation-tolerance rule
(duplicated verbatim in `MainWindow::prepare_plot` and `tools/plot_check.cpp`), the
`QPdfWriter` setup (same two places), and the paper table (private to `plot_dialog.cpp`).
These are now `ui::plot_tolerance()`, `ui::write_plot_pdf()` and `ui::standard_papers()`,
each with both a GUI and a headless caller.

**Deferred:** a scripting API; printer targets from the CLI (PDF only).

### #9 — Drafting symbols (merged, PR #14)

26 new glyphs hand-authored on the existing 6×8 cell: hole callouts, the full GD&T
characteristic set, the material-condition modifiers, and the feature-form symbols.
Reachable via `%%b`/`%%h`/`%%v` and `\U+XXXX`. Because it lands in the **font**, they work
in TEXT, MTEXT, LEADER and dimension text at once.

`\U+XXXX` was widened from MTEXT-only to universal (the `mtext` parameter removed
outright). **That inverted an existing assertion**, so the test change is its own commit
with the reasoning, per your rule.

**Also fixed a pre-existing bug:** fifteen printable ASCII characters had **no glyph** —
`% ! ? $ & @ [ ] { } ^ _ \` | ~` drew blank with only an advance, so `50% FULL` plotted as
`50 FULL`. The header claimed full ASCII coverage, which was untrue. The old test checked
only a curated subset, which is exactly how it hid.

### #7 — Dimension text decoration (merged, PR #15, native v15)

Prefix/suffix into the shared char pool plus a tolerance mode (symmetric / limits / basic /
reference). The measured value is still `dim_measure(def points)`, still never serialised,
still follows a dragged def point — asserted directly.

`compose_dim_label()` is one definition called by both `compute_dim_geometry` and the DXF
exporter, so what a vendor's CAD shows cannot disagree with what we draw. `dim_label_quad()`
was extracted because three paths needed the label rectangle — and **bounds had omitted the
label entirely**, under-reporting the AABB of any dimension whose text sticks out.

MATCHPROP: deliberately **not** matchable, agreeing with your instinct. A fit class is
semantics about *this* feature.

**Deferred:** tolerance semantics through DXF (the composed string is written to the text
override; the semantics are native-only — the same gap already documented for per-dimension
overrides).

### #12 — ISO 129-1 narrow dimensions (merged, PR #16, native v16)

Text and arrows are fitted **independently**, giving the four states the standard
recognises. The ladder artifact shows why that matters: at an 8 mm foot separation the
arrows still fit while the value has already moved out.

Arrows-outside reuses `append_arrowhead` with the direction reversed — it already had a
direction parameter, so no signature change and no second code path. The fit measures the
**decorated** label from #7, which is the dependency that ordered these two.

`text_fit` is a `DimStyle` field plus a `DimOverrides` presence bit, so it resolves through
the existing override-first-else-style path. `Auto` at both levels ⇒ existing drawings
simply stop colliding.

**Deferred (in `docs/TODO.md`):** the short-leader variant of outside text; the fit test for
radius/diameter/angular.

### #8 — GD&T (merged, PR #17, native v17)

Feature control frames and datum feature symbols: own arenas, one geometry function each,
all geometry derived from the effective text height at snapshot time.

**A cell is plain text** in the shared char pool. I rejected per-cell semantic tags: the
meaning already comes from cell position plus the symbols the font carries after #9, so an
author types `\U+2316` and the existing substitution pass does the rest — no GD&T-specific
input mode. **Family: folded into `EntityFamily::Dimension`**, deliberately, because that is
what lets MATCHPROP carry styling from a dimension onto a frame — the literal ask in the
issue. A separate `GdtFamily` would have blocked it.

**Deferred (in `docs/TODO.md`):** DXF `TOLERANCE` interop (nothing is written — stated, not
faked); the Ph11 `ParameterDialog` surface (the command-line Q&A **is** implemented — I
landed one surface properly rather than two badly); editing an existing frame's cell list
from the PR.

### #10 — Raster IMAGE (parked, not started)

**Nothing was started, so there is no half-finished code to clean up.** This was the planned
park: it is the largest and riskiest of the six (new GPU texture type, a decoder seam, an
image-definition table, base64 embedding with path-traversal safety, a plot path and DXF),
and it is the only one with no dependents.

**What I would do, in order** (the brief's own slicing is right):

1. `ImageDef` table on the store, parallel to layers/dimstyles/block defs — `{source, pixel
   w/h, format}` — plus an `ImageData` entity holding a def index, insertion point, size,
   rotation and an optional clip rect. This mirrors BLOCKDEF/INSERT and gives dedup for free.
2. `IImageDecoder` in core + `QtImageDecoder` in the UI layer, injected via the store —
   precisely the `IFontEngine`/`QtFontEngine` seam. Core stays Qt-free,
   `test_header_hygiene` stays green, and the headless CLI injects the same decoder. **Do
   not vendor `stb_image`.**
3. Native v18 with base64 chunked across lines (the format is line-oriented) plus an
   external-path form resolved relative to the drawing, refusing traversal outside it.
   Round-trip proof + older-version load, as every format bump here has.
4. **Plot path first, viewport second.** The QPainter route draws the image with its
   transform and clip; that alone delivers the headline use case (a logo in a title block
   that actually plots) and needs no GPU work. This is the coherent slice to land if time
   runs short again.
5. Only then `GpuTexture` in `render/gpu/` with a GL 4.6 DSA implementation and an image
   shader pair, plus a renderer-side texture cache keyed by def index and invalidated on
   version change.

**The architectural point not to lose:** do **not** put pixels in the snapshot. It is copied
through the triple buffer on every publish; carry a small `ImageInstance` (transform, def
index, clip, def version) and let the renderer hold the texture cache. That constraint is
why step 5 is last, not first.

---

## Things that surprised me about the codebase

**The plot path was already better factored than the issue implied.** `ui::paint_plot` was
device-agnostic and already called by both the GUI and `tools/plot_check.cpp`. The real
debt was three *copy-pasted* helpers around it. #11b took well under its time box as a
result.

**`-Werror=switch` is doing real architectural work.** Adding two `EntityKind` values in #8
produced a compiler-generated checklist of all nine sites that had to handle them, across
six files. Nothing could be missed by accident. This is the single best thing about
extending this codebase.

**Two real pre-existing bugs surfaced, both from the same cause** — a list that was correct
when written and then not extended:

1. **`GeometryEngine::all_live()` omitted hatches.** It feeds the load-time spatial-index
   rebuild, `SelectAll` and `ERASE All`, so **a hatch loaded from a file was never indexed**
   and could not be picked, hovered, window-selected or erased. One created in-session
   worked (`create_indexed` inserts directly), which is exactly why it hid. Fixed in #8 with
   a regression test verified to fail without the fix. *Worth checking whether anything else
   iterates arenas by hand.*
2. **Fifteen printable ASCII glyphs were missing** (#9 above), hidden by a test that checked
   a curated character subset rather than the range.

**A doc gap:** `ARCHITECTURE.md` had **no Phase 30 (PLOT) section** even though Ph30 is
referenced from the Ph31/Ph33 sections and `COMMANDS.md` lists PLOT as implemented. Written
up as part of #11.

**`docs/BUILD.md`'s TSan instructions need root.** `sudo sysctl -w vm.mmap_rnd_bits=28` was
not available here. `setarch "$(uname -m)" -R` disables ASLR per process and the personality
is inherited by children, so wrapping both the build *and* `ctest` works with no root — now
documented.

**The visual artifact requirement earned its keep three times**, catching things every
numeric test passed:

- #9: four glyphs technically correct and visually wrong — "profile of a line" rendered as a
  sharp peak, flatness collapsed into `//` and became indistinguishable from parallelism.
- #7: the basic-dimension frame's lower edge landed exactly on the dimension line.
- #8: the datum triangle at arrowhead proportions read as an arrow, not a datum symbol.

**One self-inflicted delay worth recording:** ~25 minutes lost to a new engine test that
never published a snapshot. I suspected dispatch, then the variant, then a hang. The cause
was that **my test forgot `engine.start()`** — the worker is started explicitly, not by the
constructor. The product was correct throughout.

---

## Judgement calls you may want to overrule

1. **`tools/tsan.supp` is new** (#11). `cli_check` is the first ctest that executes the Qt
   application binary, so it is the first to meet Qt/glib's own races — `QGuiApplication`
   starts a `QDBusConnection` thread that races with glib's event loop inside
   `libglib`/`libQt6DBus`, with no Musa CAD frame in either stack, under every platform
   plugin (I checked `offscreen` and `minimal`). The suppressions are `called_from_lib:`
   entries scoped to those libraries, wired via a **per-test** `TSAN_OPTIONS`, so the
   concurrency tests still run with none. It mirrors the existing `tools/lsan.supp`
   precedent. A race in our own code still fails — but it *is* a suppression, so it is
   flagged rather than buried.
2. **`--plot` forces the offscreen Qt platform**, deliberately overriding an inherited
   `QT_QPA_PLATFORM`. This machine exports `wayland;xcb`; with that inherited, `--plot`
   aborts. Batch plotting is the point of the option, so reliability won over honouring the
   env var. An explicit `-platform` still wins.
3. **#12 widened the *shared* `DimOverrides` block** for v16 rather than adding `text_fit` to
   the `DIM` record alone. This keeps one definition of an override block, at the cost of
   moving `LEADER`/`MLEADER` records too. Tested, including a leader.
4. **#8 writes no DXF at all** rather than a half-valid `TOLERANCE`.
5. **#7's decoration is not MATCHPROP-matchable; #12's `text_fit` is.** The line I drew is
   semantics vs presentation.

---

## Final state

| | |
|---|---|
| `dev` (ASan/UBSan) | build clean, **zero warnings**, **388/388 tests pass** |
| `release` | build clean, **zero warnings** |
| `tsan` | **388/388 pass** (run under `setarch -R`) |
| Native format | **v17** (was v14); v1–v16 all still load, each with an explicit test |
| Tests | 333 at baseline → **388** (+55) |

**Struct sizes** (all `static_assert`ed): hot path **unchanged** — `LineData` 40 B,
`CircleData` 32 B, `EntityProps` 8 B. Cold arenas: `DimData` 112 → **152 B** (#7),
`FcfData` **88 B**, `DatumData` **104 B**, `FcfCell` **8 B** (#8).

**Draw-call bounds unchanged:** 4 for the scene, 6 with aids. GD&T adds no channel — frame
borders go into the existing line batches and the datum triangle into the existing fill
channel.

**Insert benchmark:** **57–61 ns/line** (release, three runs). My baseline measurement at the
*start* of the night on this machine was **56–67 ns/line**, so there is no regression from
the night's work. Note this machine measures roughly 2× the ~30–35 ns/line quoted in
`ARCHITECTURE.md`; the box was not idle. I did not chase it — nothing tonight touches the
store — but the docs' figure and this machine's do not agree, which is worth a look on an
idle box.

---

## What I would do next, in priority order

1. **Merge the five PRs in stack order** (#13 → #14 → #15 → #16 → #17).
2. **#10, in the five steps above**, landing the plot path before the viewport path.
3. **Audit for more hand-maintained arena lists** like `all_live()`. That bug class cost
   hatches their pickability after a reload; a helper that iterates every arena once would
   make it structurally impossible.
4. **DXF `TOLERANCE` interop** for #8 — the largest honestly-stated gap of the night.
5. The smaller deferrals, all in `docs/TODO.md`: the short-leader variant for outside
   dimension text, the radial-form fit test, the `ParameterDialog` for feature control
   frames, and cell-list editing in the Properties palette.

`NIGHT_LOG.md` has the running, timestamped detail behind all of this, including the
decisions as they were made rather than as they were rationalised afterwards.
