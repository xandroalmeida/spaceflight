# Earth albedo — prompt para o Codex

Copie o bloco abaixo inteiro. O que ele produz substitui a textura procedural de
`godot/project/scripts/world/planet_textures.gd`.

**Destino:** `godot/project/assets/textures/earth/earth_albedo.png`

⚠️ A imagem é APARÊNCIA e nunca física (regra 47). Posição, rotação, escala e
órbita da Terra continuam a vir de `core/` e de `kernels/spice`. A textura só
diz de que cor é a superfície.

---

```
OBJECTIVE
Produce a seamless equirectangular (plate carrée) daytime surface texture of
planet Earth, to be wrapped on a sphere in a real-time 3D space simulator. It
will be viewed from a 400 km orbit down to lunar distance, lit by a single
directional light, with a separate cloud layer and a separate night-lights layer
composited on top by the engine. This texture must contain NO clouds, NO city
lights, NO atmosphere, NO terminator and NO shading of any kind.

SUBJECT
The full surface of Earth: oceans, continents, continental shelves, deserts,
forests, tundra, ice caps. Geographically correct coastlines and landmass shapes
for present-day Earth. Longitude 0 (Greenwich) at the horizontal centre of the
image, longitude -180 at the left edge and +180 at the right edge. Latitude +90
(North Pole) at the top edge, -90 (South Pole) at the bottom edge.

STYLE
Photographic satellite imagery, in the manner of a NASA Blue Marble composite.
Natural colour. No artistic stylisation, no painterly texture, no vignetting, no
colour grading, no film grain. Flat, evenly exposed, as if every pixel were
photographed from directly overhead at local noon.

CAMERA / PROJECTION
Equirectangular, 2:1 aspect. Uniform scale in longitude; the usual polar
stretching of this projection is expected and correct.

LIGHTING
None. This is an albedo map: every pixel is the diffuse reflectance of that patch
of surface. No shadows, no specular highlights, no directional light, no
terminator, no limb darkening. The engine supplies all lighting.

MATERIALS
Deep ocean: dark desaturated blue, roughly #0A1B33 to #0E2B4A.
Continental shelf and shallow seas: lighter blue-green, roughly #12455F.
Tropical forest: dark desaturated green, roughly #14301B.
Temperate vegetation: mid green, roughly #26471F.
Grassland and steppe: olive to khaki, roughly #5A5A33.
Desert (Sahara, Arabian, Gobi, Australian interior): warm sand, #9A7C4A to #C2A06A.
Tundra and boreal: grey-green to brown, roughly #4A4A3A.
Permanent ice (Antarctica, Greenland): near white, #E4E8EC, with subtle blue-grey
crevasse detail.
Seasonal snow: none. This is a single canonical state, not a season.

COLOR PALETTE
Natural Earth colours only. Saturation restrained: this is a science simulator,
not a travel poster. No neon, no teal-and-orange grade.

BACKGROUND
There is no background. Every pixel is planet surface.

RESOLUTION
4096 x 2048. If that is not available, 2048 x 1024.

ASPECT RATIO
Exactly 2:1.

TRANSPARENCY
None. Fully opaque RGB, no alpha channel.

SEAMLESS REQUIREMENT
The left and right edges MUST tile seamlessly: the pixel column at x=0 must
continue the pixel column at x=width-1 without a visible join, because the two
meet on the far side of the globe. The top and bottom edges do not tile; they
converge to the poles and must be a uniform ice/ocean colour across their full
width, with no feature that would pinch into a visible spike at the pole.

THINGS TO AVOID
- clouds of any kind
- city lights, roads, borders, labels, text, graticule lines, compass roses
- any shading, shadow, terminator, day/night boundary, or limb
- atmospheric haze or blue glow
- a visible seam at longitude ±180
- a swirl or pinch artefact at either pole
- stars, space, or a black surround
- watermark or signature

OUTPUT FILE
earth_albedo.png  (PNG, 8 bits per channel, RGB, sRGB colour space)

DESTINATION PATH
godot/project/assets/textures/earth/earth_albedo.png
```

---

## Pós-processamento

1. Confirme que a imagem é exatamente 2:1.
2. Verifique a costura: junte a faixa dos 32 px da esquerda com a dos 32 px da
   direita e olhe a emenda. Se houver uma linha, espelhe-corrija ou peça de novo.
3. Salve como PNG 8 bits sem alfa.
4. Ponha no caminho de destino e abra o projeto — `CelestialView._apply_surface`
   já procura o arquivo lá e usa a procedural só se ele não existir.
5. Import settings: `docs/assets/import-settings.md`, seção "Texturas de cor".
6. Atualize o estado para `INTEGRATED` em `docs/assets/manifest.md`.
