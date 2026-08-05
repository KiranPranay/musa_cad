<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Musa CAD — command line

The shipped `musacad` binary has a documented command line, so a drawing can be
opened, validated or plotted from a script without a GUI session and a human.

```
musacad [<drawing>]              open a drawing in the GUI
musacad --check <drawing>        parse a drawing, report errors, exit
musacad --help
musacad --version
```

`<drawing>` is a `.musa` file, or a `.dxf` file (loaded through the DXF importer —
the extension chooses the loader).

## Exit codes

Scripts depend on these, so they are part of the interface:

| Code | Meaning |
|---|---|
| `0` | success |
| `1` | usage error (bad command line) |
| `2` | the drawing could not be read or parsed |
| `3` | the output could not be written |

## Opening a drawing

```sh
musacad part.musa
```

The file argument rides the **existing** `OpenDocumentCommand` — the same
geometry-thread path `File ▸ Open` uses, into a new tab. There is no second load
path, so a drawing that opens from the CLI behaves identically to one opened from
the dialog (including the fail-safe rule: on a parse error the store is left
untouched and the error is reported).

## Validating a drawing

```sh
musacad --check part.musa && echo "ok"
```

`--check` runs the same core loader (`io::load_native` / `io::load_dxf`) the engine
runs, prints what it read to stdout on success, and prints the parser's message to
**stderr** with exit code `2` on failure. It constructs **no Qt application object**,
so it works with no `DISPLAY`/`WAYLAND_DISPLAY` and no GPU:

```sh
$ musacad --check broken.musa
musacad: broken.musa: LINE record malformed (line 2).
$ echo $?
2
```

## Qt options

Musa CAD's own options are **double-dash** (`--check`, `--help`, `--version`); Qt's
are **single-dash** and are forwarded verbatim, including their values:

```sh
musacad -platform offscreen part.musa
```

An unknown double-dash option is a usage error. Qt's double-dash spellings are not
supported — use the single-dash form.

## How it is wired

`main()` parses the command line **before** constructing `QApplication`
(`src/app/main.cpp` → `musacad::app::parse_cli`), which is what lets `--help`,
`--version` and `--check` run headless. The parser lives in its own **Qt-free**
static library (`musacad_cli`, `src/app/cli.cpp`) so it is unit-tested in the normal
test binary; `tests/cli_check.cmake` additionally runs the **shipped executable** with
`DISPLAY`/`WAYLAND_DISPLAY` cleared and asserts each exit code end to end.
