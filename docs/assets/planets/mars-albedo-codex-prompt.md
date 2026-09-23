# Mars albedo — prompt para o Codex

**Destino:** `assets/textures/mars/mars_albedo.png`

Enquanto o arquivo não existir, `CelestialView` usa o substituto procedural de
`PlanetTextures.mars_albedo()`. Ele acerta a cor, o contraste entre planícies
claras e regiões escuras, e as calotas — e não é um mapa de Marte. Nada bloqueia
o Milestone 8 à espera desta imagem (regra 90).

---

```
OBJECTIVE
Produce a seamless equirectangular surface texture of Mars, to be wrapped on a
sphere in a real-time 3D space simulator. It will be viewed from Mars orbit
(about 500 km altitude) out to interplanetary distance, lit by a single
directional light. The texture must contain NO shading, NO shadows and NO
terminator: the engine supplies all lighting.

SUBJECT
The full Martian surface, geographically correct:
- Syrtis Major Planum as the large dark triangular region near longitude 70 E,
  straddling the equator;
- the bright ochre plains of Arabia Terra, Tharsis, Elysium and Amazonis;
- Valles Marineris as a long dark east-west gash from about 90 W to 20 W, just
  south of the equator;
- the four Tharsis volcanoes -- Olympus Mons, Arsia, Pavonis and Ascraeus --
  visible as subtle albedo patterns and NOT as raised relief;
- Hellas Planitia as a large bright circular basin in the southern hemisphere
  near longitude 70 E;
- Acidalia Planitia and Utopia Planitia as darker northern lowlands;
- the north polar cap (bright, water ice, roughly above latitude 80 N) and the
  south polar cap (smaller, brighter, CO2, with its characteristic offset from
  the pole);
- the dichotomy between the smoother northern lowlands and the cratered southern
  highlands, visible as a change in texture and crater density.

STYLE
Photographic, in the manner of a Viking Orbiter or MRO/MOC global albedo mosaic.
No artistic stylisation, no painterly texture, no colour grading, no atmospheric
haze.

CAMERA / PROJECTION
Equirectangular, 2:1 aspect. Longitude 0 at the horizontal centre of the image,
longitude increasing east. Latitude +90 at the top edge, -90 at the bottom.

LIGHTING
None. Albedo only: every pixel is the diffuse reflectance of that patch of
surface, as if photographed from directly overhead at local noon. No crater
shadows, no relief shading, no directional light, no terminator, no limb
darkening.

MATERIALS
Bright dust-covered plains: #B87A4A to #C98B58.
Mid-tone terrain: #9A5F38 to #AC6E44.
Dark albedo regions (Syrtis Major, Acidalia, Mare Erythraeum): #54372A to
#6B4632.
Polar caps: #E8E4DE to #F5F2EE, with irregular, broken edges -- never a clean
band of latitude.
Crater rims slightly brighter, crater floors slightly darker, than their
surroundings.

COLOR PALETTE
Iron oxide: orange-brown through ochre to dark grey-brown. Emphatically NOT the
saturated red of popular illustration -- the real planet is closer to butterscotch
than to crimson. No blue, no green, no purple.

BACKGROUND
There is no background. Every pixel is Martian surface.

RESOLUTION
4096 x 2048. If that is not available, 2048 x 1024.

ASPECT RATIO
Exactly 2:1.

COLOR SPACE
sRGB, 8 bits per channel, no alpha.

SEAMLESS REQUIREMENT
The left and right edges must join without a visible seam: the pixel column at
longitude 180 W must continue into the column at longitude 180 E. The top and
bottom rows are the poles and must each be a single flat colour, because every
pixel in those rows is the same point on the sphere.

OUTPUT FILE
mars_albedo.png

DESTINATION PATH
assets/textures/mars/mars_albedo.png

WHAT TO AVOID
- No shadows, no shading, no terminator, no directional light of any kind.
- No stars, no sky, no space, no black border, no vignette.
- No clouds, no dust storm in progress, no atmospheric haze or limb glow: the
  shader draws the limb, and an atmosphere baked into the albedo would be drawn
  twice.
- No text, no labels, no graticule, no coordinate grid, no north arrow.
- No spacecraft, no rovers, no landing sites, no artificial structures.
- No liquid water, no vegetation, no terraformed Mars.
- No visible tiling, no repeated crater stamps, no mirror symmetry.
- No stretching or pinching at the poles beyond what the equirectangular
  projection itself implies.
- Not a globe, not a rendered sphere, not a photograph of a planet: a FLAT
  equirectangular map.
```

---

## O que NÃO pedir a um gerador de imagens

Um **normal map** de Marte, ou um mapa de altitude. A regra 25 do Milestone 8 é
explícita e o Milestone 7 pagou por aprendê-la: a primeira imagem de relevo
lunar entregue parecia um normal map e tinha os canais R e G a seguir o albedo
em vez da inclinação — era uma imagem bonita de uma grandeza física inventada.

Marte tem topografia **medida**, do MOLA (Mars Orbiter Laser Altimeter), em
grade global de 128 pixels por grau. Se o relevo vier a ser desejado, ele deve
ser **calculado** dessa grade, como `scripts/make_moon_normal.py` faz para o
LOLA — e não pintado.

Enquanto isso não existir, Marte é desenhado sem normal map, que é uma
aproximação visível e honesta.
