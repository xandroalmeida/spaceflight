#!/usr/bin/env python3
"""Importa os sons da nave a partir de gravações CC0 (regra 36).

    python3 scripts/import_ship_audio.py

Baixa as fontes (com checksum) para external/downloads/, decodifica,
filtra, transforma em laço onde é laço e escreve WAV mono de 22 050 Hz em
assets/audio/. Os WAV gerados ficam no repositório; este script só é preciso
para refazê-los.

## Fonte e licença

* Kenney, "Sci-fi Sounds" 1.0 -- https://kenney.nl/assets/sci-fi-sounds
* Kenney, "Interface Sounds" 1.0 -- https://kenney.nl/assets/interface-sounds

* craigsmith, "S40-13 Alien hum or space ship interior" --
  https://freesound.org/people/craigsmith/sounds/675339/
* jgxxx, "Fan looping" -- https://freesound.org/people/jgxxx/sounds/704393/
* soneproject, "Air compressor valve" --
  https://freesound.org/people/soneproject/sounds/185515/
* DJT4NN3R, "thrusters_loop" -- https://freesound.org/people/DJT4NN3R/sounds/347576/

Todos em Creative Commons Zero (CC0 1.0): domínio público, sem atribuição
obrigatória. Creditamos assim mesmo, em docs/assets/audio/README.md. Do
Freesound usamos a prévia HQ (MP3), que se baixa sem conta; para um zumbido
passado por um passa-baixo, a perda do MP3 não se ouve.

## Dependência

Vorbis e MP3 não se decodificam com a biblioteca padrão. O script usa `ffmpeg` se
estiver no PATH e, se não, o módulo `soundfile` (pip install soundfile). É a
única coisa fora da stdlib, e só na hora de reimportar: o build e o jogo leem
os WAV, que já estão prontos.

## O que se faz com cada som

O mesmo que a regra 36 pede dos placeholders: nada soa através do vácuo, o que
se ouve é o CASCO. Por isso os motores e o RCS passam por um passa-baixo -- o
metal não conduz o brilho de um jato ao ar livre -- e nenhum deles toca na
câmera externa (AudioDirector::set_interior).
"""

from __future__ import annotations

import hashlib
import io
import math
import pathlib
import shutil
import struct
import subprocess
import sys
import urllib.request
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import generate_audio_placeholders as gen  # noqa: E402  -- os filtros e o laço

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOWNLOADS = ROOT / "external/downloads"

PACKS = {
    "scifi": (
        "https://kenney.nl/media/pages/assets/sci-fi-sounds/6b296f9ecf-1677589334/"
        "kenney_sci-fi-sounds.zip",
        "119340f351a5098ad814f78719438c0da355a9ce8a4c8a3af6a8d48aa3d49e04",
    ),
    "interface": (
        "https://kenney.nl/media/pages/assets/interface-sounds/fa43c1dd4d-1677589452/"
        "kenney_interface-sounds.zip",
        "f2193d072726d6758a5f7871b2dcc54dcce0d5c35c6f0a62f92549b327c81232",
    ),
}

RECORDINGS = {
    "ship_hum": (
        "https://cdn.freesound.org/previews/675/675339_2524442-hq.mp3",
        "7c99c42381ed3baf6bdbbdd9879a124eef5c94b8d45c8ffbf3925f81bd046c17",
    ),
    "fan": (
        "https://cdn.freesound.org/previews/704/704393_14923038-hq.mp3",
        "13f393a20cec30b9243f5af59d959b914f4c4ff2fd349a418a310de39c5b1084",
    ),
    "rcs_valve": (
        "https://cdn.freesound.org/previews/185/185515_2127539-hq.mp3",
        "5c7165d18885f9e832ff8867cd862b0b50872cef9d1b9185ef53245f6a9d84db",
    ),
    "rcs_loop": (
        "https://cdn.freesound.org/previews/347/347576_3217484-hq.mp3",
        "007d3a8e68a6d49d6045a28d654e53778273f7c8cdf348b5188240ce449f5a8c",
    ),
}


def download(url: str, digest: str) -> bytes:
    path = DOWNLOADS / url.rsplit("/", 1)[1]
    if not path.exists():
        DOWNLOADS.mkdir(parents=True, exist_ok=True)
        print(f"baixando {url}")
        request = urllib.request.Request(url, headers={"User-Agent": "spaceflight-import"})
        with urllib.request.urlopen(request, timeout=120) as response:
            path.write_bytes(response.read())
    data = path.read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != digest:
        sys.exit(f"{path}: sha256 {actual}, esperado {digest}")
    return data


def fetch(pack: str) -> zipfile.ZipFile:
    return zipfile.ZipFile(io.BytesIO(download(*PACKS[pack])))


def member(archive: zipfile.ZipFile, name: str) -> bytes:
    for info in archive.infolist():
        if info.filename.endswith("/" + name):
            return archive.read(info)
    sys.exit(f"{name} não está no pacote")


def decode(encoded: bytes) -> list[float]:
    """OGG ou MP3 -> mono a gen.RATE, em [-1, 1]."""
    if shutil.which("ffmpeg"):
        pcm = subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-i", "pipe:0", "-ac", "1",
             "-ar", str(gen.RATE), "-f", "s16le", "pipe:1"],
            input=encoded, capture_output=True, check=True).stdout
        return [s / 32768.0 for s in struct.unpack(f"<{len(pcm) // 2}h", pcm)]
    try:
        import soundfile
    except ImportError:
        sys.exit("preciso de ffmpeg no PATH ou de `pip install soundfile` para ler OGG e MP3")
    data, rate = soundfile.read(io.BytesIO(encoded), always_2d=True)
    mono = [float(sum(frame)) / len(frame) for frame in data]
    return resample(mono, rate)


def resample(signal: list[float], rate: int) -> list[float]:
    """Qualquer taxa -> gen.RATE: dois polos a 9 kHz contra o rebatimento e
    interpolação linear. Pobre como o passa-baixo do gerador, e pelo mesmo
    motivo suficiente: o que sobra acima de 8 kHz é o que o casco já corta."""
    if rate == gen.RATE:
        return signal
    alpha = 1.0 - math.exp(-2.0 * math.pi * 9000.0 / rate)
    for _ in range(2):
        state, filtered = signal[0], []
        for sample in signal:
            state += alpha * (sample - state)
            filtered.append(state)
        signal = filtered
    step = rate / gen.RATE
    out = []
    position = 0.0
    while position < len(signal) - 1:
        index = int(position)
        frac = position - index
        out.append(signal[index] * (1.0 - frac) + signal[index + 1] * frac)
        position += step
    return out


def through_hull(signal: list[float]) -> list[float]:
    """O RCS ouvido de dentro: dois polos a 3 kHz tiram o brilho do gás ao ar
    livre, e um passa-alta a 150 Hz tira o ronco do microfone."""
    return gen.high_pass(gen.low_pass(gen.low_pass(signal, 3000.0), 3000.0), 150.0)


def fade_out(signal: list[float], seconds: float) -> list[float]:
    n = min(len(signal), int(gen.RATE * seconds))
    return signal[:-n] + [s * (1.0 - i / n) for i, s in enumerate(signal[-n:])]


def main() -> None:
    scifi = fetch("scifi")
    interface = fetch("interface")

    def load(archive: zipfile.ZipFile, name: str) -> list[float]:
        return decode(member(archive, name))

    # IMPULSO: 200 kN. O mais grave e o mais pesado do pacote (centróide
    # espectral ~275 Hz), aparado acima de 900 Hz -- um estrondo no chão da
    # cabine, não um rugido.
    gen.write("engine_impulse.wav",
              gen.make_loopable(gen.low_pass(load(scifi, "spaceEngineLow_003.ogg"), 900.0)))

    # CRUZEIRO: 11 kN a 0,5 c. Outro caráter e não o mesmo som mais baixo: um
    # zumbido mais agudo e mais liso (centróide ~750 Hz), que diz "outro modo"
    # mesmo com o volume igual.
    cruise = gen.high_pass(load(scifi, "spaceEngineSmall_000.ogg"), 120.0)
    gen.write("engine_cruise.wav", gen.make_loopable(gen.low_pass(cruise, 2400.0)))

    # RCS, em duas partes, as duas escolhidas de ouvido contra outras três
    # candidatas cada. O golpe: uma válvula de compressor, estalo seco de meio
    # segundo -- o "bang" que os astronautas descrevem ao ouvir o RCS por
    # dentro. O fim é esmaecido para que um corte no meio da cauda não estale.
    valve = through_hull(decode(download(*RECORDINGS["rcs_valve"])))
    gen.write("rcs_thump.wav", fade_out(valve, 0.05))
    # O sopro, enquanto os bicos estão abertos: um laço grave de propulsores
    # (centróide ~270 Hz). O jato de gás agudo do pacote Kenney soava a chiado.
    gen.write("rcs_hiss.wav",
              gen.make_loopable(through_hull(decode(download(*RECORDINGS["rcs_loop"])))))

    # O fundo são duas gravações TONAIS e não ruído: um ambiente de nave (a
    # ISS, por exemplo) é ventoinhas e bombas em frequências fixas, batendo
    # devagar umas contra as outras. Ruído largo filtrado soava a chiado, e o
    # computerNoise do pacote Kenney -- uma nota nova a cada 100-150 ms --
    # soava a bipes contínuos.
    #
    # Equipamento: um zumbido de interior de nave, com linhas em 130-140 Hz.
    hum = gen.low_pass(decode(download(*RECORDINGS["ship_hum"])), 2500.0)
    gen.write("equipment.wav", gen.make_loopable(hum))
    # Ventilação: uma ventoinha de verdade, passagem de pás em 35 Hz e os
    # harmônicos em 70, 104 e 139 Hz.
    fan = gen.low_pass(decode(download(*RECORDINGS["fan"])), 2500.0)
    gen.write("ventilation.wav", gen.make_loopable(fan))

    # Bips esporádicos: sistemas de bordo que avisam de si. Curtos e graves; os
    # agudos do pacote de interface parecem um telefone, não uma nave.
    for index, name in enumerate(
            ["question_002.ogg", "confirmation_001.ogg", "question_004.ogg", "tick_004.ogg"],
            start=1):
        gen.write(f"beep_{index}.wav", load(interface, name))


if __name__ == "__main__":
    main()
