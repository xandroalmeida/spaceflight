#!/usr/bin/env bash
# O manual do usuário, de docs/manual/ para docs/manual/manual.pdf.
#
# Regenera primeiro a tabela de teclas a partir do Input Map, para que o manual
# nunca seja compilado contra uma versão antiga dela. Se o Godot não estiver
# disponível, usa o JSON que já existe -- e diz que o fez.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! "${ROOT}/scripts/dump_controls.sh" >/dev/null 2>&1; then
    echo "aviso: não consegui regenerar as teclas (Godot ausente?);" >&2
    echo "       compilando com docs/gameplay/controls.json como está" >&2
fi

exec python3 "${ROOT}/scripts/build_manual.py" "$@"
