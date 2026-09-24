# Como as texturas e os sons são carregados (regra 80)

Até o Milestone 8 este arquivo descrevia os `.import` do Godot: compressão VRAM,
mipmaps, `compress/normal_map`, e por que eles tinham de ser versionados. Com o
[ADR-0009](../adr/0009-render-stack.md) não há mais importador nem `.import`: o
executável lê os arquivos de `assets/` tal como estão no repositório, e **todo
ajuste que antes vivia num `.import` vive agora no código que o usa**. A regra
continua a mesma — a propriedade fica onde ela é lida —, só que agora há um lugar
só onde ela pode ser lida.

## O caminho de uma textura

`Renderer::texture(spec, srgb)` em `app/gfx/renderer.cpp`:

1. `spec` é um caminho relativo ao diretório de assets, ou uma lista de
   alternativas separadas por `|`. A primeira que existe em disco ganha; uma
   alternativa `procedural:<nome>` pede a imagem a
   `app/presentation/scene/planet_textures.cpp` (regra 81). É assim que Marte
   pede `textures/mars/mars_albedo.png|procedural:mars_albedo`: quando o arquivo
   chegar, ele é usado sem mudar nenhuma outra linha;
2. o arquivo é decodificado por **stb_image**, sempre para RGBA de 8 bits por
   canal, seja PNG ou JPEG;
3. a textura é criada **sem compressão** (`R8G8B8A8`), com a cadeia de mipmaps
   inteira, e os mipmaps são **gerados na GPU** depois do upload
   (`SDL_GenerateMipmapsForGPUTexture`, em `app/gfx/gpu.cpp`);
4. o resultado fica num cache por `spec` **e** por espaço de cor: a mesma imagem
   pedida uma vez como cor e outra como dado seria duas texturas, e não uma
   amostrada errado.

Um arquivo que falta, ou que o stb_image não lê, não derruba nada: o log diz
qual, a textura fica vazia e o shader desenha sem ela: a cor de base do corpo ou
do material no lugar do albedo, a normal geométrica no lugar do relevo.

## sRGB é decidido por quem pede a textura

No Godot 4 isso era o hint `source_color` do uniforme no shader. Aqui é o segundo
argumento de `Renderer::texture`, e ele escolhe o **formato** da textura:

| mapa | formato | quem pede |
|---|---|---|
| albedo (Terra, Lua, Marte), luzes noturnas, `hull_panels`, `panel_surface` | `R8G8B8A8_UNORM_SRGB` — o amostrador decodifica para linear | `renderer.cpp`, passada do mundo e passada próxima |
| nuvens da Terra | `R8G8B8A8_UNORM` — cru | `texture(body.surface.cloud_map, false)` |
| normal map da Lua | `R8G8B8A8_UNORM` — cru | `texture(body.surface.normal_map, false)` |

O comentário no topo de `app/shaders/body.frag` diz o mesmo: mapas codificados
em sRGB são texturas sRGB, a cobertura de nuvens e o normal map são **dados**.

A máscara de nuvens tem de chegar crua, e é por isso que os limiares medidos em
bytes se aplicam diretamente ao shader: `renderer.cpp` passa `0,16..0,60`, que é
41..153 em bytes, e `body.frag` faz `smoothstep` entre eles sobre o valor amostrado.
Se a textura fosse sRGB, os mesmos bytes chegariam ao shader já pela curva sRGB e
os limiares estariam todos errados — sutilmente, e sem nenhum erro.

## Mipmaps e filtragem

Os mipmaps continuam obrigatórios pelo mesmo motivo de antes: sem eles, um
planeta que ocupa 40 px cintila com o movimento orbital. Agora não há ajuste que
possa esquecê-los — toda textura carregada por `Renderer::texture` nasce com a
cadeia inteira.

O amostrador das superfícies (`repeat_sampler_`) é trilinear, com repetição e
anisotropia 8×. A costura de longitude não depende dele: `body.frag` amostra por
`sample_wrapped`, com `textureGrad` e a derivada de `u` desembrulhada, para que a
GPU não escolha o mipmap mais grosseiro em `u = 0 ≡ 1`.

⚠️ **Não há compressão de textura.** No Godot os albedos iam para a GPU em BPTC
(o S3TC fazia bandas no gradiente do oceano); aqui vão em RGBA8 sem perda, o que
custa mais memória de vídeo — o albedo da Terra, 8192×4096, são 128 MB só no
nível zero e um terço a mais com os mipmaps — e não tem banda nenhuma. Se a
memória um dia apertar, o lugar da decisão é `Renderer::texture`.

## Normal maps

O RGTC do Godot guardava dois canais e reconstruía o terceiro; aqui o
`moon_normal.png` sobe com os três canais e `body.frag` lê `rgb · 2 − 1`
diretamente. A exigência de que os vetores tenham comprimento 1 continua a valer
— é uma das coisas que `scripts/validate_textures.py` mede — porque o shader
normaliza o resultado mas não corrige a direção de um vetor que já veio errado.

A convenção é a mesma do Godot (OpenGL): o verde cresce para o topo da imagem,
que é o norte nas texturas equirretangulares deste projeto. A esfera de
`app/presentation/scene/mesh.cpp` põe a tangente ao longo de `+u`, e
`body.frag` constrói a bitangente como `cross(normal, tangente)`, que no equador
aponta para `+y` da malha — o polo norte. Não há inversão do verde a fazer.

## Áudio

Os WAV são lidos por `SDL_LoadWAV` (`app/platform/sdl_audio.cpp`) e convertidos
pelo `SDL_AudioStream` para o formato do dispositivo. Não há passo de
importação, e portanto nada de "forçar mono" ou "8 bits": o arquivo é o que
soa.

O laço é definido **em código**: `engine_impulse`, `engine_cruise`, `rcs_hiss`,
`ventilation` e `equipment` são os que `AudioDirector` toca por `set_loop`
(`app/presentation/audio_director.cpp`), e `SdlAudio` re-enfileira o clip antes
que ele acabe. Os outros são disparos. Um arquivo de laço tem de ter o fim
vizinho do começo — é o que `make_loopable()` garante.

## Verificação depois de trocar uma textura

1. `python3 scripts/validate_textures.py` (ou `cmake --build build --target
   asset-validation`);
2. abrir o simulador e olhar para a Terra em `COCKPIT`;
3. `F3` e confirmar que o quadro não passou de 16,7 ms;
4. `.` até `1000x` e confirmar que a textura não borra — se borrar, a
   resolução é baixa demais para a escala exagerada.
