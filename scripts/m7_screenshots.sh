#!/usr/bin/env bash
# A demonstração do Milestone 7, fotografada de ponta a ponta (regra 60).
#
# Voa o roteiro do §83 sozinho -- cockpit, vista externa, motor, RCS, alvo,
# plano, execução, chegada, órbita lunar -- e guarda uma imagem em cada ponto.
#
# Renderiza FORA DA TELA (ADR-0009): SDL_GPU desenha numa textura e cada
# fotografia é essa textura lida de volta, com passo fixo de 1/60 s. Não precisa
# de display, só de um driver de GPU (Metal, Vulkan ou Direct3D 12); sem ele sai
# 77, que o CTest reporta como Skipped. `SPACEFLIGHT_SHOT_STOP=N` para no passo N.
#
#   scripts/m7_screenshots.sh [saída] [resolução]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SPACEFLIGHT_BIN_DIR:-${ROOT}/build/bin}"
OUT="${1:-${ROOT}/docs/validation/m7}"
RESOLUTION="${2:-1920x1080}"
SKIP=77

if [[ ! -x "${BIN}/spaceflight" ]]; then
    echo "SKIP: ${BIN}/spaceflight is not built (cmake --build build)" >&2
    exit ${SKIP}
fi

mkdir -p "${OUT}"
args=(--shots m7 "${OUT}" --resolution "${RESOLUTION}")
if [[ -n "${SPACEFLIGHT_SHOT_STOP:-}" ]]; then
    args+=(--stop "${SPACEFLIGHT_SHOT_STOP}")
fi
"${BIN}/spaceflight" "${args[@]}"
status=$?
if (( status == SKIP )); then
    exit ${SKIP}
fi

shopt -s nullglob
frames=("${OUT}"/*.png)
if (( status != 0 || ${#frames[@]} == 0 )); then
    echo "FAIL: the run produced ${#frames[@]} frame(s) (exit ${status})" >&2
    exit 1
fi
echo "captured ${#frames[@]} frame(s) to ${OUT} at ${RESOLUTION}"
