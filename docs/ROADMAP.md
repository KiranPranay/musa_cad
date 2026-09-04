<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Musa CAD — implementation roadmap

A survey of what is **not yet implemented**, grouped by theme, with the AutoCAD behaviour
each item is measured against. `docs/COMMANDS.md` is the per-command status table and
`docs/TODO.md` holds the fine-grained deferrals with rationale; this file is the map above
both, and each group below corresponds to a GitHub issue.

Status legend: **P0** blocks ordinary drafting · **P1** expected by a professional user ·
**P2** completeness / parity.

---

## A. Dimension editing parity — [#20](https://github.com/MusaCAD/MusaCAD/issues/20), [#21](https://github.com/MusaCAD/MusaCAD/issues/21), [#28](https://github.com/MusaCAD/MusaCAD/issues/28)

The dimension model is strong (value computed from def points, never baked, one shared
`compute_dim_geometry`). What is missing is the *editing* surface AutoCAD gives.

| Item | AutoCAD behaviour | Priority |
|---|---|---|
| ~~**Text override**~~ | **Done** (#20) — `<>` expands to the measurement. | ✅ |
| ~~**Text reposition grip**~~ | **Done** (#21) — text grip on all five types, connector leader, "home text". | ✅ |
| **DIMBASELINE / DIMCONTINUE** | Chain and stack dimensions off the last one's extension line. | **P1** |
| **DIMJOGGED / DIMARC / DIMORDINATE** | Jogged radius for large radii; arc-length; ordinate (X/Y datum) dimensions. | **P1** |
| **Narrow-dimension fit for radial/angular** | The ISO 129-1 fallback covers linear/aligned only. | **P2** |
| **Short-leader variant for outside text** | ISO 129-1 permits the value on a short leader. | **P2** |

## B. Tables and structured annotation — [#22](https://github.com/MusaCAD/MusaCAD/issues/22), [#29](https://github.com/MusaCAD/MusaCAD/issues/29)

| Item | AutoCAD behaviour | Priority |
|---|---|---|
| ~~**TABLE entity + TABLESTYLE**~~ | **Done** (#22) — merged cells, per-cell alignment, style-driven text heights. Cell editing staged. | ✅ |
| **Text STYLE table** | Named text styles (font, height, width factor, oblique) that entities reference, like DIMSTYLE. Today a text entity carries a raw font index. | **P1** |
| **FIELD** | Auto-updating text (date, filename, sheet number, object property). | **P2** |

## C. Missing draw primitives — [#23](https://github.com/MusaCAD/MusaCAD/issues/23), [#33](https://github.com/MusaCAD/MusaCAD/issues/33)

The store already has arenas for splines and points; the *commands* are missing.

| Item | Priority |
|---|---|
| **SPLINE**, **ELLIPSE**, **POLYGON**, **POINT**, **XLINE / RAY** commands | **P1** |
| **DONUT**, **REVCLOUD**, **WIPEOUT** | **P2** |
| RECTANGLE first-corner options (Chamfer / Fillet / Width) | **P2** |
| HATCH gradient fills | **P2** |

## D. Missing modify commands — [#24](https://github.com/MusaCAD/MusaCAD/issues/24), [#27](https://github.com/MusaCAD/MusaCAD/issues/27)

| Item | Priority |
|---|---|
| ~~**STRETCH**~~ | **Done** (#24) | ✅ |
| **BREAK**, **LENGTHEN**, **ALIGN** | **P1** |
| **DIVIDE / MEASURE** (point or block at intervals) | **P2** |
| **PEDIT** + add/remove polyline vertex via grips | **P1** |
| TRIM / EXTEND / FILLET on **curve entities** (arc/circle/polyline as the *modified* entity) | **P1** |

## E. Block authoring — [#25](https://github.com/MusaCAD/MusaCAD/issues/25)

Blocks can be imported and placed; they cannot be *created* in-app.

| Item | Priority |
|---|---|
| **BLOCK / WBLOCK** — define a block from a selection | **P0** |
| **EXPLODE** — instance → geometry | **P0** |
| **REFEDIT** — edit a definition in place | **P1** |
| **ATTDEF / ATTRIB** — block attribute text (title blocks!) | **P1** |
| **XREF** — external references | **P2** |

## F. Raster images — [#10](https://github.com/MusaCAD/MusaCAD/issues/10) (remainder)

Model, decoder seam, persistence and plot are done. Remaining: **viewport display**
(`GpuTexture` + shaders + texture cache), **IMAGEATTACH / IMAGECLIP** commands, **DXF
`IMAGE`/`IMAGEDEF`**, and an embedded-payload size cap.

## G. Paper space, layouts and views — [#26](https://github.com/MusaCAD/MusaCAD/issues/26), [#33](https://github.com/MusaCAD/MusaCAD/issues/33)

| Item | Priority |
|---|---|
| **Layouts / paper space + viewports** — the single biggest structural gap for sheet production | **P0** |
| **Named views (VIEW)**, **REGEN**, **VPORTS** | **P2** |

## H. Inquiry and drawing housekeeping — [#30](https://github.com/MusaCAD/MusaCAD/issues/30)

| Item | Priority |
|---|---|
| **DIST / AREA / ID / LIST** | **P1** |
| **PURGE / AUDIT** | **P2** |
| **UNITS** (drawing units + precision) | **P1** |
| **GROUP** | **P2** |

## I. Interop gaps — [#31](https://github.com/MusaCAD/MusaCAD/issues/31)

(All stated honestly in the import result string today, never silently dropped.)

| Item | Priority |
|---|---|
| DXF **SPLINE** / legacy **POLYLINE** import | **P1** |
| DXF **TOLERANCE** (GD&T) export/import | **P1** |
| DXF **IMAGE / IMAGEDEF** | **P2** |
| True **SHX** shape-file parsing (today: faithful substitution) | **P2** |

## J. Properties, snapping and input — [#32](https://github.com/MusaCAD/MusaCAD/issues/32)

| Item | Priority |
|---|---|
| PR **numeric geometry editing** (line/circle/arc/polyline/leader are read-only) | **P1** |
| OSNAP settings dialog; **apparent intersection / insertion / parallel** snaps | **P2** |
| Input dialogs for Rotate/Scale + live ghost preview | **P2** |

---

## Ordering rationale

**P0 first, and within P0 the dimension-editing pair leads**, because a drafter hits
"I need this dimension to read `<> H7`" and "I need to move this text off the geometry"
on essentially every real sheet, and neither has a workaround that keeps the dimension
trustworthy. Tables come next (every fabrication sheet has a BOM or revision block), then
block authoring and paper space, which are larger structural pieces.
