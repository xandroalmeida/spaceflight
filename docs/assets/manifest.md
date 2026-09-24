# Asset manifest — Milestones 7 e 8

O que a imagem precisa, o que já existe, e o que tem de ser gerado fora daqui.

Esta tabela é a fonte da verdade sobre o estado visual. Um asset só sai de
`PLACEHOLDER` quando o arquivo definitivo está no caminho de destino **e** o
executável o carrega; até lá o simulador roda com o substituto e não bloqueia
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
| `PROCEDURAL` | gerado em código (shader, ruído do `stb_perlin`, mesh). **Não** pedir externamente — regra 46. |
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
| `earth_albedo` | `MEASURED` | **NASA Blue Marble NG**, dezembro/2004, 21600×10800 → 8192×4096. Polos colapsados; o subsolar cai no Pacífico e o antissolar no Saara | [como](#as-texturas-da-terra-sao-medidas) | `assets/textures/earth/earth_albedo.jpg` |
| `earth_clouds` | `MEASURED` | **composto MODIS da NASA**, 8192×4096 → 4096×2048, cinza de 1 canal; histograma p25 14 / p50 60 / p75 128, remapeado 41..153 no shader | [como](#as-texturas-da-terra-sao-medidas) | `assets/textures/earth/earth_clouds.jpg` |
| `earth_night_lights` | `MEASURED` | **VIIRS Black Marble 2016**, 3600×1800 → 4096×2048; registra com o albedo em longitude (0,0°, terra 0,997 contra 0,911 a −8,4°) | [como](#as-texturas-da-terra-sao-medidas) | `assets/textures/earth/earth_night.jpg` |
| `earth_normal` | `OPTIONAL` | sem relevo | — | `assets/textures/earth/earth_normal.png` |
| `moon_albedo` | `INTEGRATED` | mares no centro (face visível), terras altas nas bordas, raios de Tycho | [prompt](planets/moon-albedo-codex-prompt.md) | `assets/textures/moon/moon_albedo.png` |
| `moon_normal` | `INTEGRATED` | **derivado**, não pintado: LOLA LDEM_16 → `scripts/make_moon_normal.py`. Mares em (127–131, 126–128, 254–255), \|n\| = 1,000 | [como](#o-normal-map-nao-e-uma-imagem) | `assets/textures/moon/moon_normal.png` |
| `mars_albedo` | `PROCEDURAL` | substituto: ocre, regiões escuras no sul, calotas de borda irregular. Acerta a cor e o contraste; **não é um mapa de Marte** | [prompt](planets/mars-albedo-codex-prompt.md) | `assets/textures/mars/mars_albedo.png` |
| `mars_normal` | `REFUSED` | topografia do MOLA existe e é medida: se o relevo for desejado ele é **calculado**, como o da Lua. Não pedir a um gerador de imagens (regra 25) | [porquê](planets/mars-albedo-codex-prompt.md#o-que-nao-pedir-a-um-gerador-de-imagens) | — |
| Mercúrio, Vênus, Júpiter, Saturno, Urano, Netuno, Plutão | `PROCEDURAL` | cor média do disco, em `CelestialView::colour_for` (`app/presentation/scene/celestial_view.cpp`). Reconhecíveis a distância e nada mais (regra 69) | — | — |
| anéis de Saturno | `BACKLOG` | desejáveis para reconhecimento (regra 70); nada no M8 depende deles | — | — |
| limbo atmosférico da Terra | `PROCEDURAL` | termo de rim scattering no shader | — | `app/shaders/body.frag` |
| limbo atmosférico de Marte | `PROCEDURAL` | mesmo shader, 0,12 de intensidade e ocre: a atmosfera marciana tem 0,6 % da pressão terrestre e o limbo dela é um fio. É DESENHO -- não há aerocaptura (regra 56) | — | `app/shaders/body.frag` |
| disco e brilho do Sol | `PROCEDURAL` | corpo auto-luminoso (corpo negro, sem iluminação) + a luz direcional do Sol nos shaders de superfície | — | `app/shaders/body.frag`, `app/shaders/lit.frag` |

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

O shader lê os três canais como dado, sem decodificação sRGB, e normaliza; o
|n| = 1 que o validador mede é o que garante que a direção chega certa
([import-settings.md](import-settings.md#normal-maps)).


## Nave

| asset | estado | placeholder | prompt | destino |
|---|---|---|---|---|
| geometria da nave | `PROCEDURAL` | `SpacecraftVisual`, primitivas geradas em `scene/mesh.cpp` | — | `app/presentation/scene/spacecraft_visual.cpp` |
| `hull_panels` | `INTEGRATED` | 1024×1024, ladrilhável nos dois sentidos; no material `hull_paint`, `uv_scale` (5, 4) | [prompt](spacecraft/hull-panels-codex-prompt.md) | `assets/textures/spacecraft/hull_panels.png` |
| `radiator_surface` | `OPTIONAL` | material `radiator` (emissivo fraco) | — | `assets/textures/spacecraft/radiator.png` |
| `thermal_blanket` | `OPTIONAL` | material `thermal_blanket` (dourado, rugoso) | — | `assets/textures/spacecraft/blanket.png` |
| pluma do motor | `PROCEDURAL` | mesh cônico + material aditivo, intensidade = empuxo real | — | `EnginePlume`, `app/presentation/scene/spacecraft_visual.cpp` |
| jatos de RCS | `PROCEDURAL` | quads aditivos por thruster, acesos pelo atuador | — | `RcsVisual`, `app/presentation/scene/spacecraft_visual.cpp` |
| modelo 3D artístico | `OPTIONAL` | procedural basta para o M7 | [spacecraft/3d-model-brief.md](spacecraft/3d-model-brief.md) | — |

## Cockpit

| asset | estado | placeholder | prompt | destino |
|---|---|---|---|---|
| geometria do cockpit | `PROCEDURAL` | `CockpitInterior`, primitivas | — | `app/presentation/scene/cockpit.cpp` |
| `panel_surface` | `INTEGRATED` | **sem um único caractere** (regra 44); no material `cockpit_panel`, `uv_scale` (2, 2) | [prompt](cockpit/panel-surface-codex-prompt.md) | `assets/textures/cockpit/panel_surface.png` |
| `warning_patterns` | `PROCEDURAL` | listras desenhadas no shader | — | — |
| vidro das janelas | `PROCEDURAL` | material `glass` | — | — |
| vidro dos displays | `PROCEDURAL` | material `display_glass` + a imagem do instrumento desenhada numa textura pelo ImGui | — | — |

## UI

| asset | estado | placeholder | prompt | destino |
|---|---|---|---|---|
| retículas, marcadores de vetor | `PROCEDURAL` | `Instrument::draw_marker` no PFD | — | — |
| moldura dos displays | `PROCEDURAL` | `Instrument::draw_frame` | — | — |
| linhas de órbita | `PROCEDURAL` | pontos vindos do core, ligados por linhas | — | — |
| fonte | `INTEGRATED` | DejaVu Sans Mono, licença livre (`DejaVu-LICENSE.txt` ao lado) | — | `assets/fonts/DejaVuSansMono.ttf` |

## Áudio

Duas origens, commitadas como WAV mono de 22 050 Hz. Os sons da nave vêm de
gravações CC0 da Kenney e do Freesound, importadas e filtradas por
`scripts/import_ship_audio.py`; os de interface continuam sendo
**placeholders honestos** de `scripts/generate_audio_placeholders.py` — ruído
filtrado e envelopes, não gravações. Substituíveis um a um sem tocar em código.

| asset | estado | destino |
|---|---|---|
| `engine_impulse` | `INTEGRATED` — Kenney `spaceEngineLow_003`, CC0 | `assets/audio/engine_impulse.wav` |
| `engine_cruise` | `INTEGRATED` — Kenney `spaceEngineSmall_000`, CC0 | `assets/audio/engine_cruise.wav` |
| `rcs_thump` | `INTEGRATED` — Freesound 185515 (soneproject), CC0 | `assets/audio/rcs_thump.wav` |
| `rcs_hiss` | `INTEGRATED` — Freesound 347576 (DJT4NN3R), CC0 | `assets/audio/rcs_hiss.wav` |
| `equipment` | `INTEGRATED` — Freesound 675339 (craigsmith), CC0 | `assets/audio/equipment.wav` |
| `ventilation` | `INTEGRATED` — Freesound 704393 (jgxxx), CC0 | `assets/audio/ventilation.wav` |
| `beep_1` … `beep_4` | `INTEGRATED` — Kenney Interface Sounds, CC0 | `assets/audio/beep_N.wav` |
| `switch_click` | `PLACEHOLDER` | `assets/audio/switch_click.wav` |
| `button_press` | `PLACEHOLDER` | `assets/audio/button_press.wav` |
| `warning_tone` | `PLACEHOLDER` | `assets/audio/warning_tone.wav` |
| `computer_notify` | `PLACEHOLDER` | `assets/audio/computer_notify.wav` |

## Detalhe por categoria

* [planetas](planets/) — quatro prompts prontos
* [nave](spacecraft/) — [dimensões](spacecraft/dimensions.md), um prompt de textura, um brief de modelo 3D
* [cockpit](cockpit/README.md) — geometria procedural, um prompt de textura
* [UI](ui/README.md) — **nada** gerado externamente, e porquê
* [áudio](audio/README.md) — dez sons CC0 importados, quatro placeholders sintetizados, e como substituí-los

## Como as texturas são carregadas

Não há mais `.import`: o executável lê o arquivo que está em `assets/`, e o que
antes eram ajustes de importação — sRGB ou dado, mipmaps — é decidido no código
que pede a textura. O que importa é que o normal map e a máscara de nuvens são
**dados** e sobem sem decodificação sRGB; se subissem como cor, o relevo e a
cobertura sairiam errados numa direção que ninguém procura, e a imagem
continuaria a parecer certa.

O detalhe, e porquê, está em [import-settings.md](import-settings.md).

## Regra que este arquivo existe para respeitar

Uma imagem gerada nunca é fonte de física (regra 47). A textura da Terra diz
como a Terra **parece**; onde ela está, quanto ela gira e que tamanho tem vem
de `core/` e de `kernels/spice`, sempre.


## As texturas da Terra são medidas

As três vieram de prompts no Milestone 7 (`planets/earth-*-codex-prompt.md`, que
ficam como registo). Elas eram boas de perto e erradas de longe: o mapa de nuvens
gerado tinha cobertura quase total, sem as regiões limpas que a circulação
atmosférica produz, e de uma órbita de 400 km o que se via pela janela era uma
pasta branca sem estrutura nenhuma. Uma nuvem não é ruído bonito -- ela tem
ciclones, frentes, a ZCIT e os anticiclones subtropicais, e nada disso sai de um
gerador que nunca mediu o planeta.

Agora vêm do arquivo público da NASA, de domínio público (NASA Earth Observatory
/ Visible Earth, crédito a NASA Goddard Space Flight Center):

    scripts/fetch_earth_textures.sh     # 68 MB, fora do Git
    scripts/make_earth_textures.sh      # deriva o que entra no Git

Dezembro de 2004 e não uma média de meses: um mapa estático não segue as
estações, então vale escolher a estação certa para a época de partida -- a missão
começa em 1 de janeiro -- em vez de um mês que não é nenhum.

**São JPEG, e o resto do projeto é PNG.** As outras texturas foram AUTORADAS: um
gerador entregou pixels exatos e um PNG guarda exatamente esses. Estas foram
MEDIDAS e a NASA já as distribui em JPEG -- recodificá-las em PNG são cinco vezes
os bytes (31 MB contra 6,7 MB só no albedo) para preservar informação que nunca
existiu. (No Godot o que chegava à GPU era BPTC ou S3TC, que perdia muito mais;
hoje sobe descomprimido, e o JPEG é a única perda.)

`scripts/validate_textures.py` decodifica PNG e só PNG, porque não tem
dependências e essa é a regra dele. Então a conferência das três corre dentro de
`make_earth_textures.sh`, sobre os intermediários PNG, com os mesmos pixels que a
seguir viram JPEG: o encoder não move um pixel de longitude. As medidas são as de
sempre -- proporção 2:1, costura de longitude, polos uniformes, e o registo
cruzado entre o albedo e as luzes noturnas.
