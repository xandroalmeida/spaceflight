# Hull panels — prompt para o Codex

**Destino:** `assets/textures/spacecraft/hull_panels.png`

Textura **ladrilhável** para o casco. Sem ela a nave usa `HullPaint` liso, que já
lê bem à distância mas não tem escala de perto.

---

```
OBJECTIVE
Produce a seamless, tileable texture of a spacecraft hull surface, for a
real-time 3D space simulator. It will be tiled several times across cylindrical
modules 3 metres in diameter and viewed from between 5 and 80 metres. It must
read as manufactured hardware at that distance and must not contain any lighting.

SUBJECT
The outer skin of a modern crewed spacecraft: flat panels with visible seams,
rows of flush fasteners along the seams, occasional raised strips and cable
runs, small access covers with recessed handles, faint scuffing and handling
marks, a few micrometeoroid pits. Panel size roughly 40 by 60 centimetres at a
tiling scale of 2 metres across the image.

STYLE
Photographic close-up of real aerospace hardware, in the manner of a Soyuz,
Orion or Cygnus service module skin. Restrained: this is manufactured, not
weathered or battle-damaged.

CAMERA / PROJECTION
Orthographic, straight down onto the surface. No perspective, no vanishing
point, no visible curvature.

LIGHTING
None. Completely flat, even illumination with no shadow, no highlight, no
directional light, no ambient occlusion baked in. Every seam and fastener must
be readable by its COLOUR difference alone, because the engine supplies all
shading.

MATERIALS
Base: off-white thermal paint, around #C6C7C4, slightly uneven.
Seams: a shade darker, #9A9B98, a millimetre or two wide at scale.
Fasteners: neutral metal, #ADAFB2, small and evenly spaced.
Scuffs and handling marks: barely darker than the base, irregular, sparse.
Micrometeoroid pits: tiny, dark, very few.

COLOR PALETTE
Near-neutral greys and off-whites. No colour cast, no rust, no blue tint.

BACKGROUND
There is no background: the surface fills the frame.

RESOLUTION
1024 x 1024.

ASPECT RATIO
Exactly 1:1.

TRANSPARENCY
None.

SEAMLESS REQUIREMENT
MUST tile seamlessly in BOTH directions: the top edge continues the bottom edge
and the left edge continues the right edge, with panel seams and fastener rows
carrying across the joins. A tiling artefact repeated eight times around a
cylinder is immediately visible.

THINGS TO AVOID
- any lighting, shadow, highlight or ambient occlusion
- perspective or surface curvature
- rust, corrosion, burn marks, battle damage, grime
- text, logos, flags, serial numbers, warning labels
- rivets in decorative patterns; fasteners follow seams
- a visible tiling seam

OUTPUT FILE
hull_panels.png  (PNG, 8 bits per channel, RGB, sRGB colour space)

DESTINATION PATH
assets/textures/spacecraft/hull_panels.png
```

---

## Pós-processamento

1. Ladrilhe 4 × 4 num editor e procure a emenda. Se aparecer, refaça.
2. Ligar exige acrescentar `albedo_texture` e `uv1_scale` a `HullPaint` em
   `app/presentation/scene/ship_materials.cpp`. As malhas de `app/presentation/scene/mesh.cpp` já trazem UV.
3. Import settings: seção "Texturas de cor", com `Repeat: Enabled`.
