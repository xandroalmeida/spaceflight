# Áudio

Os sete arquivos em `assets/audio/` são **placeholders gerados**,
e o arquivo que os gera diz isso em voz alta: ruído filtrado e envelopes,
sintetizados por `scripts/generate_audio_placeholders.py`, não gravações.

```bash
python3 scripts/generate_audio_placeholders.py
```

Sem dependências: `wave` e `math` da biblioteca padrão. Um requisito de numpy
para produzir sete segundos de áudio seria caro pelo que entrega.

| arquivo | o que é | como foi feito |
|---|---|---|
| `engine_loop.wav` | o motor, ouvido através da estrutura | ruído passa-baixo a 90 Hz + parciais a 47, 71,5 e 143 Hz, com modulação lenta a 1,7 Hz |
| `ventilation.wav` | a ventilação da cabine | ruído passa-banda 120–1400 Hz, com duas oscilações lentas |
| `rcs_thump.wav` | uma válvula a abrir | um golpe a 165 Hz com decaimento rápido + um sopro passa-alta |
| `switch_click.wav` | interruptor | ruído passa-alta a 2600 Hz, 70 ms |
| `button_press.wav` | botão | o mesmo, mais grave e mais lento |
| `warning_tone.wav` | aviso | dois tons alternados a 3 Hz, 740 e 590 Hz |
| `computer_notify.wav` | o computador avisa | duas notas a subir, 660 e 880 Hz, curtas |

Os laços de `engine_loop` e `ventilation` são cruzados com o início por
`make_loopable()`: um laço com descontinuidade produz um clique a cada volta, e a
cada volta o ouvido aprende melhor a esperá-lo — que é como um som ambiente passa
de imperceptível a insuportável em trinta segundos.

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

O tom sobe um pouco com o empuxo — 0,92× em marcha lenta, 1,08× no máximo. É a
única coisa no áudio que não corresponde a nada físico, e está dita em voz alta
em `app/presentation/audio_director.cpp` em vez de embutida.

## Substituir

Troque o arquivo e mantenha o nome. Nenhuma linha de código conhece o conteúdo.
O que toca em laço é decidido em `app/presentation/audio_director.cpp` (os dois
clips que ele pede por `set_loop`), e não num ajuste do arquivo, pela razão de
sempre: a propriedade fica onde ela é lida.

Se um dia houver som de verdade, o que se quer é:

```
engine_loop        um motor grande ouvido através de metal, não um foguete ao ar livre
ventilation        ar forçado por dutos estreitos, sem tom
rcs_thump          uma válvula pneumática e um golpe seco na estrutura
switch_click       um interruptor de qualidade aeroespacial, seco e curto
button_press       o mesmo, mais macio
warning_tone       alternado, não contínuo: um tom fixo some no fundo
computer_notify    dois tons. O computador avisa; não celebra
```
