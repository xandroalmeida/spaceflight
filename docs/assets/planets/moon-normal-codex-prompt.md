# Moon normal map — NÃO use um gerador de imagens

**Estado: o asset está `INTEGRATED`, e não foi gerado a partir deste prompt.**

```bash
./scripts/fetch_lunar_dem.sh          # LOLA LDEM_16, 33 MB
python3 scripts/make_moon_normal.py   # 2,6 s
python3 scripts/validate_textures.py
```

O prompt abaixo foi usado uma vez, e o resultado foi **recusado por medição**:
nos mares — as superfícies mais lisas da Lua, onde um normal map correto vale
`(128, 128, 255)` — ele lia `(50, 54, 247)`, com vetores de 0,68 a 1,29 de
comprimento e canais R e G a seguir o **albedo** em vez da inclinação.

A causa não é o prompt. **Uma normal é uma quantidade calculada a partir de um
campo de altura**, e um gerador de imagens não tem de onde tirar a altura: ele
pinta o que um normal map parece. Insistir com um prompt melhor ataca o sintoma.

O prompt fica registrado abaixo pelo que ele ensina — e porque a mesma armadilha
vale para qualquer outro mapa que codifique uma grandeza em vez de uma cor.

---

```
OBJECTIVE
Produce a seamless equirectangular TANGENT-SPACE NORMAL MAP of Earth's Moon,
registered pixel-for-pixel with the lunar albedo texture, for a real-time 3D
space simulator.

SUBJECT
The surface relief of the Moon: crater rims raised and floors sunken, basin walls,
mountain ranges (the Apennines, the Caucasus), wrinkle ridges across the maria,
the rille systems. Relief consistent with the albedo map: every crater in the
albedo has a crater here, in the same place, at the same size.

STYLE
A technical normal map, not an artistic rendering.

CAMERA / PROJECTION
Equirectangular, 2:1 aspect, identical framing to the albedo map.

LIGHTING
None. A normal map encodes direction, not light. There must be no shading, no
shadow and no highlight anywhere in the image.

MATERIALS
Tangent space, OpenGL convention (+Y up / green channel points up).
Flat surface = RGB (128, 128, 255). Red encodes the X slope, green the Y slope,
blue is near 255 everywhere. The image is therefore overwhelmingly light blue
-- if it looks grey, brown or like a photograph, it is a height map or a
bump map and not what is wanted.

COLOR PALETTE
The normal-map palette and nothing else: lavender-blue base, with red and green
deviations at slopes.

BACKGROUND
There is no background.

RESOLUTION
4096 x 2048, matching the albedo exactly. If not available, 2048 x 1024.

ASPECT RATIO
Exactly 2:1.

TRANSPARENCY
None.

SEAMLESS REQUIREMENT
Left and right edges must tile seamlessly, and the normals must be continuous
across the join: a discontinuity here shows as a bright line of wrong lighting.

THINGS TO AVOID
- a greyscale height map (that is a different thing)
- a shaded or lit rendering of the surface
- DirectX convention (green pointing down)
- any colour that is not in the normal-map palette
- text, labels, borders

OUTPUT FILE
moon_normal.png  (PNG, 8 bits per channel, RGB, LINEAR colour space -- NOT sRGB)

DESTINATION PATH
assets/textures/moon/moon_normal.png
```

---

## Como conferir, se alguém tentar de novo

A imagem entregue na primeira vez parecia um normal map e não era: azul-lavanda,
relevo nos canais R e G, registrada com o albedo — e, medida, tinha vetores de
comprimento 0,68 a 1,29 e canais R/G correlacionados com o **albedo** em vez da
inclinação. Era uma imagem de relevo tingida.

Antes de aceitar a próxima, **meça um mar**. Mare Imbrium, Oceanus Procellarum e
Mare Serenitatis são as superfícies mais lisas da Lua, e num normal map correto
elas leem:

```
R ≈ 128     G ≈ 128     B ≈ 255     |n| = 1,000
```

Se R e G estiverem longe de 128 sobre um mar, a imagem não codifica normais.
Se `|n|` variar com a região, também não.

Não é preciso medir à mão:

```bash
python3 scripts/validate_textures.py
```

Ele conhece os três mares e imprime a medição. Quando o arquivo passar, tire
`moon/moon_normal.png` da tabela `REJECTED` no topo do script e mude o estado no
manifesto — o script avisa se um asset recusado passar em tudo, exatamente para
que o manifesto não fique a mentir.

## Pós-processamento

⚠️ **Importar com sRGB DESLIGADO e "Normal Map" LIGADO.** Um normal map importado
como cor tem a curva sRGB aplicada às suas componentes, e o relevo sai errado de
uma forma que parece só "estranha" em vez de errada. Ver
`docs/assets/import-settings.md`.

O shader ainda não amostra este mapa; ligar requer acrescentar `NORMAL_MAP` ao
`relativistic_body.gdshader`. Está no backlog.
