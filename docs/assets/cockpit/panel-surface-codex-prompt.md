# Cockpit panel surface — prompt para o Codex

**Destino:** `assets/textures/cockpit/panel_surface.png`

Superfície do painel, vista a menos de um metro. É a textura que mais se olha no
jogo inteiro.

⚠️ **Sem texto embutido** (regra 44). Os rótulos — `ENGINE`, `RCS`, `NAV` — são
desenhados pelo Godot como `Label3D`, para ficarem nítidos em qualquer resolução
e poderem ser traduzidos.

---

```
OBJECTIVE
Produce a seamless, tileable texture of a spacecraft cockpit panel surface, for a
real-time 3D space simulator. It will be tiled across an instrument panel about
1.9 metres wide and viewed from 0.8 to 1.2 metres -- close enough that the
micro-texture matters. It must contain NO text and NO lighting.

SUBJECT
The face of a modern crewed-spacecraft instrument panel: a matte, fine-grained
composite or powder-coated surface, with subtle machined edges, a faint linear
brushing, occasional flush screw heads, and very slight variation in tone across
the surface. No switches, no dials, no screens: those are separate geometry.

STYLE
Photographic close-up of real aerospace or industrial control-panel hardware.
Clean, matte, functional. Modern spacecraft or submarine console, not retro
aircraft, not sci-fi.

CAMERA / PROJECTION
Orthographic, straight down onto the surface. No perspective.

LIGHTING
None. Flat, even, shadowless. No highlight, no reflection, no ambient occlusion.

MATERIALS
Base: dark neutral graphite, around #191B1E, with a fine grain.
Machined edges and bevels: a shade lighter, #2A2D31.
Screw heads: neutral metal, #4A4D52, flush and sparse.
Overall: very low contrast. The panel must not compete with the displays mounted
on it.

COLOR PALETTE
Neutral dark greys only. No blue, no green, no warm cast, no colour of any kind.

BACKGROUND
There is no background: the surface fills the frame.

RESOLUTION
1024 x 1024.

ASPECT RATIO
Exactly 1:1.

TRANSPARENCY
None.

SEAMLESS REQUIREMENT
MUST tile seamlessly in both directions.

THINGS TO AVOID
- TEXT OF ANY KIND: no labels, no legends, no numbers, no letters, no symbols
- switches, buttons, knobs, dials, screens, indicator lights
- any lighting, shadow, highlight or reflection
- scratches, wear, grime, coffee stains, retro patina
- rivets, decorative hex patterns, carbon-fibre weave, brushed-aluminium sheen
- a visible tiling seam

OUTPUT FILE
panel_surface.png  (PNG, 8 bits per channel, RGB, sRGB colour space)

DESTINATION PATH
assets/textures/cockpit/panel_surface.png
```

---

## Pós-processamento

1. Ladrilhe 3 × 2 e confira a emenda.
2. Confirme que **não há um único caractere** na imagem. Se houver, refaça: um
   rótulo errado embutido numa textura é pior do que nenhum rótulo.
3. Ligar exige acrescentar `albedo_texture` a `CockpitPanel` em
   `scripts/world/materials.gd`.
