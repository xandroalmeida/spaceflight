# M7 — a demonstração, fotografada

Produzido por `scripts/m7_screenshots.sh` (`spaceflight --shots m7`). Precisa de
um **driver de GPU**, não de tela: o quadro é desenhado fora da tela numa textura
e cada fotografia é essa textura lida de volta (ADR-0009). O `--headless` não
serve: não abre GPU nenhuma, e uma corrida "bem sucedida" sem rasterizar um pixel
não provaria coisa nenhuma — que foi como o starfield ficou partido durante um
milestone inteiro (`docs/validation/starfield-debug.md`). Sem GPU o script sai 77
(Skipped).

A sequência é **reproduzível**: cada passo é uma condição sobre o estado da
simulação e não um número de quadros escolhido à mão, e o roteiro está em
`app/presentation/shot_director.cpp` (`m7_script()`). Quando uma condição não é atingida
dentro do orçamento de quadros, a sequência **fotografa na mesma e diz que não
foi** — uma corrida que abortasse não deixaria imagem nenhuma do que de facto
aconteceu, que é justamente o que se quer ver quando algo corre mal.

⚠️ **As imagens são evidência, não oráculo** (regra 60). Elas provam que a cena
põe alguma coisa na tela nos momentos certos, e o que uma imagem pode decidir e
nada mais pode é se aquilo é visível. Se está *bonito* é julgamento humano, e
nem este arquivo nem o script opinam.

| imagem | o que ela tem de mostrar |
|---|---|
| `cockpit-earth-orbit.png` | o primeiro quadro de um voo novo: cockpit, Terra pela janela, instrumentos já a mostrar números (regra 66) |
| `cockpit-instruments.png` | os quatro mostradores legíveis, com o nariz no prógrado |
| `earth-whole-disc.png` | a Terra inteira, de três raios de distância: continentes, nuvens, terminador e limbo ao mesmo tempo. É a imagem que decide a regra 31, e a única em que a orientação é conferível a olho |
| `moon-whole-disc.png` | a Lua inteira, com o terminador no quadro: é lá que a luz é rasante e o normal map se vê |
| `external-spacecraft.png` | a nave inteira, do lado iluminado, reconhecível como máquina de vácuo |
| `engine-plume.png` | a pluma acesa, com o comprimento a seguir o empuxo |
| `rcs-firing.png` | os jatos que o alocador abriu — não os que a tecla pediu |
| `orbit-map-earth.png` | o mapa: Terra, órbita atual, caminho real da Lua, anéis de escala |
| `moon-target.png` | o computador aberto com a Lua selecionada |
| `mission-plan.png` | Δv por queima, propelente, órbita prevista, candidatos voados |
| `transfer-map.png` | o arco translunar e os marcadores de queima com ETA |
| `lunar-approach.png` | a Lua a crescer, na fase `APPROACH`, com a contagem para a inserção |
| `lunar-capture.png` | dentro de 20 000 km da Lua, perto da queima |
| `lunar-orbit.png` | depois de a queima ACABAR, já em `ORBIT_INSERTION` |
| `lunar-orbit-external.png` | a nave em órbita lunar, vista de fora |

Para gerar outra vez:

```bash
./scripts/m7_screenshots.sh                       # 1920x1080, tudo
./scripts/m7_screenshots.sh /tmp/out 2560x1440    # outra resolução
SPACEFLIGHT_SHOT_STOP=3 ./scripts/m7_screenshots.sh   # só os três primeiros passos
./build/bin/spaceflight --shots m7 /tmp/out --stop 3  # o mesmo, direto
```

`SPACEFLIGHT_SHOT_STOP` existe porque iterar na imagem não pode exigir voar até à
Lua primeiro: com ele uma volta de ajuste visual custa segundos em vez de
minutos.

## Os marcadores do manual saem daqui

Ao lado de duas das imagens fica um `<nome>.anchors.json`: onde cada peça caiu
**na imagem**, em percentagem do quadro.

```json
{ "engine": { "x": 33.9, "y": 74.8 } }
```

O passo declara a peça em **metros no referencial do corpo** — das constantes de
`SpacecraftVisual`, a mesma fonte que desenha o casco — e quem a projeta é a
câmera que tirou a fotografia. O manual lê o JSON por `{{anchor:figura:peça}}` e
nunca vê um pixel.

Isto existe porque a alternativa envelhece em silêncio: o marcador "motor
principal" do capítulo 2 eram dois números escritos à mão sobre uma captura
antiga, e depois de a câmera mudar de enquadramento ele apontava para um
radiador, sem que nada falhasse.

Para o RCS, **qual** bico se marca é decisão do alocador e não do roteiro: o
passo escolhe o aberto mais voltado para a câmera, entre os de braço radial — os
quatro thrusters que ficam sobre o eixo longitudinal estão dentro do casco e
nunca se veem.

## Como a câmera externa é enquadrada

`ShotDirector::frame_sunlit()` deriva o azimute e a elevação da **direção do Sol** nos eixos
do casco, em vez de os fixar à mão. O lado contrário ao Sol fica escuro e tem de
ficar (regra 29) — mas um azimute fixo põe a câmera no lado errado assim que a
órbita avança, e a fotografia de demonstração sai preta com dois radiadores a
brilhar. Isto é enquadramento, não física: nada do que se vê muda, só de onde se
vê.
