#!/usr/bin/env bash
# Runs the starfield validation harness (Milestone 6.1, Part B) and collects its
# artefacts.
#
# Unlike scripts/run_godot_headless.sh this one NEEDS a display: the whole point
# is to read pixels back out of a real framebuffer, and Godot's headless mode has
# a dummy rasteriser that renders nothing at all.  A harness that passed under
# --headless would be proving nothing, which is how the starfield stayed broken
# through an entire milestone.
#
# Artefacts land in the Godot user data directory and are copied to
# docs/validation/starfield/.  The PNGs are the evidence; the log is the reading.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${ROOT}/godot/project"
OUT="${ROOT}/docs/validation/starfield"

GODOT="${GODOT_BIN:-${ROOT}/external/godot/Godot.app/Contents/MacOS/Godot}"
if [[ ! -x "${GODOT}" ]]; then
    GODOT="$(command -v godot || true)"
fi
# 77, not 1: CTest reads it as Skipped.  A machine with no Godot has not FAILED
# the graphics validation, it has not run it, and a suite that cannot tell those
# apart is how an untested renderer reads as a tested one (Milestone 6.2 s. 22).
if [[ -z "${GODOT}" || ! -x "${GODOT}" ]]; then
    echo "SKIP: Godot not found. Run scripts/fetch_godot.sh, or set GODOT_BIN." >&2
    exit 77
fi

if ! ls "${PROJECT}"/bin/spaceflight.* >/dev/null 2>&1; then
    echo "SKIP: the GDExtension is not built. Run:" >&2
    echo "  cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON" >&2
    echo "  cmake --build build-godot --target spaceflight_gdextension -j" >&2
    exit 77
fi

# The .gdextension file is only registered after an editor scan writes
# .godot/extension_list.cfg; without it SpaceflightSky does not exist and the
# script fails to parse with no mention of the extension anywhere.  The scan
# crashes on exit in 4.5 (see run_godot_headless.sh), and the file is written
# before the crash, so the status is ignored on purpose.
if [[ ! -f "${PROJECT}/.godot/extension_list.cfg" ]]; then
    "${GODOT}" --path "${PROJECT}" --headless --import >/dev/null 2>&1
fi

"${GODOT}" --path "${PROJECT}" --resolution 1024x640 res://starfield_debug.tscn
status=$?

USERDATA="${HOME}/Library/Application Support/Godot/app_userdata/Spaceflight/starfield"
if [[ ! -d "${USERDATA}" ]]; then
    USERDATA="${HOME}/.local/share/godot/app_userdata/Spaceflight/starfield"
fi
if [[ -d "${USERDATA}" ]]; then
    mkdir -p "${OUT}"
    cp -f "${USERDATA}"/*.png "${USERDATA}"/*.log "${OUT}/" 2>/dev/null || true
    echo "artefacts copied to ${OUT}"

    # Seeding the references is a deliberate act, never a side effect of a run:
    # a suite that quietly adopts whatever it just rendered as the thing to
    # compare against cannot fail, and the four snapshots of section 26 are the
    # only part of this harness whose oracle is a previous image rather than
    # core/.  Seed them when the physics has been checked by the other nine
    # stages, and say so in the commit.
    if [[ "${1:-}" == "--seed-references" ]]; then
        mkdir -p "${ROOT}/docs/validation/starfield-reference"
        cp -f "${USERDATA}"/10_sky_beta_*.png "${ROOT}/docs/validation/starfield-reference/"
        echo "reference snapshots seeded from this run"
    fi
fi

exit ${status}
