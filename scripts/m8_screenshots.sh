#!/usr/bin/env bash
# A demonstração do Milestone 8, fotografada de ponta a ponta (regra 109).
#
# Voa o roteiro inteiro sozinho -- órbita terrestre, Marte selecionado, a busca a
# correr, as alternativas, o mapa do sistema solar, a queima de partida, o
# cruzeiro, a aproximação, a captura e a órbita marciana -- e guarda uma imagem
# em cada ponto.
#
# DEMORA. A busca leva cerca de um minuto e meio e o voo são duzentos e quatro
# dias sob warp: conte com uns dez minutos de relógio. `SPACEFLIGHT_SHOT_STOP=N`
# para a sequência no passo N, o que faz uma volta de ajuste visual custar
# segundos em vez disso.
#
# Renderiza FORA DA TELA (ADR-0009): não precisa de display, só de um driver de
# GPU; sem ele sai 77, que o CTest reporta como Skipped.
#
#   scripts/m8_screenshots.sh [saída] [resolução]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SPACEFLIGHT_BIN_DIR:-${ROOT}/build/bin}"
OUT="${1:-${ROOT}/docs/validation/m8}"
RESOLUTION="${2:-1920x1080}"
SKIP=77

if [[ ! -x "${BIN}/spaceflight" ]]; then
    echo "SKIP: ${BIN}/spaceflight is not built (cmake --build build)" >&2
    exit ${SKIP}
fi

mkdir -p "${OUT}"
args=(--shots m8 "${OUT}" --resolution "${RESOLUTION}")
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
