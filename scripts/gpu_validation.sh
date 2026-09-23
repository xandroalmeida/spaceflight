#!/usr/bin/env bash
# The graphics suite: the tests that need a real framebuffer.
#
# ---------------------------------------------------------------------------
# Why this is separate from ctest (Milestone 6.2 section 22)
#
# Everything in `ctest --label-regex "unit|integration|scientific|regression"`
# runs with no display and is about arithmetic.  None of it can tell you whether
# anything is VISIBLE, and Milestone 6 is the proof: 8 786 stars loaded, zero of
# them on screen, and a completely green suite.  Two defects caused it --
# `render_mode unshaded` making Godot 4's Forward+ renderer discard EMISSION, and
# a sky sphere below one quantisation step of a 24-bit depth buffer -- and
# neither is reachable by a test that never rasterises a pixel.
#
# So these are labelled `gpu` and run by their own target:
#
#   cmake --build build --target ctest-headless    anywhere
#   cmake --build build --target gpu-validation    needs a screen
#
# and a CI job that runs the first must not report its result as coverage of the
# second.
#
# ---------------------------------------------------------------------------
# Skipping is not failing
#
# Missing Godot, a missing GDExtension or no display exits 77, which CTest
# reports as Skipped.  A machine that cannot run these has not failed them; it
# has not run them, and those are different facts.  Exit 1 is reserved for a
# harness that ran and did not like what it saw.
#
# Qualification status: Apple M5 Pro, Metal, Godot Forward+ only.  See
# docs/validation/graphics-compatibility.md.
#
#   scripts/gpu_validation.sh starfield
#   scripts/gpu_validation.sh relativistic_visual
set -uo pipefail

CASE="${1:-starfield}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${ROOT}/godot/project"

SKIP=77

source "${ROOT}/scripts/godot_bin.sh"
GODOT="$(godot_bin "${ROOT}")"
if [[ ! -x "${GODOT}" ]]; then
    GODOT="$(command -v godot || true)"
fi
if [[ -z "${GODOT}" || ! -x "${GODOT}" ]]; then
    echo "SKIP: Godot not found (run scripts/fetch_godot.sh, or set GODOT_BIN)" >&2
    exit ${SKIP}
fi

if ! ls "${PROJECT}"/bin/spaceflight.* >/dev/null 2>&1; then
    echo "SKIP: the GDExtension is not built." >&2
    echo "      cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON" >&2
    echo "      cmake --build build-godot --target spaceflight_gdextension -j" >&2
    exit ${SKIP}
fi

# No display, no pixels.  Godot's --headless has a dummy rasteriser that renders
# nothing at all, so a harness that "passed" under it would be proving nothing --
# which is exactly how the starfield stayed broken through a whole milestone.
if [[ "$(uname)" != "Darwin" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "SKIP: no display; these tests read pixels out of a real framebuffer" >&2
    exit ${SKIP}
fi

case "${CASE}" in
    starfield)
        exec "${ROOT}/scripts/starfield_validation.sh"
        ;;
    relativistic_visual)
        # The scene photographs itself: one frame per rung of the visual-test
        # ladder, then quits (scripts/flight.gd, SPACEFLIGHT_CAPTURE).
        #
        # The images are EVIDENCE, not the oracle -- the physics is checked
        # numerically against core/ in tests/scientific/test_relativistic_sky.cpp
        # and recorded in docs/validation/relativistic-rendering-visual.md.  What
        # a picture can settle, and nothing else can, is whether this scene's near
        # plane, camera placement and body meshes put that physics on a screen.
        OUT="${ROOT}/docs/validation/scene"
        mkdir -p "${OUT}"
        if [[ ! -f "${PROJECT}/.godot/extension_list.cfg" ]]; then
            # The .gdextension file is registered only after an editor scan, and
            # that scan crashes on exit in Godot 4.5 after writing the file.  The
            # status is ignored on purpose; see scripts/run_godot_headless.sh.
            "${GODOT}" --path "${PROJECT}" --headless --import >/dev/null 2>&1
        fi
        SPACEFLIGHT_CAPTURE="${OUT}" "${GODOT}" --path "${PROJECT}" --resolution 1024x640
        status=$?
        shopt -s nullglob
        frames=("${OUT}"/*.png)
        if (( ${#frames[@]} == 0 )); then
            echo "FAIL: the capture ladder produced no frames (exit ${status})" >&2
            exit 1
        fi
        echo "captured ${#frames[@]} frame(s) to ${OUT}"
        exit 0
        ;;
    *)
        echo "unknown case \"${CASE}\" (starfield | relativistic_visual)" >&2
        exit 2
        ;;
esac
