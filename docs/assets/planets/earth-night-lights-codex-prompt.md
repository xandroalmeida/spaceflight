# Earth night lights — prompt para o Codex

**Destino:** `godot/project/assets/textures/earth/earth_night.png`

O shader soma esta imagem como EMISSÃO no lado escuro, desvanecendo através do
terminador. Ela não substitui o albedo: os dois coexistem.

---

```
OBJECTIVE
Produce a seamless equirectangular map of Earth's artificial night-time lighting,
to be added as an emissive layer over a separate daytime albedo texture in a
real-time 3D space simulator. Only the lights are in this image. The land and the
ocean are pure black.

SUBJECT
City lights as seen from orbit at night, geographically correct for present-day
Earth:
- dense, near-continuous lighting along the eastern seaboard of North America,
  western Europe, the Nile valley and delta, the Indian subcontinent, eastern
  China, Japan and Korea, Java;
- bright coastal strings around the Mediterranean, the Gulf, southern Brazil and
  Argentina, southeast Australia;
- isolated bright points in the interiors of continents, thinning to nothing over
  the Sahara, the Amazon, central Australia, Siberia and the Tibetan plateau;
- North Korea dark beside a brilliant South Korea;
- gas flares in the Niger delta, the Persian Gulf and western Siberia;
- nothing at all over the oceans, Antarctica, Greenland's interior.

STYLE
Photographic, in the manner of the NASA "Black Marble" composite. Points and
filaments, not blobs. Soft glow around the brightest clusters.

CAMERA / PROJECTION
Equirectangular, 2:1 aspect. Longitude 0 at the horizontal centre, latitude +90
at the top edge. Must register exactly with the daytime albedo map: a light must
not fall in the sea.

LIGHTING
None of its own. This IS the light.

MATERIALS
Sodium and LED street lighting: warm amber-white, around #FFB45C to #FFE0B0.
Gas flares: more saturated orange, around #FF8A3C.
Everything else: pure black, #000000.

COLOR PALETTE
Amber through warm white, on black. No blue-white, no green, no purple.

BACKGROUND
Pure black over every unlit area, which is most of the image.

RESOLUTION
4096 x 2048. If that is not available, 2048 x 1024.

ASPECT RATIO
Exactly 2:1.

TRANSPARENCY
None. Opaque RGB on black; the engine adds it, so black adds nothing.

SEAMLESS REQUIREMENT
Left and right edges must tile seamlessly. Top and bottom edges must be black
across their full width.

THINGS TO AVOID
- any daytime surface, coastline outline, ocean colour or landmass fill
- aurorae, moonlight, airglow, city names, borders, labels
- lights over water or over Antarctica
- a uniform scatter of dots: the geography is the whole point
- a rendered globe -- this is a flat map

OUTPUT FILE
earth_night.png  (PNG, 8 bits per channel, RGB, sRGB colour space)

DESTINATION PATH
godot/project/assets/textures/earth/earth_night.png
```

---

## Pós-processamento

1. Sobreponha ao `earth_albedo.png` num editor e confira que nenhuma luz cai no
   mar. Se cair, a imagem está desalinhada em longitude.
2. Import settings: seção "Texturas de cor" (**sRGB ligado** — isto é cor).
3. Atualize o manifesto.
