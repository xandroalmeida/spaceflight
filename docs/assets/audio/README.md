# Áudio

`assets/audio/` tem duas origens.

**Os sons da nave** — motor, RCS, ambiente e bips — vêm de gravações CC0
(domínio público) da [Kenney](https://kenney.nl) e do
[Freesound](https://freesound.org), importadas e filtradas por
`scripts/import_ship_audio.py`:

```bash
python3 scripts/import_ship_audio.py     # precisa de ffmpeg ou `pip install soundfile`
```

O script baixa as fontes para `external/downloads/` com checksum, decodifica os
OGG e MP3 e faz o resto com a biblioteca padrão, pelos filtros do gerador abaixo. É a
única dependência fora da stdlib, e só para REIMPORTAR: o jogo lê os WAV que já
estão no repositório.

| arquivo | o que é | origem | o que se fez |
|---|---|---|---|
| `engine_impulse.wav` | o motor em IMPULSO (200 kN), ouvido pela estrutura | Sci-fi Sounds, `spaceEngineLow_003` | passa-baixo a 900 Hz, laço |
| `engine_cruise.wav` | o motor em CRUZEIRO (11 kN a 0,5 c) | Sci-fi Sounds, `spaceEngineSmall_000` | passa-banda 120–2400 Hz, laço |
| `rcs_thump.wav` | o golpe da válvula a cada disparo manual | Freesound 185515, soneproject, "Air compressor valve" | passa-banda 150–3000 Hz, fim esmaecido |
| `rcs_hiss.wav` | os propulsores enquanto os bicos estão abertos | Freesound 347576, DJT4NN3R, "thrusters_loop" | passa-banda 150–3000 Hz, laço |
| `equipment.wav` | o zumbido das máquinas da nave, linhas em 130–140 Hz | Freesound 675339, craigsmith, "Alien hum or space ship interior" | passa-baixo a 2500 Hz, laço |
| `ventilation.wav` | uma ventoinha: passagem de pás em 35 Hz e harmônicos | Freesound 704393, jgxxx, "Fan looping" | passa-baixo a 2500 Hz, laço |
| `beep_1.wav` … `beep_4.wav` | sistemas de bordo que avisam de si | Interface Sounds, `question_002`, `confirmation_001`, `question_004`, `tick_004` | nenhum |

Licença: Creative Commons Zero 1.0. Atribuição não é obrigatória; creditamos
assim mesmo — Kenney, [Sci-fi Sounds](https://kenney.nl/assets/sci-fi-sounds) e
[Interface Sounds](https://kenney.nl/assets/interface-sounds); craigsmith,
[Alien hum or space ship interior](https://freesound.org/people/craigsmith/sounds/675339/);
jgxxx, [Fan looping](https://freesound.org/people/jgxxx/sounds/704393/);
soneproject, [Air compressor valve](https://freesound.org/people/soneproject/sounds/185515/);
DJT4NN3R, [thrusters_loop](https://freesound.org/people/DJT4NN3R/sounds/347576/). Do
Freesound usamos a prévia HQ (MP3), que se baixa sem conta.

O fundo é TONAL de propósito. A primeira versão tinha ruído largo filtrado na
ventilação (planura espectral 0,74 — quase ruído branco) e o `computerNoise` da
Kenney no equipamento, que é uma nota nova a cada 100–150 ms: soavam a chiado e
a bipes contínuos. Um ambiente de nave de verdade é ventoinhas e bombas em
frequências fixas, batendo devagar umas contra as outras.

O RCS também foi escolhido de ouvido, depois que a primeira versão — o jato
agudo `thrusterFire` da Kenney e um golpe sintetizado — não convenceu: quatro
pulsos e quatro sopros candidatos, todos CC0, tocados pelo mesmo filtro de
casco e no mesmo volume.

**Os sons de interface** são **placeholders gerados**, e o
arquivo que os gera diz isso em voz alta: ruído filtrado e envelopes,
sintetizados por `scripts/generate_audio_placeholders.py`, não gravações.

```bash
python3 scripts/generate_audio_placeholders.py
```

Sem dependências: `wave` e `math` da biblioteca padrão.

| arquivo | o que é | como foi feito |
|---|---|---|
| `switch_click.wav` | interruptor | ruído passa-alta a 2600 Hz, 70 ms |
| `button_press.wav` | botão | o mesmo, mais grave e mais lento |
| `warning_tone.wav` | aviso | dois tons alternados a 3 Hz, 740 e 590 Hz |
| `computer_notify.wav` | o computador avisa | duas notas a subir, 660 e 880 Hz, curtas |

Todo laço, dos dois scripts, passa por `make_loopable()`: a cauda é cruzada por baixo do começo e o
arquivo é cortado onde ela começava, de modo que a última amostra e a primeira
são vizinhas no original. Um laço com descontinuidade produz um clique a cada
volta, e a cada volta o ouvido aprende melhor a esperá-lo — que é como um som
ambiente passa de imperceptível a insuportável em trinta segundos. (O
`engine_loop.wav` sintético que os motores substituíram tinha exatamente esse
defeito: um salto de 11347 na volta, contra passos de ~1500 no resto.)

## Como a nave soa

* **Motor.** Um som por modo, e cada um escalado pelo empuxo máximo DO SEU modo:
  na escala de IMPULSO, os 11 kN do CRUZEIRO eram inaudíveis. Trocar de modo com
  o motor aceso é um cruzamento de ~120 ms, não um corte. O CRUZEIRO toca a 60 %
  do volume do IMPULSO no máximo — mais baixo porque é muito menos empuxo pelo
  mesmo casco, mas não na proporção de 1:18, porque aí não se ouviria. O
  RELATIVISTIC ainda não tem gravação própria: toca o estrondo do IMPULSO uma
  quinta abaixo (0,62× a 0,72×), mais grave e mais pesado que os modos de fusão.
* **RCS.** Dois sons: o golpe da válvula (`rcs_thump`) quando o piloto dispara, e
  o sopro (`rcs_hiss`) enquanto QUALQUER bico está aberto — o do piloto
  automático também. O sopro cresce com a raiz do número de bicos.
* **Ambiente.** Ventilação e equipamento tocam sempre, dentro, com pesos
  parecidos. A cada 15–45 s — esporádico, para que cada bip seja um evento e
  não um ritmo — um dos quatro bips, com volume e tom levemente variados,
  sorteados por um gerador de semente fixa.

## A regra que este diretório respeita

**Nada soa através do vácuo.** Não há som de motor "de fora", não há explosão,
não há passagem de nave. O que existe são sons ESTRUTURAIS — o que se ouve dentro
do casco porque o casco está a vibrar — e sons de interface. Na câmera externa,
`AudioDirector::set_interior(false)` cala tudo o que é da nave e deixa apenas a
interface, que não está no espaço: está no monitor de quem joga (regra 36).

## O volume do motor segue o EMPUXO

Pela mesma razão que a pluma: com o tanque vazio a tecla continua a funcionar e o
empuxo é zero. Um som que seguisse o acelerador continuaria a rugir sobre um
motor apagado.

O tom sobe um pouco com o empuxo — 0,92× a 1,08× em IMPULSO, 0,96× a 1,04× em
CRUZEIRO. É a única coisa no áudio que não corresponde a nada físico, e está dita em voz alta
em `app/presentation/audio_director.cpp` em vez de embutida.

## Substituir

Troque o arquivo e mantenha o nome. Nenhuma linha de código conhece o conteúdo.
O que toca em laço é decidido em `app/presentation/audio_director.cpp` (os
clips que ele pede por `set_loop`), e não num ajuste do arquivo, pela razão de
sempre: a propriedade fica onde ela é lida.

Para os placeholders que restam, o que se quer é:

```
switch_click       um interruptor de qualidade aeroespacial, seco e curto
button_press       o mesmo, mais macio
warning_tone       alternado, não contínuo: um tom fixo some no fundo
computer_notify    dois tons. O computador avisa; não celebra
```
