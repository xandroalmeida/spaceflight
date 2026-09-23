#!/usr/bin/env bash
# Regenera docs/gameplay/controls.md e controls.json a partir da tabela de teclas
# (app/presentation/input_actions.cpp, regra 75). Não precisa de GPU nem de tela.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SPACEFLIGHT_BIN_DIR:-${ROOT}/build/bin}"
if [[ ! -x "${BIN}/spaceflight" ]]; then
    echo "${BIN}/spaceflight is not built (cmake --build build)" >&2
    exit 1
fi
exec "${BIN}/spaceflight" --dump-controls "${ROOT}/docs/gameplay"
