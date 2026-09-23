#!/usr/bin/env python3
"""Confere as texturas antes de elas serem ligadas (regra 40, 47).

Uma imagem entregue não entra no jogo porque parece certa. Este script mede o
que os prompts de `docs/assets/` prometem, e é ele que reprovou o `moon_normal`
do Milestone 7 -- uma imagem que parecia um normal map, registrava com o albedo,
e não codificava normais.

    python3 scripts/validate_textures.py            # todas as conhecidas
    python3 scripts/validate_textures.py a.png b.png  # avulsas

Sai 0 se tudo passa, 1 se alguma coisa reprova, 77 (Skipped, no CTest) se não há
texturas para conferir -- que é o estado de um clone antes de alguém gerar os
assets, e não é uma falha.

Sem dependências: a máquina de desenvolvimento não tem PIL nem numpy, e instalar
um dos dois para conferir sete imagens custaria mais do que o decodificador de
PNG que está aqui.
"""

from __future__ import annotations

import math
import pathlib
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEXTURES = ROOT / "assets/textures"

SKIP = 77


# --- decodificador --------------------------------------------------------

def decode(path: pathlib.Path):
    """PNG de 8 bits, não entrelaçado, para (largura, altura, canais, bytes)."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path.name}: não é um PNG")
    pos, idat, width, height, channels = 8, [], 0, 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace != 0:
                raise ValueError(f"{path.name}: {depth} bits, entrelaçado={interlace}; "
                                 "só 8 bits não entrelaçado")
            channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length

    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    out = bytearray(width * height * channels)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        filter_type = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if filter_type == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                estimate = a + b - c
                da, db, dc = abs(estimate - a), abs(estimate - b), abs(estimate - c)
                pick = a if (da <= db and da <= dc) else (b if db <= dc else c)
                line[i] = (line[i] + pick) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, out


# --- medições -------------------------------------------------------------

def seam_ratio(w, h, ch, px, horizontal=True):
    """Diferença média na emenda contra a de vizinhos do interior.

    Perto de 1 é um ladrilho bom; acima de ~6 há uma descontinuidade que se vê
    desenhada como uma linha do polo ao polo.
    """
    edge = inner = 0.0
    bands = min(ch, 3)
    if horizontal:
        for y in range(h):
            row = y * w * ch
            for c in range(bands):
                edge += abs(px[row + c] - px[row + (w - 1) * ch + c])
                inner += abs(px[row + (w // 3) * ch + c] - px[row + (w // 3 + 1) * ch + c])
        n = h * bands
    else:
        for x in range(w):
            for c in range(bands):
                edge += abs(px[x * ch + c] - px[(h - 1) * w * ch + x * ch + c])
                inner += abs(px[(h // 3) * w * ch + x * ch + c]
                             - px[(h // 3 + 1) * w * ch + x * ch + c])
        n = w * bands
    edge, inner = edge / n, inner / n
    return edge / max(inner, 1e-6), edge, inner


def row_spread(w, ch, px, y):
    bands = min(ch, 3)
    values = [sum(px[(y * w + x) * ch:(y * w + x) * ch + bands]) / bands for x in range(w)]
    return max(values) - min(values)


def channel_means(w, h, ch, px):
    bands = min(ch, 3)
    sums = [0] * bands
    for i in range(0, w * h * ch, ch):
        for c in range(bands):
            sums[c] += px[i + c]
    return [s / (w * h) for s in sums]


def patch_normal(w, ch, px, cx, cy, radius=12):
    """Média dos canais e comprimento mediano do vetor, numa mancha."""
    sums = [0.0, 0.0, 0.0]
    lengths = []
    for y in range(cy - radius, cy + radius):
        for x in range(cx - radius, cx + radius):
            i = (y * w + x) * ch
            for c in range(3):
                sums[c] += px[i + c]
            v = [px[i + c] / 127.5 - 1.0 for c in range(3)]
            lengths.append(math.sqrt(sum(c * c for c in v)))
    n = (2 * radius) ** 2
    lengths.sort()
    return [s / n for s in sums], lengths[len(lengths) // 2]


# --- verificações por asset ----------------------------------------------

# Assets entregues, conferidos e RECUSADOS. O estado está em
# `docs/assets/manifest.md` e esta tabela é a sua execução: uma reprovação num
# asset listado aqui é o resultado esperado e não derruba a suíte.
#
# Sem isto o script sairia 1 para sempre, e uma suíte que está vermelha por um
# estado conhecido é uma suíte que se aprende a ignorar -- o que a tornaria
# inútil no dia em que ficasse vermelha por um motivo novo.
#
# O contrário também é reportado: se um asset recusado passar em tudo, ele foi
# regerado e o manifesto é que está desatualizado.
REJECTED: dict[str, str] = {}


class Report:
    def __init__(self):
        self.failures = 0
        self.checks = 0
        self.expected_failures = 0
        self.tolerate = False

    def check(self, ok, what, detail=""):
        self.checks += 1
        if ok:
            mark = "ok  "
        elif self.tolerate:
            mark = "recusado"
            self.expected_failures += 1
        else:
            mark = "FAIL"
            self.failures += 1
        print(f"  {mark}  {what}{('  ' + detail) if detail else ''}")


def check_equirectangular(report, name, w, h, ch, px, pole_limit=60.0):
    report.check(abs(w / h - 2.0) < 1e-6, f"{name}: proporção 2:1", f"({w}x{h})")
    ratio, edge, inner = seam_ratio(w, h, ch, px, True)
    report.check(ratio < 6.0, f"{name}: a costura de longitude fecha",
                 f"(emenda {edge:.2f} contra {inner:.2f} no interior, razão {ratio:.2f})")
    top, bottom = row_spread(w, ch, px, 0), row_spread(w, ch, px, h - 1)
    middle = row_spread(w, ch, px, h // 2)
    report.check(top < pole_limit and bottom < pole_limit,
                 f"{name}: os polos são uniformes",
                 f"(topo {top:.0f}, base {bottom:.0f}, meio {middle:.0f})")


def check_tileable(report, name, w, h, ch, px):
    report.check(w == h, f"{name}: quadrada", f"({w}x{h})")
    for horizontal, label in ((True, "horizontal"), (False, "vertical")):
        ratio, edge, inner = seam_ratio(w, h, ch, px, horizontal)
        report.check(ratio < 6.0, f"{name}: ladrilha na {label}",
                     f"(razão {ratio:.2f})")


def check_normal_map(report, name, w, h, ch, px, flat_spots):
    """Um normal map tangente plano vale (128, 128, 255) e |n| = 1.

    Medido nas regiões LISAS, não na média global: a topografia rugosa enviesa a
    média e faz uma imagem errada parecer aceitável.
    """
    worst_offset = 0.0
    worst_length = 0.0
    for label, (x, y) in flat_spots.items():
        means, length = patch_normal(w, ch, px, x, y)
        offset = max(abs(means[0] - 128.0), abs(means[1] - 128.0))
        worst_offset = max(worst_offset, offset)
        worst_length = max(worst_length, abs(length - 1.0))
        print(f"        {label:<22} R {means[0]:5.1f}  G {means[1]:5.1f}  "
              f"B {means[2]:5.1f}   |n| {length:.3f}")
    report.check(worst_offset < 25.0, f"{name}: R e G centrados em 128 no terreno liso",
                 f"(desvio máximo {worst_offset:.1f})")
    report.check(worst_length < 0.12, f"{name}: os vetores são unitários",
                 f"(|n| desvia até {worst_length:.3f})")


## O varrimento de longitude, em GRAUS: de onde até onde, e de quanto em quanto.
##
## Eram 13 deslocamentos de 8 px num mapa de 2048, ou seja 1,4 grau cada, até
## 8,4 graus para cada lado. Os números continuam a ser esses; passaram a estar
## escritos na unidade em que o desalinhamento existe, que não depende de quantas
## colunas cada mapa tem.
REGISTRATION_STEP_DEG = 1.4
REGISTRATION_SPAN_DEG = 8.4


def check_registration(report, albedo, night):
    """As luzes noturnas caem em terra, e a longitude não está deslocada.

    O valor absoluto é modesto -- o brilho das cidades transborda para a água e
    o classificador de oceano é grosseiro -- então o que se verifica é que
    NENHUM deslocamento em longitude melhora o alinhamento. Se algum melhorasse,
    ELE seria o desalinhamento.

    ⚠️ Os dois mapas NÃO precisam de ter o mesmo tamanho. São a mesma projeção,
    então um pixel do mapa de luzes tem um lugar bem definido no albedo seja qual
    for a resolução de cada um, e exigir tamanhos iguais confundia a pergunta
    ("estão alinhados?") com uma coincidência de formato. A exigência reprovou o
    primeiro par vindo da NASA -- Blue Marble a 8192 contra VIIRS a 4096 -- sem
    que houvesse nada de errado com o alinhamento deles.
    """
    wa, ha, ca, alb = albedo
    wn, hn, cn, nit = night
    scale_x = wa / float(wn)
    scale_y = ha / float(hn)

    lit = []
    for y in range(2, hn - 2, 3):
        for x in range(0, wn, 3):
            i = (y * wn + x) * cn
            if (nit[i] + nit[i + 1] + nit[i + 2]) // 3 >= 55:
                lit.append((int((x + 0.5) * scale_x),
                            min(int((y + 0.5) * scale_y), ha - 1)))
    if not lit:
        report.check(False, "há luzes noturnas para conferir")
        return

    def land_fraction(shift_deg):
        dx = int(round(shift_deg * wa / 360.0))
        hits = 0
        for x, y in lit:
            j = (y * wa + (x + dx) % wa) * ca
            r, g, b = alb[j], alb[j + 1], alb[j + 2]
            if not (b > r + 18 and b > g + 8 and b < 150):
                hits += 1
        return hits / len(lit)

    steps = int(round(REGISTRATION_SPAN_DEG / REGISTRATION_STEP_DEG))
    shifts = [i * REGISTRATION_STEP_DEG for i in range(-steps, steps + 1)]
    scores = {shift: land_fraction(shift) for shift in shifts}
    best = max(scores, key=scores.get)
    report.check(abs(best) <= REGISTRATION_STEP_DEG + 1e-9,
                 "as luzes noturnas registram com o albedo em longitude",
                 f"(melhor deslocamento {best:+.1f}°, terra {scores[best]:.4f} "
                 f"contra {scores[0.0]:.4f} em zero e "
                 f"{scores[shifts[0]]:.4f} a {shifts[0]:+.1f}°)")


# --- as texturas conhecidas ----------------------------------------------

EQUIRECTANGULAR = {
    "earth/earth_albedo.png": 60.0,
    "earth/earth_clouds.png": 90.0,
    "earth/earth_night.png": 30.0,
    "moon/moon_albedo.png": 90.0,
}
TILEABLE = ["spacecraft/hull_panels.png", "cockpit/panel_surface.png"]

# Os mares são as superfícies mais lisas da Lua, e é neles que um normal map tem
# de ler (128, 128, 255). Coordenadas em pixels de um mapa 2048x1024.
LUNAR_MARIA = {
    "Mare Imbrium": (820, 250),
    "Oceanus Procellarum": (700, 360),
    "Mare Serenitatis": (960, 270),
}


def main(argv):
    report = Report()
    argv = list(argv[1:])

    # `--textures DIR` aponta a suíte inteira -- incluindo o registro cruzado,
    # que precisa de duas imagens ao mesmo tempo e por isso não cabe no modo
    # avulso -- para outro diretório.
    #
    # É o que `scripts/make_earth_textures.sh` usa: as texturas da Terra passaram
    # a ser JPEG (a fonte da NASA já é JPEG, e um PNG delas são cinco vezes os
    # bytes por informação que nunca existiu), este decodificador só lê PNG, e
    # então a conferência corre sobre os intermediários PNG da conversão -- os
    # mesmos pixels, antes do encoder, que não move um pixel de longitude.
    textures = TEXTURES
    if argv and argv[0] == "--textures":
        if len(argv) < 2:
            print("--textures precisa de um diretório", file=sys.stderr)
            return 2
        textures = pathlib.Path(argv[1])
        argv = argv[2:]

    paths = [pathlib.Path(a) for a in argv]

    if paths:
        for path in paths:
            w, h, ch, px = decode(path)
            print(f"\n{path.name}  {w}x{h}  {ch} canais  "
                  f"média {'/'.join('%.0f' % m for m in channel_means(w, h, ch, px))}")
            if abs(w / h - 2.0) < 1e-6:
                check_equirectangular(report, path.stem, w, h, ch, px)
            elif w == h:
                check_tileable(report, path.stem, w, h, ch, px)
        return 1 if report.failures else 0

    if not textures.exists() or not any(textures.rglob("*.png")):
        print(f"SKIP: não há texturas em {textures} "
              "(docs/assets/manifest.md diz como gerá-las)", file=sys.stderr)
        return SKIP

    decoded = {}
    for rel, pole_limit in EQUIRECTANGULAR.items():
        path = textures / rel
        if not path.exists():
            fetched = path.with_suffix(".jpg")
            if fetched.exists():
                print(f"\n{fetched.relative_to(textures)}: da NASA, conferida na conversão "
                      "(scripts/make_earth_textures.sh) -- este decodificador só lê PNG")
            else:
                print(f"\n{rel}: ausente -- o simulador usa o substituto procedural")
            continue
        w, h, ch, px = decode(path)
        decoded[rel] = (w, h, ch, px)
        print(f"\n{rel}  {w}x{h}  {ch} canais  "
              f"média {'/'.join('%.0f' % m for m in channel_means(w, h, ch, px))}")
        check_equirectangular(report, path.stem, w, h, ch, px, pole_limit)

    for rel in TILEABLE:
        path = textures / rel
        if not path.exists():
            print(f"\n{rel}: ausente -- o material fica liso")
            continue
        w, h, ch, px = decode(path)
        print(f"\n{rel}  {w}x{h}  {ch} canais")
        check_tileable(report, path.stem, w, h, ch, px)

    if "earth/earth_albedo.png" in decoded and "earth/earth_night.png" in decoded:
        print("\nregistro cruzado")
        check_registration(report, decoded["earth/earth_albedo.png"],
                           decoded["earth/earth_night.png"])

    rel = "moon/moon_normal.png"
    normal = textures / rel
    if normal.exists():
        w, h, ch, px = decode(normal)
        print(f"\n{rel}  {w}x{h}  {ch} canais")
        if rel in REJECTED:
            print(f"        RECUSADO: {REJECTED[rel]}")
        if (w, h) == (2048, 1024):
            before = report.expected_failures
            report.tolerate = rel in REJECTED
            check_normal_map(report, "moon_normal", w, h, ch, px, LUNAR_MARIA)
            if report.tolerate and report.expected_failures == before:
                report.tolerate = False
                report.check(False, f"{rel} continua a merecer a recusa",
                             "passou em tudo -- foi regerado? atualize o manifesto")
            report.tolerate = False
        else:
            print("        (as manchas de referência são para 2048x1024; não conferido)")

    tail = (f", {report.expected_failures} recusadas de propósito"
            if report.expected_failures else "")
    print(f"\n{report.checks} verificações, {report.failures} reprovadas{tail}")
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
