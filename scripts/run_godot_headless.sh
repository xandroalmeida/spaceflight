#!/usr/bin/env bash
# Runs the Godot project without a display and prints the HUD to stdout.
#
# This is how Milestone 2 is verified on a machine with no screen -- and how it
# was verified in the first place.  It exercises the whole chain: the extension
# loads, the kernels load, the propagator advances, the snapshot is built, the
# projection runs.
#
# Two Godot quirks it works around, both real and both undocumented in our code
# until they bit us:
#
#   1. A .gdextension file is only registered after an EDITOR scan, which writes
#      .godot/extension_list.cfg.  Without it the class simply does not exist and
#      the script fails to parse -- with no mention of the extension anywhere in
#      the output.  Running the game twice does not help; only a scan does.
#   2. `--headless --import` performs that scan and then CRASHES on exit in
#      Godot 4.5 (signal 11 inside the fog shader init, which headless has no
#      business initialising).  The file is written before the crash, so the exit
#      status is ignored on purpose.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${ROOT}/godot/project"
FRAMES="${1:-1200}"

source "${ROOT}/scripts/godot_bin.sh"
GODOT="$(godot_bin "${ROOT}")"
if [[ ! -x "${GODOT}" ]]; then
    GODOT="$(command -v godot || true)"
fi
if [[ -z "${GODOT}" || ! -x "${GODOT}" ]]; then
    echo "Godot not found. Run scripts/fetch_godot.sh, or set GODOT_BIN." >&2
    exit 1
fi

if [[ ! -f "${PROJECT}/bin/spaceflight.dylib" && ! -f "${PROJECT}/bin/spaceflight.so" \
   && ! -f "${PROJECT}/bin/spaceflight.dll" ]]; then
    echo "The GDExtension is not built. Run:" >&2
    echo "  cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON" >&2
    echo "  cmake --build build-godot --target spaceflight_gdextension -j" >&2
    exit 1
fi

# A third quirk, found while the Milestone 7 scene was being split into files:
# `class_name` is resolved from `.godot/global_script_class_cache.cfg`, which is
# also written only by a scan. A NEW script with a class_name is invisible until
# then -- the parse fails with "Could not find type X" and nothing mentions the
# cache. So the scan also runs whenever a script is newer than the cache, which
# is cheap (a second) and removes a trap that costs ten minutes each time.
NEEDS_SCAN=0
if [[ ! -f "${PROJECT}/.godot/extension_list.cfg" ]]; then
    NEEDS_SCAN=1
elif [[ ! -f "${PROJECT}/.godot/global_script_class_cache.cfg" ]]; then
    NEEDS_SCAN=1
else
    while IFS= read -r script; do
        if [[ "${script}" -nt "${PROJECT}/.godot/global_script_class_cache.cfg" ]]; then
            NEEDS_SCAN=1
            break
        fi
    done < <(find "${PROJECT}" -name '*.gd' -not -path '*/.godot/*')
fi

if (( NEEDS_SCAN )); then
    echo "Scanning the project so Godot registers the extension and the script classes..."
    "${GODOT}" --headless --path "${PROJECT}" --import > /dev/null 2>&1 || true
fi

if [[ ! -f "${PROJECT}/.godot/extension_list.cfg" ]]; then
    echo "The scan did not register spaceflight.gdextension. Open the project in the" >&2
    echo "editor once: ${GODOT} --path ${PROJECT} --editor" >&2
    exit 1
fi

exec "${GODOT}" --headless --path "${PROJECT}" --quit-after "${FRAMES}"
