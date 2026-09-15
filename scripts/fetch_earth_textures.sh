#!/usr/bin/env bash
# As imagens da Terra, medidas, do arquivo público da NASA.
#
# O Milestone 7 gerou estas três texturas a partir de prompts
# (`docs/assets/planets/`). Elas eram boas de perto e erradas de longe: o mapa de
# nuvens gerado tinha cobertura quase total, sem as regiões limpas que a
# circulação atmosférica produz, e de uma órbita de 400 km o que se via pela
# janela era uma pasta branca sem estrutura. Uma nuvem não é ruído bonito -- ela
# tem ciclones, frentes, a ZCIT e os anticiclones subtropicais, e nada disso sai
# de um gerador que nunca mediu o planeta.
#
#   world.topo.bathy   Blue Marble Next Generation, dezembro de 2004, topografia
#                      e batimetria, 21600 x 10800 (1,85 km/px), 30 MB. Dezembro
#                      porque a missão começa em 1 de janeiro: um mapa estático
#                      não segue as estações, então vale escolher a estação certa
#                      para a época de partida em vez de uma média que não é
#                      nenhum mês.
#   cloud_combined     composto MODIS de cobertura de nuvens, 8192 x 4096, 34 MB.
#   BlackMarble_2016   luzes noturnas do VIIRS, 3600 x 1800, 4,4 MB.
#
# Todas de domínio público (NASA Earth Observatory / Visible Earth, crédito a
# NASA Goddard Space Flight Center).
#
# Fora do Git, como os kernels, o catálogo de estrelas e o DEM lunar, e pela
# mesma razão: é dado obtido, versionado por quem o produz. O que entra no Git é
# o que `scripts/make_earth_textures.sh` deriva daqui.
#
#   scripts/fetch_earth_textures.sh
#   scripts/make_earth_textures.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/external/nasa-earth"
BASE="https://eoimages.gsfc.nasa.gov/images/imagerecords"

FILES=(
    "73000/73909/world.topo.bathy.200412.3x21600x10800.jpg"
    "57000/57747/cloud_combined_8192.tif"
    "144000/144898/BlackMarble_2016_01deg_geo.tif"
)

mkdir -p "${DEST}"
status=0
for path in "${FILES[@]}"; do
    name="$(basename "${path}")"
    target="${DEST}/${name}"
    if [[ -s "${target}" ]]; then
        echo "já existe: ${target}"
        continue
    fi
    echo "a baixar ${name} ..."
    # Para um ficheiro temporário e só depois renomeia: um download interrompido
    # que ficasse com o nome final passaria no teste de "já existe" acima e o
    # conversor trabalharia sobre metade de uma imagem.
    if curl -fL --retry 3 --retry-delay 5 --max-time 1800 \
            -o "${target}.part" "${BASE}/${path}"; then
        mv "${target}.part" "${target}"
    else
        echo "FALHOU: ${BASE}/${path}" >&2
        rm -f "${target}.part"
        status=1
    fi
done

if (( status == 0 )); then
    echo
    echo "pronto. agora: scripts/make_earth_textures.sh"
fi
exit ${status}
