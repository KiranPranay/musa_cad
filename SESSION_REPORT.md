# Session report — 2026-09-04/05

**Two releases cut and installed, five issues resolved, fourteen new issues created from a
full survey of what is missing.** `main` is green on all three presets (455 tests) and
pushed.

---

## What you asked for, and where it landed

| Ask | Result |
|---|---|
| Pull latest | Already up to date; a new issue **#19** had appeared and was fixed first. |
| Solve the open issues | **#19, #20, #21, #22, #24** closed; **#28, #30** partially landed and left open with a comment saying exactly what remains. |
| List + group pending implementations | [`docs/ROADMAP.md`](docs/ROADMAP.md) — ten themed groups (A–J), each measured against AutoCAD's behaviour, with a priority rationale. |
| Create + push issues | **#20–#33** (14 issues), each mapped to a roadmap group. |
| Create a release (Linux only) | **[v0.2.0](https://github.com/MusaCAD/MusaCAD/releases/tag/v0.2.0)** and **[v0.3.0](https://github.com/MusaCAD/MusaCAD/releases/tag/v0.3.0)**, AppImage only. |
| Install it on your PC | `~/.local/bin/musacad` → v0.3.0, plus a desktop entry and icon. Type `musacad` or launch it from your applications menu. |
| Windows | Deliberately untouched — the `.exe` is for the Windows dev to build and verify on real hardware (#6). |

---

## Your three named priorities

You called out *tables*, *dimension positions adjustable like leaders*, and *overriding
dimension text*. All three are done and shipped.

**Dimension text override (#20)** — AutoCAD's `<>` field. `<> H7` keeps tracking the
geometry, `2x<>` and `⌀<> THRU` work, and an override with no `<>` replaces the value as an
explicit choice. The measurement is always still computed, so an override can be inspected
and removed — the entity can't quietly lie.

*One decision worth your review:* an override produces the **whole** label. Prefix/suffix
and the deviation-deriving tolerance modes are not stacked on top, because an author
writing the text is not also asking for text to be generated around it. Basic (the box) and
Reference (the parentheses) still apply, since they frame the label rather than say
anything.

**Dimension text position (#21)** — a grip on the text of **every** dimension type,
including radius and diameter where the text used to be pinned inside the part. Drag it
anywhere; the value stays measured, a connector leader appears once the label leaves its
dimension line, and the Properties palette's *Text moved* row is "home text".

**TABLE entity (#22)** — real tables with merged cells, per-cell alignment and
style-driven title/header/data text heights. `artifacts/issue-22.pdf` shows a BOM, a
revision block and a hole schedule.

## Also implemented

- **#19 (bug)** — plotted text ignored its lineweight. Your reporter's exact repro now
  measures 27 / 62 / 120 / 247 ink per scanline across weights instead of four identical
  hairlines. **Behaviour change:** text now prints at its real weight, so existing sheets
  will print heavier than before.
- **#24 STRETCH** — the crossing-window move. A dimension whose def points are enclosed
  **re-measures**, which fell out of a decision made in Phase 13 rather than needing code.
- **#30 (partial)** — `DIST`, `ID`, `AREA`, `LIST`. `AREA` is exact for circles and matches
  the drawn tessellation otherwise; an open object reports its length and says it has no
  area.
- **#28 (partial)** — `DIMCONTINUE` / `DIMBASELINE`. `artifacts/issue-28.pdf`.

---

## Two bugs found that nobody had reported

1. **The v0.2.0 AppImage could not plot headlessly.** It bundled only Qt's `xcb` platform
   plugin, so `musacad --plot` inside the packaged artifact died with exit 127 while the
   desktop launch was perfect. Found by running `--plot` from an *extracted* AppImage
   rather than assuming a working GUI implied a working headless path. Fixed in v0.3.0, and
   `docs/RELEASING.md` now makes that check part of the pre-flight.
2. **A silently wrong baseline offset.** The obvious "which side is the dimension line on"
   vector is parallel to the dimension direction *by construction*, so every stacked
   DIMBASELINE landed on the same line. A test caught it before the artifact did.

## Things worth challenging

- **The override supersession rule** (above) — the alternative was applying every tolerance
  mode on top, which makes `<> H7` in Limits mode unpredictable. I chose predictability.
- **Baseline spacing is derived** as 1.5 × text height (AutoCAD's ISO DIMDLI default at
  2.5 mm text) rather than stored as a style variable, to avoid a format bump. A real
  `DIMDLI` field is in `docs/TODO.md`.
- **Tables and GD&T are native-only.** DXF `ACAD_TABLE` and `TOLERANCE` are stated gaps in
  `docs/TODO.md`, not faked.
- **A flaky test, once.** `Dimension: a non-centre grip is grabbable` timed out once under
  concurrent build load, then passed 8/8 standalone and in three full runs. Its 5-second
  wait is a house-wide pattern across 12 files, so I left it rather than churning all of
  them; if CI ever sees it, raising that shared deadline is the fix.
- **An incremental-build hazard.** After `DimData` grew, a stale object file produced an
  ASan stack overflow that vanished on a clean rebuild. Worth a `rm -rf build/dev` if you
  ever see something impossible.

---

## Final state

| | |
|---|---|
| `dev` (ASan/UBSan) | clean, **zero warnings**, **455/455** |
| `release` | clean, **zero warnings** |
| `tsan` | **455/455** (under `setarch -R`) |
| Native format | **v20** (was v14 at the start of the previous session); every older version still loads, each with a test |
| Installed | `musacad` → v0.3.0 AppImage, with desktop entry |

Hot structs unchanged (`LineData` 40 B, `CircleData` 32 B, `EntityProps` 8 B); scene draw
calls still 4 (6 with aids). Artifacts for eyeballing are in `artifacts/`.

## What I'd do next

1. **#25 block authoring** (BLOCK/EXPLODE first) — the greyed-out *Edit Block* button is a
   visible promise the app doesn't keep, and title blocks need ATTDEF.
2. **#26 paper space** — the largest structural gap; the layout tab strip already exists
   with nothing behind it.
3. **#10** — finish the image viewport path (`GpuTexture`), which will raise the documented
   draw-call bound and so deserves its own measurement.
4. **#31 DXF `TOLERANCE`** — the most damaging interop gap, since GD&T is the part of a
   drawing that carries the inspection requirement.
