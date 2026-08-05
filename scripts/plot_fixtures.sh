#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
#
# Plot every fixture drawing to artifacts/ through the SHIPPED binary's headless
# --plot path -- i.e. exactly what a user's build would produce, not a private
# harness. Useful as an eyes-on check after a change to the plot path, the stroke
# font, dimensions or any entity's geometry.
#
#   scripts/plot_fixtures.sh [build-dir] [fixture-dir] [out-dir]
#
# Defaults: build/dev  tests/fixtures  artifacts
#
# Needs no display: --plot forces the offscreen Qt platform.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build/dev}"
fixtures="${2:-$root/tests/fixtures}"
out="${3:-$root/artifacts}"
app="$build/bin/musacad_app"

if [[ ! -x "$app" ]]; then
    echo "plot_fixtures: no binary at $app -- build first (cmake --build --preset dev)" >&2
    exit 1
fi

mkdir -p "$out"
shopt -s nullglob
found=0
failed=0

for f in "$fixtures"/*.musa; do
    found=$((found + 1))
    name="$(basename "${f%.musa}")"
    pdf="$out/$name.pdf"
    # A landscape A4 fit-to-extents plot is the neutral default; a fixture that wants
    # something else carries a sidecar "<name>.plotargs" with the extra flags.
    args=(--paper A4 --landscape)
    if [[ -f "${f%.musa}.plotargs" ]]; then
        # shellcheck disable=SC2207
        args=($(cat "${f%.musa}.plotargs"))
    fi
    if "$app" --plot "$f" "$pdf" "${args[@]}" >/dev/null; then
        printf '  ok    %-28s -> %s\n' "$name.musa" "${pdf#"$root"/}"
    else
        printf '  FAIL  %-28s (exit %d)\n' "$name.musa" "$?" >&2
        failed=$((failed + 1))
    fi
done

if (( found == 0 )); then
    echo "plot_fixtures: no *.musa under $fixtures" >&2
    exit 1
fi
echo "plot_fixtures: $((found - failed))/$found plotted into ${out#"$root"/}"
exit $(( failed > 0 ? 1 : 0 ))
