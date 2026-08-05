# Night log — unattended run, 2026-08-05/06

Append-only running log. Every entry: **timestamp — what I was doing / what I decided /
what was left.** Written so that a future compacted context can reconstruct state from
this file alone.

Task: close GitHub issues #7 #8 #9 #10 #11 #12 in `Musa-CAD/musa_cad`.
Work order (dependencies are real): **#11a → #11b → #9 → #7 → #12 → #8 → #10**.

Standing constraints from the operator:
* **No `Co-Authored-By: Claude` trailer on any commit** (org policy) — and no
  "Generated with Claude Code" footer in PR bodies either.
* Never disable a test, loosen an assertion, or add a warning suppression to pass a gate.
* Park rather than half-land. Green tree at every merge to `main`.

---

## 2026-08-05 23:5x — Orientation (§1)

**Doing:** reading the contract before touching anything.

**Findings that change the plan:**

1. **There is no `CLAUDE.md` and no `.claude/` in this repo.** The prompt says CLAUDE.md
   outranks it; it does not exist, so this prompt + `docs/ARCHITECTURE.md` are the whole
   contract. Recorded so nobody later thinks I skipped it.
2. **The remote is `git@github.com:Musa-CAD/musa_cad.git`** (the prompt said
   `MusaCAD/MusaCAD`). Same project; issues #7–#12 are there and match the prompt exactly.
3. **`gh auth status` is authenticated** as `KiranPranay`, scopes include `repo`. So the
   end-of-night push/PR path is open.
4. **The plot path is already shared and already device-agnostic.**
   `ui::paint_plot(QPaintDevice&, const RenderSnapshot&, const PlotSpec&, amin, amax)` in
   `include/musacad/ui/plot.hpp` is called by *both* `tools/plot_check.cpp` and the GUI
   PLOT dialog. #11b is therefore **not** an untangling job — it is "add a CLI front end
   that calls the same `paint_plot`". Big de-risk on the §5.1 time box.
5. **`ARCHITECTURE.md` has no Phase 30 (PLOT) section**, although Ph30 is referenced from
   the Ph31/Ph33 sections and `COMMANDS.md` lists PLOT as implemented. Pre-existing doc
   gap; I will add the plot-path section as part of #11b since I'm documenting the CLI
   that rides it.
6. **`musacad_tests` links core+render+command, NOT ui.** So CLI/plot tests must be
   `add_test` invocations of the built binary, not Catch2 cases. That matches what §5.1
   asked for anyway.
7. Native format is at **v14**. Version history is documented in
   `include/musacad/core/io/document.hpp` above `kFormatVersion`; growth is detected by
   token count / trailing records. Planned: v15 (#7), v16 (#8), v17 (#10).
8. `substitute_text_codes(raw, mtext=false)` — the `\U+XXXX` escape is **MTEXT-only**
   today. #9 asks for it in TEXT + dimension text too.
