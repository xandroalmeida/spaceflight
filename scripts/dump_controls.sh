#!/usr/bin/env bash
# Regenera docs/gameplay/controls.md a partir do Input Map (regra 75).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GODOT="${GODOT_BIN:-${ROOT}/external/godot/Godot.app/Contents/MacOS/Godot}"
[[ -x "${GODOT}" ]] || GODOT="$(command -v godot || true)"
if [[ -z "${GODOT}" || ! -x "${GODOT}" ]]; then
    echo "Godot not found. Run scripts/fetch_godot.sh, or set GODOT_BIN." >&2
    exit 1
fi
exec "${GODOT}" --headless --path "${ROOT}/godot/project" \
    --script res://scripts/dump_controls.gd
