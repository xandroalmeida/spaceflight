#!/usr/bin/env bash
# A demonstração do Milestone 8, fotografada de ponta a ponta (regra 109).
#
# Voa o roteiro inteiro sozinho -- órbita terrestre, Marte selecionado, a busca a
# correr, as alternativas, o mapa do sistema solar, a queima de partida, o
# cruzeiro, a aproximação, a captura e a órbita marciana -- e guarda uma imagem
# em cada ponto.
#
# DEMORA. A busca leva cerca de um minuto e o voo são duzentos e quatro dias sob
# warp: conte com vários minutos de relógio de parede. `SPACEFLIGHT_SHOT_STOP=N`
# para a sequência no passo N, o que faz uma volta de ajuste visual custar
# segundos em vez disso.
#
# PRECISA DE TELA. `--headless` tem um rasterizador que não desenha nada, e uma
# corrida "bem sucedida" sob ele não provaria coisa nenhuma -- que foi como o
# starfield ficou partido durante um milestone inteiro. Sem tela, sai 77, que o
# CTest reporta como Skipped: uma máquina que não pode correr isto não falhou,
# não correu.
#
#   scripts/m8_screenshots.sh [saída] [resolução]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${ROOT}/godot/project"
OUT="${1:-${ROOT}/docs/validation/m8}"
RESOLUTION="${2:-1920x1080}"
SKIP=77

GODOT="${GODOT_BIN:-${ROOT}/external/godot/Godot.app/Contents/MacOS/Godot}"
if [[ ! -x "${GODOT}" ]]; then
    GODOT="$(command -v godot || true)"
fi
if [[ -z "${GODOT}" || ! -x "${GODOT}" ]]; then
    echo "SKIP: Godot not found (run scripts/fetch_godot.sh, or set GODOT_BIN)" >&2
    exit ${SKIP}
fi
if ! ls "${PROJECT}"/bin/spaceflight.* >/dev/null 2>&1; then
    echo "SKIP: the GDExtension is not built." >&2
    exit ${SKIP}
fi
if [[ "$(uname)" != "Darwin" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "SKIP: no display; these images come out of a real framebuffer" >&2
    exit ${SKIP}
fi

if [[ ! -f "${PROJECT}/.godot/extension_list.cfg" ]]; then
    # O .gdextension só é registrado depois de um scan de editor, e esse scan
    # crasha ao sair no Godot 4.5 DEPOIS de escrever o arquivo. O status é
    # ignorado de propósito; ver scripts/run_godot_headless.sh.
    "${GODOT}" --path "${PROJECT}" --headless --import >/dev/null 2>&1
fi

mkdir -p "${OUT}"
SPACEFLIGHT_M8_SHOTS="${OUT}" "${GODOT}" --path "${PROJECT}" --resolution "${RESOLUTION}"
status=$?

shopt -s nullglob
frames=("${OUT}"/*.png)
if (( ${#frames[@]} == 0 )); then
    echo "FAIL: the run produced no frames (exit ${status})" >&2
    exit 1
fi
echo "captured ${#frames[@]} frame(s) to ${OUT} at ${RESOLUTION}"
