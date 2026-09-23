#!/usr/bin/env bash
# As texturas da Terra que entram no Git, derivadas do que a NASA publicou.
#
#   scripts/fetch_earth_textures.sh    # 68 MB, fora do Git
#   scripts/make_earth_textures.sh
#
# ## Porquê JPEG, aqui e não no resto do projeto
#
# As outras texturas são PNG porque foram AUTORADAS: um gerador entregou pixels
# exatos e um PNG guarda exatamente esses. Estas foram MEDIDAS e a NASA já as
# distribui em JPEG -- recodificá-las em PNG são cinco vezes os bytes para
# preservar informação que nunca existiu. E o que chega à GPU é BPTC ou S3TC, que
# perde muito mais do que a passagem por JPEG a 94.
#
#   albedo    8192 x 4096   6,7 MB   contra 31 MB em PNG
#   nuvens    4096 x 2048   2,9 MB   contra 18 MB a 8192
#   noite     4096 x 2048   0,9 MB   contra  4 MB
#
# As nuvens ficam em 4096 e não em 8192 porque a diferença foi PROCURADA numa
# captura de uma órbita de 400 km e não se encontrou: as células que 8192
# resolve a mais são menores do que o borrão que o `smoothstep` da máscara e a
# compressão de VRAM já produzem. Custavam 6,7 MB para isso.
#
# ## A verificação
#
# `scripts/validate_textures.py` decodifica PNG e só PNG (sem dependências, que é
# a regra dele). Então a conferência acontece AQUI, sobre os intermediários PNG,
# com os mesmos pixels que a seguir viram JPEG -- as mesmas medidas de sempre:
# polos uniformes e as luzes noturnas a registrar com o albedo em longitude.
# Conferir depois do JPEG não acrescentaria nada: o encoder não move um pixel de
# longitude.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE="${ROOT}/external/nasa-earth"
STAGE="${SOURCE}/stage"
DEST="${ROOT}/assets/textures/earth"
SKIP=77

if ! command -v magick >/dev/null 2>&1; then
    # O `convert` do ImageMagick 6 que o Ubuntu empacota não serve: a policy.xml
    # da distribuição recusa um JPEG de 21600 px de largura.
    echo "SKIP: ImageMagick 7 (\`magick\`) não encontrado -- macOS: brew install imagemagick;" >&2
    echo "      Linux: o AppImage de https://imagemagick.org/script/download.php" >&2
    exit ${SKIP}
fi

ALBEDO="${SOURCE}/world.topo.bathy.200412.3x21600x10800.jpg"
CLOUDS="${SOURCE}/cloud_combined_8192.tif"
NIGHT="${SOURCE}/BlackMarble_2016_01deg_geo.tif"
for file in "${ALBEDO}" "${CLOUDS}" "${NIGHT}"; do
    if [[ ! -s "${file}" ]]; then
        echo "SKIP: falta $(basename "${file}") -- corra scripts/fetch_earth_textures.sh" >&2
        exit ${SKIP}
    fi
done

mkdir -p "${STAGE}/earth" "${DEST}"

# Colapsa as linhas polares na sua própria média horizontal.
#
# Num mapa equirretangular a linha de cima INTEIRA é o mesmo lugar: o polo. Os
# produtos da NASA não a forçam a ser uniforme -- no Blue Marble de dezembro o
# Ártico está em noite polar e a primeira linha mistura gelo claro com água
# escura, com uma dispersão de 117 níveis. Enrolada numa esfera, uma linha de
# cima não uniforme vira um cata-vento no polo, que é um artefato de projeção e
# não uma coisa que esteja lá.
#
# Quatro linhas de 4096 são 0,18 grau de latitude, uns vinte quilómetros: na
# esfera isso é menos do que um pixel de tela a partir de qualquer distância de
# onde o polo se veja. A emenda com a quinta linha não existe na imagem.
polar_average() {
    local file="$1" rows="$2"
    local width height
    width=$(magick identify -format "%w" "${file}")
    height=$(magick identify -format "%h" "${file}")
    local tmp="${file%.png}.polar.png"
    # `-resize 1xN!` é a média horizontal de cada linha; o `-resize WxN!` de
    # volta espalha-a pela linha toda.
    magick "${file}" \
        \( -clone 0 -crop "${width}x${rows}+0+0" +repage \
           -resize "1x${rows}!" -resize "${width}x${rows}!" \) \
        -geometry +0+0 -composite \
        \( -clone 0 -crop "${width}x${rows}+0+$((height - rows))" +repage \
           -resize "1x${rows}!" -resize "${width}x${rows}!" \) \
        -geometry "+0+$((height - rows))" -composite \
        "${tmp}"
    mv "${tmp}" "${file}"
}

# ⚠️ `-colorspace RGB` antes de redimensionar e `sRGB` depois: a média de dois
# pixels é uma média de LUZ, e fazê-la sobre valores com a curva sRGB aplicada
# escurece tudo o que tem contraste. Numa redução de 21600 para 8192 isso é a
# diferença entre um oceano e um oceano sujo.
echo "albedo   21600x10800 -> 8192x4096"
magick "${ALBEDO}" -colorspace RGB -filter Lanczos -resize 8192x4096! \
    -colorspace sRGB "${STAGE}/earth/earth_albedo.png"

# Um canal: a cobertura é uma fração, não uma cor -- o shader lê `.r` e o
# uniforme não leva `source_color` justamente por isso.
echo "nuvens    8192x4096  -> 4096x2048, cinza de 1 canal"
magick "${CLOUDS}" -colorspace Gray -depth 8 -filter Lanczos -resize 4096x2048! \
    "${STAGE}/earth/earth_clouds.png"

# As luzes sobem de 3600 para 4096, que é um aumento de 1,14. Não inventa
# detalhe nenhum; põe a textura numa potência de dois, que é o que a compressão
# de VRAM e os mipmaps querem.
echo "noite     3600x1800  -> 4096x2048"
magick "${NIGHT}" -colorspace RGB -filter Lanczos -resize 4096x2048! \
    -colorspace sRGB "${STAGE}/earth/earth_night.png"

echo "polos    a colapsar as quatro linhas de cada polo"
for stage_file in "${STAGE}/earth"/*.png; do
    polar_average "${stage_file}" 4
done

echo
echo "--- conferência sobre os intermediários ---"
python3 "${ROOT}/scripts/validate_textures.py" --textures "${STAGE}"
checked=$?
if (( checked != 0 && checked != SKIP )); then
    echo "FALHOU: a conferência reprovou; nada foi escrito em assets/" >&2
    exit 1
fi

echo
echo "--- a escrever em assets/textures/earth ---"
# `-sampling-factor 1x1`: sem subamostragem de croma. O padrão do JPEG joga fora
# três quartos da informação de cor, e numa textura esticada sobre um planeta
# isso aparece como franjas na linha da costa.
magick "${STAGE}/earth/earth_albedo.png" -quality 90 -sampling-factor 1x1 \
    "${DEST}/earth_albedo.jpg"
magick "${STAGE}/earth/earth_clouds.png" -quality 92 -sampling-factor 1x1 \
    "${DEST}/earth_clouds.jpg"
magick "${STAGE}/earth/earth_night.png" -quality 92 -sampling-factor 1x1 \
    "${DEST}/earth_night.jpg"
ls -la "${DEST}"/*.jpg | awk '{printf "  %-18s %5.1f MB\n", $NF, $5/1048576}'
