# Sourced, not executed: resolves the Godot editor binary for the other scripts.
#
#   source "${ROOT}/scripts/godot_bin.sh"
#   GODOT="$(godot_bin "${ROOT}")"
#
# Order: $GODOT_BIN, then what scripts/fetch_godot.sh unpacked into external/godot
# (Godot.app on macOS, Godot_v*_linux.<arch> on Linux), then godot/godot4 on PATH.
# Prints nothing when none is found; callers keep their own SKIP/exit handling.

godot_bin() {
    local root="$1" candidate
    if [[ -n "${GODOT_BIN:-}" ]]; then
        echo "${GODOT_BIN}"
        return
    fi
    for candidate in \
        "${root}/external/godot/Godot.app/Contents/MacOS/Godot" \
        "${root}"/external/godot/Godot_v*_linux.* \
        "${root}/external/godot/godot"; do
        if [[ -f "${candidate}" && -x "${candidate}" ]]; then
            echo "${candidate}"
            return
        fi
    done
    command -v godot || command -v godot4 || true
}
