# Asset manifest — Milestones 7 e 8

O que a imagem precisa, o que já existe, e o que tem de ser gerado fora daqui.

Esta tabela é a fonte da verdade sobre o estado visual. Um asset só sai de
`PLACEHOLDER` quando o arquivo definitivo está no caminho de destino **e** o
Godot o importa; até lá o simulador roda com o substituto e não bloqueia
(regra 81).

## Como um asset entra

Nenhuma imagem é ligada sem conferência, e a conferência é numérica antes de ser
visual: cabeçalho PNG, proporção, costura entre as bordas, uniformidade dos
polos, convenção de normal map, e — quando duas imagens têm de concordar —
registro cruzado.

```bash
python3 scripts/validate_textures.py      # ou: cmake --build build --target asset-validation
```

Sem engine, sem kernels e sem tela: decodifica os PNG em Python puro. Foi ele
que recusou uma das sete imagens do Milestone 7, e três defeitos do próprio
renderizador só apareceram porque as texturas tornaram a orientação verificável.

Um asset já recusado está listado no topo do script e a sua reprovação **não**
derruba a suíte — uma suíte vermelha por um estado conhecido é uma suíte que se
aprende a ignorar. O script também avisa no caso contrário: se um asset recusado
passar em tudo, ele foi regerado e o manifesto é que está desatualizado.

## Estados

| estado | significado |
|---|---|
| `PROCEDURAL` | gerado em código (shader, `FastNoiseLite`, mesh). **Não** pedir externamente — regra 46. |
| `PLACEHOLDER` | há um substituto funcional no jogo; o definitivo ainda não existe |
| `GENERATE` | precisa ser gerado fora do Claude Code; o prompt está pronto no arquivo indicado |
| `READY` | o arquivo definitivo existe no repositório, ainda não integrado |
| `INTEGRATED` | no caminho de destino e carregado pela cena |
| `REJECTED` | entregue, **conferido e recusado**; o arquivo fica, com a medição que o reprovou |
| `OPTIONAL` | melhoraria a imagem e nada depende dele |
| `PROCEDURAL` | gerado em código; o arquivo externo substitui-o sem outra mudança |
| `BACKLOG` | reconhecido como desejável, não feito, não bloqueante |
| `REFUSED` | **não** deve ser gerado por modelo de imagem: é uma grandeza física com fonte medida (regra 25) |

## Planetas

| asset | estado | verificação | prompt | destino |
|---|---|---|---|---|
| `earth_albedo` | `INTEGRATED` | 2048×1024, 2:1, polos uniformes; o subsolar cai no Pacífico e o antissolar no Saara | [prompt](planets/earth-albedo-codex-prompt.md) | `assets/textures/earth/earth_albedo.png` |
| `earth_clouds` | `INTEGRATED` | cinza de 1 canal; histograma p25 38 / p50 115 / p75 191, remapeado 82..204 no shader | [prompt](planets/earth-clouds-codex-prompt.md) | `assets/textures/earth/earth_clouds.png` |
| `earth_night_lights` | `INTEGRATED` | 90,6 % preto, âmbar (R>G>B); registra com o albedo em longitude (dx = 0) | [prompt](planets/earth-night-lights-codex-prompt.md) | `assets/textures/earth/earth_night.png` |
| `earth_normal` | `OPTIONAL` | sem relevo | — | `godot/project/assets/textures/earth/earth_normal.png` |
| `moon_albedo` | `INTEGRATED` | mares no centro (face visível), terras altas nas bordas, raios de Tycho | [prompt](planets/moon-albedo-codex-prompt.md) | `assets/textures/moon/moon_albedo.png` |
| `moon_normal` | `INTEGRATED` | **derivado**, não pintado: LOLA LDEM_16 → `scripts/make_moon_normal.py`. Mares em (127–131, 126–128, 254–255), \|n\| = 1,000 | [como](#o-normal-map-nao-e-uma-imagem) | `assets/textures/moon/moon_normal.png` |
| `mars_albedo` | `PROCEDURAL` | substituto: ocre, regiões escuras no sul, calotas de borda irregular. Acerta a cor e o contraste; **não é um mapa de Marte** | [prompt](planets/mars-albedo-codex-prompt.md) | `assets/textures/mars/mars_albedo.png` |
| `mars_normal` | `REFUSED` | topografia do MOLA existe e é medida: se o relevo for desejado ele é **calculado**, como o da Lua. Não pedir a um gerador de imagens (regra 25) | [porquê](planets/mars-albedo-codex-prompt.md#o-que-nao-pedir-a-um-gerador-de-imagens) | — |
| Mercúrio, Vênus, Júpiter, Saturno, Urano, Netuno, Plutão | `PROCEDURAL` | cor média do disco, em `CelestialView._colour_for`. Reconhecíveis a distância e nada mais (regra 69) | — | — |
| anéis de Saturno | `BACKLOG` | desejáveis para reconhecimento (regra 70); nada no M8 depende deles | — | — |
| limbo atmosférico da Terra | `PROCEDURAL` | shader de rim scattering | — | `shaders/planet_surface.gdshader` |
| limbo atmosférico de Marte | `PROCEDURAL` | mesmo shader, 0,12 de intensidade e ocre: a atmosfera marciana tem 0,6 % da pressão terrestre e o limbo dela é um fio. É DESENHO -- não há aerocaptura (regra 56) | — | `shaders/relativistic_body.gdshader` |
| disco e brilho do Sol | `PROCEDURAL` | shader auto-luminoso + `DirectionalLight3D` | — | `shaders/relativistic_body.gdshader` |

### O normal map não é uma imagem

A primeira tentativa foi um asset **gerado como figura**, e foi recusada por
medição. A segunda é **calculada**, e é assim que este se produz:

```bash
./scripts/fetch_lunar_dem.sh        # LOLA LDEM_16, 33 MB, fora do Git
python3 scripts/make_moon_normal.py # 2,6 s
python3 scripts/validate_textures.py
```

A razão da troca é estrutural e vale mais do que o asset: **uma normal é uma
quantidade calculada a partir de um campo de altura**, não uma coisa que se
pinte. Um gerador de imagens produz o que um normal map *parece* — azul-lavanda
com relevo em R e G — sem ter de onde tirar a altura. Pedir de novo, com um
prompt mais insistente, ataca o sintoma.

A diferença, medida nos três mares, que são as superfícies mais lisas da Lua e
onde um normal map correto tem de valer (128, 128, 255) com |n| = 1:

```
                        pintado                   derivado do LOLA
Mare Imbrium         50,7  54,0 246,5  1,290    131,0 126,5 254,5  1,000
Oceanus Procellarum  44,9  52,2 247,7  1,293    127,3 127,7 255,0  1,000
Mare Serenitatis     47,3  54,8 247,2  1,266    127,3 127,7 255,0  1,000
```

No pintado, R e G seguiam o **albedo** — escuro dava 50, claro dava 94 — e os
vetores iam de 0,68 a 1,29 de comprimento. Ligá-lo inclinaria todas as normais
proporcionalmente ao brilho.

A topografia é real: LOLA/LRO, 1895 m/pixel, relevo de −8 937 a +10 522 m em
torno de 1737,4 km. Os declives que saem dela a 2048 × 1024 são p50 2,7°,
p90 9,0°, p99 15,1° — a Lua como ela é, sem exageração vertical (a constante
existe no script, com um nome, e está em 1,0).

Duas coisas que a conta tem de acertar e que quase sempre se erram:

* **a métrica esférica.** Uma coluna representa `R·cos(φ)·Δλ` de chão e uma
  linha representa `R·Δφ`. Ignorar o `cos(φ)` exagera as encostas leste-oeste
  por `1/cos(φ)` — o dobro a 60 graus, seis vezes a 80;
* **os polos.** Lá o `cos(φ)` tende a zero e a diferença finita passa a ser
  sobre uma distância minúscula, amplificando o ruído do DEM. A correção não é
  limitar o `cos`: é alargar o estêncil em longitude na mesma proporção, de modo
  que ele cubra sempre a mesma quantidade de chão.

O `.import` leva `compress/normal_map=1` (RGTC), que guarda R e G e reconstrói o
azul — só correto porque |n| = 1 exatamente, que é o que o validador mede.


## Nave

| asset | estado | placeholder | prompt | destino |
|---|---|---|---|---|
| geometria da nave | `PROCEDURAL` | `SpacecraftVisual`, primitivas + `SurfaceTool` | — | `scripts/world/spacecraft_visual.gd` |
| `hull_panels` | `INTEGRATED` | 1024×1024, ladrilhável nos dois sentidos; em `HullPaint`, `uv1_scale` (5, 4) | [prompt](spacecraft/hull-panels-codex-prompt.md) | `assets/textures/spacecraft/hull_panels.png` |
| `radiator_surface` | `OPTIONAL` | material `Radiator` (emissivo fraco, anisotrópico) | — | `godot/project/assets/textures/spacecraft/radiator.png` |
| `thermal_blanket` | `OPTIONAL` | material `ThermalBlanket` (dourado, rugoso) | — | `godot/project/assets/textures/spacecraft/blanket.png` |
| pluma do motor | `PROCEDURAL` | mesh cônico + shader aditivo, intensidade = empuxo real | — | `scripts/world/engine_plume.gd` |
| jatos de RCS | `PROCEDURAL` | quads aditivos por thruster, acesos pelo atuador | — | `scripts/world/rcs_visual.gd` |
| modelo 3D artístico | `OPTIONAL` | procedural basta para o M7 | [spacecraft/3d-model-brief.md](spacecraft/3d-model-brief.md) | — |

## Cockpit

| asset | estado | placeholder | prompt | destino |
|---|---|---|---|---|
| geometria do cockpit | `PROCEDURAL` | `CockpitInterior`, primitivas | — | `scripts/cockpit/cockpit_interior.gd` |
| `panel_surface` | `INTEGRATED` | **sem um único caractere** (regra 44); em `CockpitPanel`, `uv1_scale` (2, 2) | [prompt](cockpit/panel-surface-codex-prompt.md) | `assets/textures/cockpit/panel_surface.png` |
| `warning_patterns` | `PROCEDURAL` | listras desenhadas no shader | — | — |
| vidro das janelas | `PROCEDURAL` | material `Glass` | — | — |
| vidro dos displays | `PROCEDURAL` | material `DisplayGlass` + `SubViewport` | — | — |

## UI

| asset | estado | placeholder | prompt | destino |
|---|---|---|---|---|
| retículas, marcadores de vetor | `PROCEDURAL` | `_draw()` no PFD | — | — |
| moldura dos displays | `PROCEDURAL` | `StyleBoxFlat` + cantos desenhados | — | — |
| linhas de órbita | `PROCEDURAL` | pontos vindos do core, `_draw()` | — | — |
| fonte | `INTEGRATED` | `SystemFont` monoespaçada (Menlo/SF Mono/Consolas/DejaVu) | — | — |

## Áudio

Todos gerados proceduralmente por `scripts/generate_audio_placeholders.py` e
commitados como WAV curtos. São **placeholders honestos**: ruído filtrado e
envelopes, não gravações. Substituíveis um a um sem tocar em código.

| asset | estado | destino |
|---|---|---|
| `engine_loop` | `PLACEHOLDER` | `godot/project/assets/audio/engine_loop.wav` |
| `rcs_thump` | `PLACEHOLDER` | `godot/project/assets/audio/rcs_thump.wav` |
| `switch_click` | `PLACEHOLDER` | `godot/project/assets/audio/switch_click.wav` |
| `button_press` | `PLACEHOLDER` | `godot/project/assets/audio/button_press.wav` |
| `warning_tone` | `PLACEHOLDER` | `godot/project/assets/audio/warning_tone.wav` |
| `computer_notify` | `PLACEHOLDER` | `godot/project/assets/audio/computer_notify.wav` |
| `ventilation` | `PLACEHOLDER` | `godot/project/assets/audio/ventilation.wav` |

## Detalhe por categoria

* [planetas](planets/) — quatro prompts prontos
* [nave](spacecraft/) — [dimensões](spacecraft/dimensions.md), um prompt de textura, um brief de modelo 3D
* [cockpit](cockpit/README.md) — geometria procedural, um prompt de textura
* [UI](ui/README.md) — **nada** gerado externamente, e porquê
* [áudio](audio/README.md) — sete placeholders sintetizados, e como substituí-los

## Import settings do Godot

Os `.import` **são versionados**, e não é detalhe de conveniência: é neles que
vive `compress/normal_map=1` do `moon_normal`. Sem ele o Godot importa o normal
map como uma textura de cor, com decodificação sRGB, e o relevo sai errado numa
direção que ninguém procura — a imagem continua a parecer um normal map. Um
clone que gerasse os `.import` do zero herdaria os defaults, não estes valores.

O que cada um tem de dizer, e porquê, está em
[import-settings.md](import-settings.md).

## Regra que este arquivo existe para respeitar

Uma imagem gerada nunca é fonte de física (regra 47). A textura da Terra diz
como a Terra **parece**; onde ela está, quanto ela gira e que tamanho tem vem
de `core/` e de `kernels/spice`, sempre.
