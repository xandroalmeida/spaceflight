# Planetas

Para o M7 importam três: **Terra, Lua, Sol** (regra 30). Os outros corpos do
catálogo continuam desenhados com a cor de base e ninguém parou o milestone por
eles.

## O que é procedural e o que tem de ser gerado

| | estado | onde |
|---|---|---|
| a superfície da Terra e da Lua | `INTEGRATED` | as texturas entregues estão em `assets/textures/`; `planet_textures.gd` continua como substituto quando elas faltam (regra 81) |
| o limbo atmosférico | `PROCEDURAL` | `relativistic_body.gdshader`, um termo de rim scattering |
| o lado noturno | textura + shader | a emissão vem do mapa de luzes; o desvanecimento no terminador é do shader |
| o disco e o brilho do Sol | `PROCEDURAL` | o shader auto-luminoso do M5, mais uma `DirectionalLight3D` |
| a rotação dos corpos | `PROCEDURAL` | `pxform_c` sobre os mesmos kernels, via `get_body_orientation()` |

## Três coisas que só apareceram quando as texturas chegaram

Com uma esfera de cor lisa nada disto era observável. É a textura que torna a
orientação verificável, e as três foram apanhadas por medição, não por gosto.

**A rotação estava transposta.** `get_body_orientation()` passava as LINHAS da
matriz do SPICE ao construtor de `Basis` do Godot, que recebe COLUNAS. O
resultado era a rotação inversa: a Terra girava ao contrário. Conferido contra
um facto de fora do código — às 00:00 UTC o ponto subsolar está perto de 180° E,
porque o meio-dia em Greenwich é às 12:00 — e agora
`tests/probe_orientation.gd` responde `longitude 180,92 E, latitude −23,01`, com
janeiro a dar os −23.

**O polo da malha não era o polo do corpo.** O polo de `SphereMesh` é o `+y`
local; o polo do corpo é o `+z` fixo ao corpo. Sem a conversão, as calotas
polares iam parar ao equador. A conversão está em `CelestialView._mesh_basis`,
com a tabela de correspondências escrita por extenso, e o teste fecha a cadeia
inteira: o ponto subsolar cai em `uv (0,003, 0,628)` — Pacífico, oceano — e o
antissolar em `uv (0,503, 0,372)`, que é o Saara.

**A costura de UV desenhava uma linha do polo ao polo.** Em `u = 0 ≡ 1` a
derivada de `u` salta de ~1e-3 para ~1, a GPU escolhe o mipmap mais grosseiro e
o resultado é uma linha visível — que na primeira captura do planeta inteiro
cortava o Pacífico exatamente na longitude ±180. Corrigida com `textureGrad` e
uma derivada desembrulhada (`sample_wrapped` no shader).

## Os substitutos

`PlanetTextures` desenha ruído 3D **amostrado na direção da esfera** — sem
costura e sem beliscar os polos, que é a razão de ser 3D e não uma imagem
equirretangular ruidosa — e fatia-o em oceano, terra, deserto e gelo por latitude
e altura. O degrau terra/mar dá cerca de 30 % de terra, que é a fração da Terra,
e é o único número nesse arquivo que tem um alvo.

O resultado lê como um planeta a 400 km e **não sobrevive a um zoom**: 512 × 256
a 100 km de altitude é um texel por 21 km. Com as texturas entregues isso deixou
de importar para a Terra e para a Lua; o gerador continua a existir para o caso
de os arquivos não estarem lá, que é a regra 81.

As imagens são desenhadas uma vez e guardadas em `user://`, porque meio milhão de
pixels em GDScript custa cerca de um segundo e a regra 66 dá "alguns segundos"
para o jogo inteiro aparecer.

## ⚠️ Uma imagem gerada nunca é física (regra 47)

A textura da Terra diz de que **cor** é a superfície. Onde a Terra está, quanto
ela girou, que tamanho tem e por que órbita anda vem de `core/` e de
`kernels/spice`, sempre. Se os dois discordarem, quem está errado é a imagem.

## Os prompts

| | destino |
|---|---|
| [earth-albedo](earth-albedo-codex-prompt.md) | `assets/textures/earth/earth_albedo.png` |
| [earth-clouds](earth-clouds-codex-prompt.md) | `assets/textures/earth/earth_clouds.png` |
| [earth-night-lights](earth-night-lights-codex-prompt.md) | `assets/textures/earth/earth_night.png` |
| [moon-albedo](moon-albedo-codex-prompt.md) | `assets/textures/moon/moon_albedo.png` |
| **`moon_normal` não vem de prompt**: `scripts/make_moon_normal.py`, a partir do LOLA | `assets/textures/moon/moon_normal.png` |

Quando o arquivo existir no caminho de destino, `CelestialView._apply_surface`
carrega-o e o gerador procedural deixa de ser chamado — sem mudar nenhuma outra
linha, que é o ponto da regra 81.
