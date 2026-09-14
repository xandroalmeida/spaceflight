#!/usr/bin/env bash
# Topografia lunar medida: LOLA LDEM, do arquivo PDS da NASA.
#
# É a fonte de um normal map da Lua que valha alguma coisa. O que um gerador de
# imagens produz é uma FIGURA de um normal map -- o Milestone 7 recusou uma por
# medição (docs/assets/manifest.md) --, porque uma normal é uma quantidade
# calculada a partir de um campo de altura e não uma coisa que se pinte.
#
# LDEM_16: 5760 x 2880, 16 px/grau, 1895 m/pixel, 33 MB. Duas vírgula oito vezes
# a resolução do normal map de 2048 x 1024 que sai dele, que é sobreamostragem
# suficiente para a média de área não deixar serrilhado. O LDEM_64 tem quatro
# vezes mais e pesa 531 MB para uma diferença que ninguém vê num planeta que
# ocupa 400 px.
#
# Fora do Git, como os kernels e o catálogo de estrelas, e pela mesma razão: é
# dado obtido, versionado por quem o produz.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/external/lola"
BASE="https://pds-geosciences.wustl.edu/lro/lro-l-lola-3-rdr-v1/lrolol_1xxx/data/lola_gdr/cylindrical/img"
PRODUCT="${1:-ldem_16}"

mkdir -p "${DEST}"
for extension in lbl img; do
    target="${DEST}/${PRODUCT}.${extension}"
    if [[ -s "${target}" ]]; then
        echo "já existe: ${target}"
        continue
    fi
    echo "baixando ${PRODUCT}.${extension} ..."
    if ! curl -fSL --retry 3 --connect-timeout 30 -o "${target}" \
            "${BASE}/${PRODUCT}.${extension}"; then
        rm -f "${target}"
        echo "falhou: ${BASE}/${PRODUCT}.${extension}" >&2
        exit 1
    fi
done

ls -lh "${DEST}/${PRODUCT}".{lbl,img}
echo
echo "agora: python3 scripts/make_moon_normal.py"
