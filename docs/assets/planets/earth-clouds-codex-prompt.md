# Earth clouds — prompt para o Codex

**Destino:** `godot/project/assets/textures/earth/earth_clouds.png`

O shader lê o canal **vermelho** como cobertura, de 0 (céu limpo) a 1 (nuvem
opaca), e compõe a nuvem por cima do albedo. Uma imagem em tons de cinza serve.

---

```
OBJECTIVE
Produce a seamless equirectangular greyscale cloud-cover map of planet Earth, to
be composited as a second layer over a separate surface albedo texture in a
real-time 3D space simulator. White is full cloud, black is clear sky. No
surface, no ocean, no land is visible in this image.

SUBJECT
A single plausible instantaneous snapshot of Earth's cloud cover, showing the
structures that are always there:
- a broken, bright, lumpy band along the equator (the intertropical convergence
  zone), strongest over the western Pacific, the Amazon and the Congo;
- two clear, dark, dry belts at roughly 20-35 degrees north and south, over the
  Sahara, Arabia, the Australian interior, and the eastern subtropical oceans;
- comma-shaped and spiral mid-latitude frontal systems at 40-65 degrees north and
  south, several per hemisphere, sheared east-north-east;
- a broad, even, bright cap of cloud over both polar regions;
- one or two tight tropical cyclone spirals at around 10-20 degrees latitude.

STYLE
Photographic satellite imagery, greyscale. Soft edges, wispy filaments,
realistic fractal structure. Not painterly, not stylised, not cartoon.

CAMERA / PROJECTION
Equirectangular, 2:1 aspect. Longitude 0 at the horizontal centre, latitude +90
at the top edge.

LIGHTING
None. This is a coverage mask, not a rendering. No shadows, no shading, no
terminator, no 3D relief on the cloud tops.

MATERIALS
Pure greyscale. Black (#000000) = no cloud. White (#FFFFFF) = opaque cloud.
Overall cloud fraction around 60 to 67 per cent of the globe by area, which is
Earth's actual value; do not fill the frame.

COLOR PALETTE
Greyscale only. No colour cast of any kind.

BACKGROUND
Black, meaning clear sky. Large clear areas are expected and correct.

RESOLUTION
4096 x 2048. If that is not available, 2048 x 1024.

ASPECT RATIO
Exactly 2:1.

TRANSPARENCY
None. Opaque greyscale; the engine derives opacity from brightness.

SEAMLESS REQUIREMENT
Left and right edges MUST tile seamlessly. Cloud systems crossing longitude ±180
must continue across the join. Top and bottom edges must be uniform across their
full width so the poles do not pinch.

THINGS TO AVOID
- any visible land, coastline, ocean or surface colour
- colour of any kind
- city lights, text, labels, graticule
- a visible vertical seam at the edges
- uniform noise or a flat grey wash: the structure is the whole point
- a globe, a circle, or a rendered sphere -- this is a flat map

OUTPUT FILE
earth_clouds.png  (PNG, 8 bits per channel, greyscale or RGB)

DESTINATION PATH
godot/project/assets/textures/earth/earth_clouds.png
```

---

## Pós-processamento

1. Confirme 2:1 e a costura nas bordas laterais.
2. Import settings: `docs/assets/import-settings.md`, seção "Máscaras e dados" —
   **sRGB desligado**. Uma máscara é uma fração, não uma cor.
3. Atualize o manifesto.
