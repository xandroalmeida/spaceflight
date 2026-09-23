#!/usr/bin/env python3
"""Normal map da Lua, DERIVADO da topografia medida (LOLA LDEM).

Uma normal é uma quantidade **calculada** a partir de um campo de altura, não
uma imagem que se pinte: o Milestone 7 recusou um normal map gerado como figura
porque os seus canais R e G seguiam o albedo em vez da inclinação
(`docs/assets/manifest.md`). Este script faz a conta.

    ./scripts/fetch_lunar_dem.sh          # 33 MB, fora do Git
    python3 scripts/make_moon_normal.py
    python3 scripts/validate_textures.py  # confere o resultado

Sem dependências, pelo mesmo motivo do resto do M7: a máquina de
desenvolvimento não tem numpy nem PIL, e um decodificador de PNG de quarenta
linhas custa menos do que a dependência.

## A conta

O DEM é simples cilíndrico: cada linha é uma latitude, cada coluna uma
longitude. A distância no CHÃO que uma coluna representa depende da latitude --
`R·cos(φ)·Δλ` -- e a que uma linha representa não: `R·Δφ`. Ignorar esse `cos(φ)`
é o erro clássico da conversão: as encostas leste-oeste saem exageradas por
`1/cos(φ)`, o que a 60 graus é o dobro e a 80 graus é seis vezes.

Perto dos polos o `cos(φ)` tende a zero e a diferença finita passa a ser medida
sobre uma distância minúscula, o que amplifica o ruído do próprio DEM. A
correção não é fixar um limite no `cos`: é ALARGAR o estêncil em longitude na
mesma proporção, `passo ≈ 1/cos(φ)`, de modo que o denominador
`passo·cos(φ)·R·Δλ` fique aproximadamente constante em todas as latitudes. É a
mesma quantidade de chão em todo o lado, que é o que uma derivada quer.

## A convenção

Espaço tangente, convenção OpenGL (a que app/shaders/body.frag lê, sem inverter
o y):

    +x  cresce com `u` -- para leste
    +y  aponta para "cima" no espaço UV, que é `-v` -- para norte
    +z  sai da superfície

Superfície plana vale `(128, 128, 255)` e todo vetor tem comprimento 1, que é
exatamente o que `scripts/validate_textures.py` mede nos mares.

A componente leste-oeste é demonstrável: uma encosta que sobe para leste tem a
normal a inclinar-se para OESTE, e `-∂h/∂e` dá exatamente isso. A norte-sul segue
a mesma derivação, com o topo da imagem a ser o norte. Se algum dia ela aparecer
invertida -- crateras a ler como cúpulas quando a luz vem do norte --, o remédio
é uma linha: `process/normal_map_invert_y=true` no `.import`.
"""

from __future__ import annotations

import array
import math
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "external/lola"
DESTINATION = ROOT / "assets/textures/moon/moon_normal.png"

OUT_WIDTH = 2048
OUT_HEIGHT = 1024

# Exageração vertical. 1.0 é a Lua como ela é.
#
# Não é um número escolhido por gosto: a distribuição de declives medida no DEM
# está impressa quando o script corre, e a 2048 x 1024 um pixel do equador cobre
# 5,3 km, sobre os quais a mediana do relevo lunar dá uma inclinação de poucos
# graus. É pouco, e é o que existe. Exagerar é uma decisão de RENDERIZAÇÃO e
# fica aqui, com um nome, em vez de embutida na conta.
EXAGGERATION = 1.0


def read_label(path: pathlib.Path) -> dict:
    text = path.read_text(errors="replace")
    def value(key, cast=str):
        m = re.search(rf"^\s*{key}\s*=\s*([^\s<]+)", text, re.M)
        if not m:
            raise ValueError(f"{path.name}: falta {key}")
        return cast(m.group(1).strip('"'))
    return {
        "lines": value("LINES", int),
        "samples": value("LINE_SAMPLES", int),
        "bits": value("SAMPLE_BITS", int),
        "type": value("SAMPLE_TYPE"),
        "scale": value("SCALING_FACTOR", float),
        "radius_km": value("A_AXIS_RADIUS", float),
        "center_longitude": value("CENTER_LONGITUDE", float),
    }


def read_dem(img: pathlib.Path, label: dict) -> array.array:
    if label["bits"] != 16 or "INTEGER" not in label["type"]:
        raise ValueError(f"esperado 16 bits inteiros, veio {label['bits']} {label['type']}")
    raw = img.read_bytes()
    expected = label["lines"] * label["samples"] * 2
    if len(raw) != expected:
        raise ValueError(f"{img.name}: {len(raw)} bytes, esperado {expected}")
    data = array.array("h")
    data.frombytes(raw)
    # LSB_INTEGER é little-endian; `array` usa a ordem da máquina.
    if sys.byteorder != "little":
        data.byteswap()
    return data


def downsample(data, src_w, src_h, out_w, out_h, scale):
    """Média de ÁREA, em duas passagens. Em metros, já com o fator de escala.

    Média e não amostragem: 5760 para 2048 descarta dois de cada três pixels se
    for por amostragem, e o que se perde são justamente as cristas finas das
    crateras -- que é o relevo que se quer ver.
    """
    rows = [None] * out_h
    for oy in range(out_h):
        y0 = oy * src_h // out_h
        y1 = max(y0 + 1, (oy + 1) * src_h // out_h)
        acc = [0.0] * src_w
        for y in range(y0, y1):
            base = y * src_w
            for x in range(src_w):
                acc[x] += data[base + x]
        count = y1 - y0
        rows[oy] = [v / count for v in acc]

    out = [None] * out_h
    for oy in range(out_h):
        source = rows[oy]
        line = [0.0] * out_w
        for ox in range(out_w):
            x0 = ox * src_w // out_w
            x1 = max(x0 + 1, (ox + 1) * src_w // out_w)
            total = 0.0
            for x in range(x0, x1):
                total += source[x]
            line[ox] = total / (x1 - x0) * scale
        out[oy] = line
    return out


def roll_longitude(height, out_w, shift):
    """Leva a longitude 180 E para a coluna 0.

    O DEM tem a longitude 0 na coluna 0; as texturas deste projeto têm -180 na
    borda esquerda, o que foi CONFERIDO e não assumido: o ponto subsolar de
    2026-01-01 00:00 UTC, que está a 180,9 E, cai em `u = 0,003`
    (`tests/presentation/test_presentation_flight.cpp`).
    """
    for oy in range(len(height)):
        row = height[oy]
        height[oy] = row[shift:] + row[:shift]
    return height


def build_normals(height, out_w, out_h, radius_m, exaggeration):
    d_lon = 2.0 * math.pi / out_w
    d_lat = math.pi / out_h
    ground_y = radius_m * d_lat

    slopes = []
    pixels = bytearray(out_w * out_h * 3)
    for y in range(out_h):
        latitude = (0.5 - (y + 0.5) / out_h) * math.pi
        cos_lat = max(math.cos(latitude), 1.0e-6)
        # O estêncil em longitude alarga com 1/cos(φ), de modo que ele cubra
        # sempre aproximadamente a mesma distância de chão.
        step = max(1, min(out_w // 4, int(round(1.0 / cos_lat))))
        ground_x = radius_m * cos_lat * d_lon * step

        north = height[max(y - 1, 0)]
        south = height[min(y + 1, out_h - 1)]
        row = height[y]
        span_y = ground_y * (min(y + 1, out_h - 1) - max(y - 1, 0))

        for x in range(out_w):
            east = row[(x + step) % out_w]
            west = row[(x - step) % out_w]
            dh_de = (east - west) / (2.0 * ground_x)
            # Norte é a linha de CIMA, e `y` cresce para sul.
            dh_dn = (north[x] - south[x]) / span_y

            nx = -dh_de * exaggeration
            ny = -dh_dn * exaggeration
            length = math.sqrt(nx * nx + ny * ny + 1.0)
            slopes.append(math.hypot(dh_de, dh_dn))

            i = (y * out_w + x) * 3
            pixels[i] = min(255, max(0, int(round((nx / length) * 127.5 + 127.5))))
            pixels[i + 1] = min(255, max(0, int(round((ny / length) * 127.5 + 127.5))))
            pixels[i + 2] = min(255, max(0, int(round((1.0 / length) * 127.5 + 127.5))))
    return pixels, slopes


def write_png(path: pathlib.Path, width: int, height: int, rgb: bytearray) -> None:
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)                      # filtro None: simples e suficiente
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(kind: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b""))


def main(argv) -> int:
    product = argv[1] if len(argv) > 1 else "ldem_16"
    lbl = SOURCE / f"{product}.lbl"
    img = SOURCE / f"{product}.img"
    if not lbl.exists() or not img.exists():
        print(f"SKIP: falta {img.name} -- rode ./scripts/fetch_lunar_dem.sh", file=sys.stderr)
        return 77

    label = read_label(lbl)
    radius_m = label["radius_km"] * 1000.0
    print(f"{product}: {label['samples']} x {label['lines']}, "
          f"{label['bits']} bits {label['type']}, escala {label['scale']} m/DN, "
          f"raio {label['radius_km']} km")

    data = read_dem(img, label)
    print(f"reamostrando para {OUT_WIDTH} x {OUT_HEIGHT} por média de área ...")
    height = downsample(data, label["samples"], label["lines"],
                        OUT_WIDTH, OUT_HEIGHT, label["scale"])

    lows = min(min(r) for r in height)
    highs = max(max(r) for r in height)
    print(f"relevo {lows:+.0f} .. {highs:+.0f} m em torno de {label['radius_km']} km")

    # O DEM tem longitude 0 na coluna 0; a textura quer -180 na borda esquerda.
    height = roll_longitude(height, OUT_WIDTH, OUT_WIDTH // 2)

    print("derivando as normais ...")
    pixels, slopes = build_normals(height, OUT_WIDTH, OUT_HEIGHT, radius_m, EXAGGERATION)

    slopes.sort()
    def percentile(q):
        return slopes[min(len(slopes) - 1, int(q * len(slopes)))]
    print("declive |∇h|  p50 %.4f (%.1f deg)  p90 %.4f (%.1f deg)  p99 %.4f (%.1f deg)"
          % (percentile(0.50), math.degrees(math.atan(percentile(0.50))),
             percentile(0.90), math.degrees(math.atan(percentile(0.90))),
             percentile(0.99), math.degrees(math.atan(percentile(0.99)))))
    if EXAGGERATION != 1.0:
        print(f"(com exageração vertical de {EXAGGERATION}x)")

    write_png(DESTINATION, OUT_WIDTH, OUT_HEIGHT, pixels)
    print(f"\nescrito {DESTINATION.relative_to(ROOT)}  "
          f"{DESTINATION.stat().st_size / 1024 / 1024:.1f} MB")
    print("confira com: python3 scripts/validate_textures.py")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
