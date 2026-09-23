# Planetas

Para o M7 importam três: **Terra, Lua, Sol** (regra 30). Os outros corpos do
catálogo continuam desenhados com a cor de base e ninguém parou o milestone por
eles.

## O que é procedural e o que tem de ser gerado

| | estado | onde |
|---|---|---|
| a superfície da Terra e da Lua | `INTEGRATED` | as texturas entregues estão em `assets/textures/`; se faltarem, o corpo é desenhado com a cor de base (ver [os substitutos](#os-substitutos)) |
| o limbo atmosférico | `PROCEDURAL` | `app/shaders/body.frag`, um termo de rim scattering |
| o lado noturno | textura + shader | a emissão vem do mapa de luzes; o desvanecimento no terminador é do shader |
| o disco e o brilho do Sol | `PROCEDURAL` | o corpo auto-luminoso do M5 (`body.frag`), mais a luz direcional do Sol nos shaders de superfície |
| a rotação dos corpos | `PROCEDURAL` | `pxform_c` sobre os mesmos kernels, via `FlightSession::body_orientation()` |

## Três coisas que só apareceram quando as texturas chegaram

Com uma esfera de cor lisa nada disto era observável. É a textura que torna a
orientação verificável, e as três foram apanhadas por medição, não por gosto.

**A rotação estava transposta.** `get_body_orientation()` passava as LINHAS da
matriz do SPICE ao construtor de `Basis` do Godot, que recebe COLUNAS. O
resultado era a rotação inversa: a Terra girava ao contrário. Conferido contra
um facto de fora do código — às 00:00 UTC o ponto subsolar está perto de 180° E,
porque o meio-dia em Greenwich é às 12:00 — e a sonda do Milestone 7 respondia
`longitude 180,92 E, latitude −23,01`, com janeiro a dar os −23. Hoje a mesma
conferência é o teste
`the_body_orientation_puts_the_sub_solar_point_where_the_calendar_does`
(`tests/presentation/test_presentation_flight.cpp`, `presentation.flight`), que
exige `|longitude|` a menos de 3° de 180 e latitude −23 ± 1.

**O polo da malha não era o polo do corpo.** O polo de `SphereMesh` é o `+y`
local; o polo do corpo é o `+z` fixo ao corpo. Sem a conversão, as calotas
polares iam parar ao equador. A conversão está em `CelestialView::mesh_basis`
(`app/presentation/scene/celestial_view.cpp`), e o teste fecha a cadeia inteira.
No Milestone 7 o ponto subsolar caiu em `uv (0,003, 0,628)` — Pacífico, oceano —
e o antissolar em `uv (0,503, 0,372)`, que é o Saara; o teste de hoje amostra o
`earth_albedo.jpg` pela mesma cadeia e exige oceano no subsolar e terra no
antissolar.

**A costura de UV desenhava uma linha do polo ao polo.** Em `u = 0 ≡ 1` a
derivada de `u` salta de ~1e-3 para ~1, a GPU escolhe o mipmap mais grosseiro e
o resultado é uma linha visível — que na primeira captura do planeta inteiro
cortava o Pacífico exatamente na longitude ±180. Corrigida com `textureGrad` e
uma derivada desembrulhada (`sample_wrapped` no shader).

## Os substitutos

`app/presentation/scene/planet_textures.cpp` desenha ruído 3D (`stb_perlin`)
**amostrado na direção da esfera** — sem costura e sem beliscar os polos, que é a
razão de ser 3D e não uma imagem equirretangular ruidosa —, 512 × 256. Lê como
um planeta a distância e **não sobrevive a um zoom**: a 100 km de altitude é um
texel por 21 km.

Hoje ele só desenha **Marte** (`procedural:mars_albedo`), que é o único corpo
visitável sem mapa real. O gerador da Terra e da Lua do Milestone 7 — oceano,
terra, deserto e gelo por latitude e altura, com cerca de 30 % de terra — existia
no GDScript e **não foi portado** para o C++: com as texturas medidas no
repositório ele não era mais chamado. Se `earth_albedo.jpg` ou `moon_albedo.png`
faltarem, o corpo é desenhado com a cor de base de `CelestialView::colour_for`,
e o jogo continua (regra 81).

A imagem procedural é gerada quando a textura é pedida pela primeira vez, e fica
na memória; o cache em `user://` do GDScript deixou de existir.

## ⚠️ Uma imagem gerada nunca é física (regra 47)

A textura da Terra diz de que **cor** é a superfície. Onde a Terra está, quanto
ela girou, que tamanho tem e por que órbita anda vem de `core/` e de
`kernels/spice`, sempre. Se os dois discordarem, quem está errado é a imagem.

## Os prompts

| | destino |
|---|---|
| [earth-albedo](earth-albedo-codex-prompt.md) — **superado**, ver abaixo | `assets/textures/earth/earth_albedo.jpg` |
| [earth-clouds](earth-clouds-codex-prompt.md) — **superado**, ver abaixo | `assets/textures/earth/earth_clouds.jpg` |
| [earth-night-lights](earth-night-lights-codex-prompt.md) — **superado**, ver abaixo | `assets/textures/earth/earth_night.jpg` |
| [moon-albedo](moon-albedo-codex-prompt.md) | `assets/textures/moon/moon_albedo.png` |
| **`moon_normal` não vem de prompt**: `scripts/make_moon_normal.py`, a partir do LOLA | `assets/textures/moon/moon_normal.png` |

Quando o arquivo existir no caminho de destino, `Renderer::texture` carrega-o —
cada superfície pede uma lista de alternativas, como
`textures/mars/mars_albedo.png|procedural:mars_albedo`, e a primeira que existe
ganha — e o gerador procedural deixa de ser chamado, sem mudar nenhuma outra
linha, que é o ponto da regra 81.


## Os três prompts da Terra estão superados

As texturas da Terra deixaram de ser geradas e passam a vir medidas do arquivo
público da NASA (`scripts/fetch_earth_textures.sh`). Os prompts ficam como
registo do que foi pedido e do que o pedido produziu -- ver
[a secção no manifesto](../manifest.md#as-texturas-da-terra-sao-medidas).
