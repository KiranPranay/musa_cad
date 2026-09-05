# Night report — 2026-09-05/06 unattended run

The six objectives you left. **1–5 are complete. 6 is an open-ended set of epics, so it is
partly done and fully analysed** — every open issue has a status and a reason below.

Nothing is released, per your instruction. 12 commits on `fix/stretch-window-picks`,
merged to `main`.

*(The previous run's report is in git history at `cf5c373`.)*

---

## The headline: STRETCH was broken by ORTHO, not by the stretch code

You said *"the stretch is not working as intended… verify carefully."* I did, and the
engine was **correct all along** — every stretch rule and every existing test passed. The
defect was in the pick path in front of it.

A crossing window is a **screen region, not geometry**. AutoCAD applies neither object
snap nor ortho/polar to its corners. Musa CAD applied both, through the shared
`resolve_pick()` that every coordinate pick goes through:

- **ORTHO was the killer.** `StretchCommand` seeds the last point from the first window
  corner, so ortho forced the second corner onto an axis through it. The crossing window
  came out with **zero area** and caught nothing. Drag a box over half a rectangle, get
  silence. With OSNAP and ORTHO on — the ordinary drafting setup, and what your status bar
  shows — STRETCH could essentially never work.
- **OSNAP** separately pulled a corner onto a vertex of the very object being windowed.

`ICommand` grew `wants_window()`, true only while the next pick is a window corner, so the
bypass is scoped: STRETCH's later base and displacement picks still snap and still honour
ortho, which is what AutoCAD does. I verified the tests catch it by reverting the fix —
with the bypass gone, the end-to-end case never even reaches "Stretched", confirming the
window caught nothing.

STRETCH also had **no visual feedback at all**. The crossing window now rubber-bands as you
drag it, the displacement step draws a rubber line from the base point, and the window can
be dragged out **press-drag-release** in one gesture as well as clicked corner-to-corner.

---

## Objectives

| # | Objective | State |
|---|-----------|-------|
| 1 | DYN on by default | **Done** — and a real bug found |
| 2 | Resize table rows and columns | **Done** |
| 3 | Text inserts into tables | **Done** |
| 4 | STRETCH works as in AutoCAD | **Done** — root cause was ORTHO/OSNAP on window corners |
| 5 | All AutoCAD array commands | **Done** — ARRAYPATH is new; see the note on ARRAYEDIT |
| 6 | Analyse and fix all issues | **Partly fixed, fully analysed** — see the table below |

### 1. DYN on by default

It already defaulted ON at startup and has since v0.1.0, and your own config says
`enabled=true`. But the default was written **twice**, and the second copy said `false`:
`selftest_dyn()` saved and restored the preference with a `false` default, so running the
harness on a fresh profile **silently persisted DYN off**. There is now one named constant,
and a new self-test asserts a fresh profile resolves ON.

Worth knowing: the self-test harness deliberately forces DYN **off** so captures run in a
canonical state. If you have been reading DYN's state off my screenshots, that is why it
looked off.

### 2 & 3. Tables

- **Rows resize now, not just columns.** The old comment claimed row heights follow the
  style's text height. They do not — they are stored per table in `row_heights` and are
  exactly what the grid is laid out from. Each interior row boundary gets a grip on the
  table's left edge. Both axes measure the drag in the **table's own frame**, so a rotated
  table resizes along itself, and a drag that would produce a non-positive size is refused
  on both axes.
- **Cells accept text.** Rather than a parallel editing path, a table is now treated as the
  text-bearing entity it is: `EditTextContentCommand` resolves the picked point to a cell
  and rewrites only that cell. **The existing double-click gesture and DDEDIT therefore both
  work on tables with no new UI**, and one undo group preserves position, sizes, style and
  layer. Every visible cell is an edit target, **empty ones included** — an empty cell is
  precisely the one you want to type into. Targets carry the **raw** stored string, so
  reopening a cell containing `%%c` shows what you typed, not the diameter glyph.

### 5. The array family

AutoCAD ships seven array commands. The four that **create** an array are now all here:

| Command | State |
|---|---|
| `ARRAY` / `-ARRAY` (asks the type) | Implemented — `PA`/`PO` told apart, bare `P` refused as ambiguous |
| `ARRAYRECT` | Implemented — plus AutoCAD's **axis angle** |
| `ARRAYPOLAR` | Implemented |
| `ARRAYPATH` | **New** — Divide and Measure, aligned or not |
| `ARRAYEDIT` / `ARRAYCLOSE` | **Not applicable** — see below |

`ARRAYPATH` distributes along any tessellable curve. Divide spreads N items over the whole
path (a **closed** path divides by N rather than N−1, so nothing doubles at the seam);
Measure steps by distance, optionally capped, never past the end. Aligned items turn by how
far the tangent has swung **since the first station**, so item 1 keeps the orientation you
drew. Stations and tangents come from one tessellation, so an item cannot land somewhere the
curve does not go.

`ARRAY` keeps AutoCAD's legacy four rectangular prompts unchanged; the axis angle is on
`ARRAYRECT`, which is where AutoCAD puts it.

**Why ARRAYEDIT and ARRAYCLOSE are absent, and it is not laziness:** both edit an
**associative** array — a parametric entity that remembers its source objects and
parameters so the pattern can be re-driven. Musa CAD's arrays are non-associative: ordinary
independent copies, exactly what AutoCAD's own `-ARRAY` produces. There is no association
for `ARRAYEDIT` to reopen. Adding them means adding associative arrays first, which is a
**data-model change (a new parametric entity kind)**, not a command. `docs/COMMANDS.md` now
says this.

---

## Real bugs found and fixed (beyond the objectives)

1. **STRETCH vs ORTHO/OSNAP** — above. The user-visible one.
2. **The ARRAY dialog was passing the undo-group id in as the axis angle.** My own change
   caused it: adding `angle` before `group` in `ArrayRectCommand` silently shifted two
   **positional** initialisations, one of them the GUI dialog. The compiler cannot catch
   this — both are numbers. Both call sites now use designated initialisers.
3. **A flaky test, and it was a genuine race.** Two grip tests failed ~1 run in 4 under
   parallel load. Not a slow timeout, as I first assumed: they submitted `Begin` then `Move`
   and waited only for the preview to be **non-empty** — but `Begin` publishes a preview of
   its own at the grip's *original* position, so the wait could return one snapshot early
   and the assertion then checked the moved position against the un-moved preview. Now
   verified with **12 consecutive full runs at -j32**, all green.
4. **The GUI self-test had been failing, silently, for two releases.** Two assertions went
   stale when later features landed: dimension grips expected 5 but issue #21 added the
   label grip making 6; the DWG gap catalogue used a `HATCH` as its "unsupported entity",
   then hatch import landed, so the check **stopped testing anything and started failing**.
   Both fixed; the harness is `overall: PASS` again. It was invisible because it needs a
   real window — run offscreen it fails ~40 checks for want of `setParent`, burying the two
   that mean something.

---

## New commands added tonight

All are registered, documented in `docs/COMMANDS.md`, icon'd, and covered by tests.

| Command | Notes |
|---|---|
| `ARRAYPATH`, `ARRAYRECT`, `ARRAYPOLAR`, `-ARRAY` | The array family |
| `POINT` (`PO`) | Points were stored, drawn, picked, snapped and persisted — everything except a way to **make** one |
| `DIVIDE` (`DIV`) | n−1 marks on an open curve, **n on a closed one** (no free end to count from) |
| `MEASURE` (`ME`) | A mark every d from the start, never one **at** the start |
| `POLYGON` (`POL`) | Centre (Inscribed/Circumscribed) or Edge |
| `BREAK` (`BR`) | Lines, arcs, circles, open **and** closed polylines |
| `BREAKATPOINT` | Split with no gap |
| `ALIGN` (`AL`) | Two point pairs, optional uniform scale |
| `LENGTHEN` (`LEN`) | DElta / Percent / Total, on lines and arcs |

`AddPointCommand` was wired through every path a new entity kind must reach to be a
first-class citizen — capture, add-to-store, the property fan-out, and all five transforms
— so points move, copy, array, mirror and stretch like anything else instead of being
decoration that edit commands skip.

`DIVIDE`, `MEASURE` and `ARRAYPATH` share one arc-length sampler (`PathSampler`), so the
three cannot disagree about where "40% along this arc" is — marks land **on** a curve, not
on the chord between its ends.

---

## Testing

You asked that everything be tested "either by script or gui control". Both.

- **Unit/integration: 526 tests, all passing** (up from 459). 12 consecutive full runs at
  `-j32` green after the flake fix.
- **GUI: a new real-window self-test section** drives each new or fixed command through the
  **real `CommandProcessor`** — the same route your keystrokes take, prompts and parsing
  included — not by submitting engine commands directly:
  - STRETCH **with ORTHO ON**, the exact configuration that used to defeat it
  - TABLE places, and a cell accepts text
  - A table row grip resizes its row
  - POLYGON, DIVIDE, BREAK, ARRAYPATH
- Whole harness: `[selftest] overall: PASS`, run three times.

One thing writing that surfaced: the processor's selection count is a **cache refreshed by
a 100 ms UI timer**, and selection-gated commands read that cache. A person cannot type a
command name inside 100 ms, but the harness can. I left it as a poll — the snapshot publish
happens on the render thread, and making it write the processor's state would introduce a
**real data race to fix a window no human can hit**.

---

## Objective 6: every open issue, analysed

| Issue | Title | Status after tonight |
|---|---|---|
| **#27** | Modify gaps | **5 of 5 missing commands done**: BREAK, LENGTHEN, ALIGN, DIVIDE, MEASURE. Remaining: curve TRIM/EXTEND/FILLET (the *modified* entity must still be a line) |
| **#23** | Draw commands | **POINT and POLYGON done.** Remaining: SPLINE, ELLIPSE, XLINE/RAY |
| #30 | Inquiry + housekeeping | DIST/AREA/ID/LIST landed last run. Remaining: PURGE, AUDIT, UNITS |
| #28 | Dimension types | DIMBASELINE/DIMCONTINUE landed last run. Remaining: DIMORDINATE, DIMJOGGED, DIMARC |
| #33 | Smaller parity gaps | Untouched — but see the DONUT note below |
| #10 | Raster IMAGE entity | Partial slice landed last run |
| #25 | Block authoring | Untouched (epic) |
| #26 | Paper space / layouts | Untouched (epic) |
| #29 | Text STYLE table | Untouched |
| #31 | DXF interop gaps | Untouched |
| #32 | Properties palette + input | Untouched |
| #1–#6 | Packaging / platform | Untouched — these need Windows/macOS hardware, a Flathub account, or CI changes, none of which I can do from here |

### Judgement calls you may want to overrule

- **DONUT was deliberately left out** even though it looks like an easy win. An AutoCAD
  donut is a **filled ring drawn as a wide polyline**, and polylines have no width here.
  Drawing it as two concentric circles would *look like two circles*. It waits on polyline
  width rather than shipping a lookalike.
- **ELLIPSE was skipped for the same class of reason.** DXF import already tessellates
  ellipses to polylines ("Musa has no ellipse primitive"), so an ELLIPSE command could
  match that cheaply — but you would be drawing an "ellipse" you cannot later edit as one,
  which breaks the derived-not-baked rule the rest of the model follows. It deserves a real
  entity.
- **ARRAYEDIT/ARRAYCLOSE** — see above.

### What I would do next, in priority order

1. **Curve TRIM/EXTEND** (#27). The boundary side already handles curves; it is the
   modified entity that is line-only. This is the biggest remaining everyday wall.
2. **PURGE and UNITS** (#30). Small, self-contained, no data-model change.
3. **ELLIPSE as a real entity** (#23), which also improves DXF fidelity (#31).
4. **Associative arrays**, which unlocks ARRAYEDIT/ARRAYCLOSE.
5. **Polyline width**, which unlocks DONUT and wide-polyline DXF fidelity.

---

## Final state

- Branch `fix/stretch-window-picks`, merged into `main`. No force-push, no history
  rewritten, no tags or releases touched.
- No commit carries a Claude co-author trailer.
- `cmake --build build/dev` clean; **526/526 tests pass**; GUI self-test `overall: PASS`.
- No release cut, as instructed.
