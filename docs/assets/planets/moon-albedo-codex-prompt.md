# Moon albedo — prompt para o Codex

**Destino:** `godot/project/assets/textures/moon/moon_albedo.png`

---

```
OBJECTIVE
Produce a seamless equirectangular surface texture of Earth's Moon, to be wrapped
on a sphere in a real-time 3D space simulator. It will be viewed from lunar orbit
(about 100 km altitude) out to Earth distance, lit by a single directional light.
The texture must contain NO shading, NO shadows and NO terminator: the engine
supplies all lighting.

SUBJECT
The full lunar surface, near side and far side, geographically correct:
- the near side (centred on longitude 0) with its large dark basalt maria --
  Imbrium, Serenitatis, Tranquillitatis, Crisium, Procellarum, Nectaris,
  Humorum -- occupying roughly a third of that hemisphere;
- the far side almost entirely bright, heavily cratered highland, with the single
  dark patch of Mare Moscoviense and the dark floor of Tsiolkovskiy;
- the South Pole-Aitken basin as a large, subtly darker depression around the
  southern far side;
- craters at every scale, from basin-sized down to the resolution limit, with
  bright rims and darker floors;
- bright ray systems radiating from Tycho and Copernicus.

STYLE
Photographic, in the manner of an LROC or Clementine albedo mosaic. Greyscale
with the faint warm-to-cool variation the real surface has. No artistic
stylisation, no painterly texture, no colour grading.

CAMERA / PROJECTION
Equirectangular, 2:1 aspect. Longitude 0 (the centre of the near side, the face
that points at Earth) at the horizontal centre of the image. Latitude +90 at the
top edge.

LIGHTING
None. Albedo only: every pixel is the diffuse reflectance of that patch of
surface, as if photographed from directly overhead at local noon. No crater
shadows, no relief shading, no directional light, no terminator.

MATERIALS
Highland regolith: light grey, #8C8B87 to #A5A29C.
Mare basalt: dark grey, #3E3E42 to #4C4B4F.
Fresh crater rims and ejecta: brighter, up to #C8C6C0.
Crater floors: slightly darker than their surroundings.
Ray systems: brighter than the surrounding terrain, streaky, fading with
distance from the source crater.

COLOR PALETTE
Essentially neutral grey, with a very slight warm cast in the highlands and a
very slight cool cast in the maria -- the real difference, which is small. No
blue, no brown, no sepia.

BACKGROUND
There is no background. Every pixel is lunar surface.

RESOLUTION
4096 x 2048. If that is not available, 2048 x 1024.

ASPECT RATIO
Exactly 2:1.

TRANSPARENCY
None. Fully opaque, no alpha.

SEAMLESS REQUIREMENT
The left and right edges MUST tile seamlessly; they meet on the far side of the
Moon. Top and bottom edges must be uniform across their full width so the poles
do not pinch into a spike.

THINGS TO AVOID
- any shading, shadow or terminator
- a rendered sphere, a globe, or a photograph of the Moon in the sky
- stars, space, or a black surround
- text, labels, crater names, graticule lines, scale bars
- a visible seam at longitude ±180
- colour: this is not a "supermoon" photograph
- uniform noise standing in for craters

OUTPUT FILE
moon_albedo.png  (PNG, 8 bits per channel, RGB, sRGB colour space)

DESTINATION PATH
godot/project/assets/textures/moon/moon_albedo.png
```

---

## Pós-processamento

1. Confirme 2:1 e a costura lateral.
2. Confirme que o lado perto (centro da imagem) tem mares e o lado longe (bordas)
   quase não tem — é o que torna a Lua reconhecível de relance.
3. Import settings: `docs/assets/import-settings.md`, seção "Texturas de cor".
4. Atualize o manifesto para `INTEGRATED`.
