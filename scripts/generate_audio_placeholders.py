#!/usr/bin/env python3
"""Gera os sons-substitutos do cockpit (regra 36).

São PLACEHOLDERS e o arquivo diz isso em voz alta: ruído filtrado e envelopes,
sintetizados aqui, não gravações. Existem para que o simulador tenha retorno
sonoro desde o primeiro dia (regra 37) e para que trocá-los por som de verdade
seja substituir um arquivo -- nenhuma linha de código conhece o conteúdo deles.

Sem dependências: `wave` e `math` da biblioteca padrão. Um requisito de numpy
para produzir sete segundos de áudio seria caro pelo que entrega.

    python3 scripts/generate_audio_placeholders.py

Escreve em assets/audio/.

## O que NÃO está aqui

Nada que soe através do vácuo. Não há som de motor "de fora", não há explosão,
não há passagem de nave. O que existe são sons ESTRUTURAIS -- o que se ouve
dentro do casco porque o casco está a vibrar -- e sons de interface. A câmera
externa é silenciosa, e isso é uma decisão de física, não de mixagem (regra 36).
"""

from __future__ import annotations

import math
import pathlib
import random
import struct
import wave

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / "assets/audio"


def write(name: str, samples: list[float]) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    # Normalizado a -3 dBFS e não a 0: mixar sete fontes num barramento que já
    # está no topo é o caminho mais curto para clipping, e o volume real é
    # decidido pelo `AudioDirector`.
    gain = 0.707 / peak
    frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * gain)) * 32767))
                      for s in samples)
    path = OUT / name
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(frames)
    print(f"{path.relative_to(OUT.parent.parent.parent.parent)}  {len(samples) / RATE:.2f} s")


def noise(n: int, seed: int) -> list[float]:
    rng = random.Random(seed)
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def low_pass(signal: list[float], cutoff_hz: float) -> list[float]:
    """Um polo. É um filtro pobre e é o filtro certo aqui: o que se quer é tirar
    o brilho do ruído branco, não desenhar uma resposta."""
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / RATE)
    out, state = [], 0.0
    for sample in signal:
        state += alpha * (sample - state)
        out.append(state)
    return out


def high_pass(signal: list[float], cutoff_hz: float) -> list[float]:
    low = low_pass(signal, cutoff_hz)
    return [s - l for s, l in zip(signal, low)]


def make_loopable(signal: list[float], crossfade: float = 0.15) -> list[float]:
    """Cruza o fim com o começo para que o laço não estale.

    Um laço com uma descontinuidade produz um clique a cada volta, e a cada volta
    o ouvido aprende melhor a esperá-lo -- que é como um som ambiente passa de
    imperceptível a insuportável em trinta segundos.

    A cauda entra por baixo do COMEÇO e o laço é cortado onde ela começava: a
    última amostra é signal[n - fade - 1] e a primeira, que vem logo depois na
    volta, é a cauda em signal[n - fade] -- vizinhas no original. A versão que
    cruzava a cauda no FIM terminava em signal[fade - 1] e voltava a signal[0],
    um salto de 11347 contra um passo típico de ~1500 no engine_loop antigo."""
    n = len(signal)
    fade = int(n * crossfade)
    out = list(signal[:n - fade])
    for i in range(fade):
        t = i / fade
        out[i] = signal[n - fade + i] * (1.0 - t) + signal[i] * t
    return out


def click(seed: int, brightness: float, decay: float, duration: float) -> list[float]:
    n = int(RATE * duration)
    crack = high_pass(noise(n, seed), brightness)
    return [crack[i] * math.exp(-(i / RATE) * decay) for i in range(n)]


def warning_tone() -> list[float]:
    """Dois tons alternados a 3 Hz. Alternado e não contínuo porque um tom fixo
    some no fundo e um alternado não."""
    n = int(RATE * 0.9)
    out = []
    for i in range(n):
        t = i / RATE
        frequency = 740.0 if int(t * 6.0) % 2 == 0 else 590.0
        envelope = min(1.0, t * 40.0) * min(1.0, (0.9 - t) * 12.0)
        out.append(math.sin(2.0 * math.pi * frequency * t) * envelope * 0.8)
    return out


def computer_notify() -> list[float]:
    """Duas notas a subir, curtas. O computador avisa; não celebra."""
    n = int(RATE * 0.42)
    out = []
    for i in range(n):
        t = i / RATE
        frequency = 660.0 if t < 0.14 else 880.0
        local = t if t < 0.14 else t - 0.14
        envelope = math.exp(-local * 11.0) * min(1.0, local * 120.0)
        out.append(math.sin(2.0 * math.pi * frequency * t) * envelope * 0.7)
    return out


if __name__ == "__main__":
    write("switch_click.wav", click(53, 2600.0, 150.0, 0.07))
    write("button_press.wav", click(59, 1500.0, 95.0, 0.10))
    write("warning_tone.wav", warning_tone())
    write("computer_notify.wav", computer_notify())
