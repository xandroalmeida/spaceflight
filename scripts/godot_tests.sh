#!/usr/bin/env bash
# Os testes do Milestone 7, sem tela (regra 62).
#
# Cobrem o que o M7 acrescentou -- snapshot no cockpit, seleção de alvo,
# formatação de unidades, estado da câmera, mapeamento dos atuadores, pontos de
# trajetória -- e nada de física, que continua verificada por `ctest` em C++.
#
# 77 = Skipped: sem Godot, sem GDExtension ou sem kernels a suíte não correu, e
# isso não é o mesmo que ter reprovado.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${ROOT}/godot/project"
SKIP=77

source "${ROOT}/scripts/godot_bin.sh"
GODOT="$(godot_bin "${ROOT}")"
[[ -x "${GODOT}" ]] || GODOT="$(command -v godot || true)"
if [[ -z "${GODOT}" || ! -x "${GODOT}" ]]; then
    echo "SKIP: Godot not found (run scripts/fetch_godot.sh, or set GODOT_BIN)" >&2
    exit ${SKIP}
fi
if ! ls "${PROJECT}"/bin/spaceflight.* >/dev/null 2>&1; then
    echo "SKIP: the GDExtension is not built." >&2
    exit ${SKIP}
fi

# O mesmo scan de que run_godot_headless.sh depende, e pela mesma razão: sem ele
# nem a extensão nem os `class_name` existem.
NEEDS_SCAN=0
if [[ ! -f "${PROJECT}/.godot/global_script_class_cache.cfg" ]]; then
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
    "${GODOT}" --headless --path "${PROJECT}" --import >/dev/null 2>&1 || true
fi

# Qual suíte. Sem argumento, TODAS -- porque a pergunta "o M7 continua de pé?"
# é exatamente a que um milestone que generaliza o planejador tem de responder.
SUITES=("$@")
if (( ${#SUITES[@]} == 0 )); then
    SUITES=(m7 m8)
fi

status=0
for suite in "${SUITES[@]}"; do
    script="res://tests/test_${suite}.gd"
    echo "=== ${suite} ==="
    "${GODOT}" --headless --path "${PROJECT}" --script "${script}"
    rc=$?
    # 77 (Skipped) não sobrescreve uma falha real de outra suíte.
    if (( rc != 0 )); then
        if (( rc == SKIP && status == 0 )); then
            status=${SKIP}
        elif (( rc != SKIP )); then
            status=${rc}
        fi
    fi
done
exit ${status}
