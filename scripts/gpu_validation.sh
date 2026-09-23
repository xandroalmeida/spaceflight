#!/usr/bin/env bash
# The graphics suite: the tests that read pixels back from the GPU.
#
# ---------------------------------------------------------------------------
# Why this is separate from ctest's headless labels (Milestone 6.2 section 22)
#
# Everything in `ctest --label-regex "unit|integration|scientific|regression|presentation"`
# runs with no GPU and is about arithmetic.  None of it can tell you whether
# anything is VISIBLE, and Milestone 6 is the proof: 8 786 stars loaded, zero of
# them on screen, and a completely green suite.  The defects that caused it were
# in the renderer, and no test that never rasterises a pixel can reach one.
#
#   cmake --build build --target ctest-headless    anywhere
#   cmake --build build --target gpu-validation    needs a GPU driver
#
# ---------------------------------------------------------------------------
# Skipping is not failing
#
# No GPU device (no Metal / Vulkan / Direct3D 12 driver) or a binary that was
# not built exits 77, which CTest reports as Skipped.  Exit 1 is reserved for a
# harness that ran and did not like what it saw.
#
# Both render OFF-SCREEN (ADR-0009): no window and no display are needed, only a
# driver.  Qualification: docs/validation/graphics-compatibility.md.
#
#   scripts/gpu_validation.sh starfield [--seed-references]
#   scripts/gpu_validation.sh relativistic_visual
#
# SPACEFLIGHT_BIN_DIR says where the binaries are (ctest sets it); the default is
# build/bin.
set -uo pipefail

CASE="${1:-starfield}"
shift || true
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SPACEFLIGHT_BIN_DIR:-${ROOT}/build/bin}"
SKIP=77

case "${CASE}" in
    starfield)
        HARNESS="${BIN}/spaceflight_starfield_validation"
        if [[ ! -x "${HARNESS}" ]]; then
            echo "SKIP: ${HARNESS} is not built (cmake --build build)" >&2
            exit ${SKIP}
        fi
        # The artefacts -- one image per measurement and the log -- are evidence
        # and live with the report (docs/validation/starfield-debug.md).
        exec "${HARNESS}" "${ROOT}/docs/validation/starfield" "$@"
        ;;
    relativistic_visual)
        # The scene photographs itself: one frame per rung of the visual-test
        # ladder, then quits (FlightApp::capture_step, --capture).
        #
        # The images are EVIDENCE, not the oracle -- the physics is checked
        # numerically against core/ in tests/scientific/test_relativistic_sky.cpp
        # and recorded in docs/validation/relativistic-rendering-visual.md.  What
        # a picture can settle, and nothing else can, is whether this scene's near
        # plane, camera placement and body meshes put that physics on a screen.
        APP="${BIN}/spaceflight"
        if [[ ! -x "${APP}" ]]; then
            echo "SKIP: ${APP} is not built (cmake --build build)" >&2
            exit ${SKIP}
        fi
        OUT="${ROOT}/docs/validation/scene"
        mkdir -p "${OUT}"
        before="$(date +%s)"
        "${APP}" --capture "${OUT}" --resolution 1024x640
        status=$?
        if (( status == 77 )); then
            exit ${SKIP}
        fi
        # Only frames written by THIS run count: stale images from an earlier one
        # would make a run that drew nothing look like a pass.
        fresh=0
        for frame in "${OUT}"/*.png; do
            [[ -f "${frame}" ]] || continue
            # `date -r FILE` is the modification time on both GNU and BSD.
            if (( $(date -r "${frame}" +%s) >= before )); then
                fresh=$((fresh + 1))
            fi
        done
        if (( status != 0 || fresh == 0 )); then
            echo "FAIL: the capture ladder produced ${fresh} frame(s) (exit ${status})" >&2
            exit 1
        fi
        echo "captured ${fresh} frame(s) to ${OUT}"
        exit 0
        ;;
    *)
        echo "unknown case \"${CASE}\" (starfield | relativistic_visual)" >&2
        exit 2
        ;;
esac
