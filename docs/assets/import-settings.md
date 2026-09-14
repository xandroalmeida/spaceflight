# Import settings do Godot (regra 80)

Os `.import` **são versionados**, e isto é uma correção do que este arquivo dizia
antes.

O raciocínio anterior era que um arquivo gerado dentro do repositório é uma cópia
que envelhece em silêncio, e que a tabela abaixo bastava. Está errado por um
motivo simples: o `.import` é o **único** lugar de onde o Godot lê estes ajustes.
Sem ele, cada máquina importa com os padrões — e os padrões são `mipmaps
desligados` e `compressão nenhuma`, o que faz um planeta a 40 px cintilar e um
albedo de 2048×1024 ocupar 8 MB de VRAM. Não versionar não deixava a informação
onde ela é lida; deixava-a só aqui, onde nada a aplica.

Além disso, `detect_3d/compress_to` está posto a `0`. Com o padrão, o editor
**reescreve o `.import` sozinho** na primeira vez que a textura aparece numa
cena 3D — um arquivo versionado que muda sozinho no git, que é a pior das duas
coisas.

## O que está pinado, e por quê

| ajuste | valor | por quê |
|---|---|---|
| `compress/mode` | `2` (VRAM Compressed) | 2048×1024 sem compressão são 8 MB por mapa e há cinco |
| `compress/high_quality` | `true` nos albedos | BPTC em vez de S3TC: o S3TC faz bandas visíveis no gradiente do oceano |
| `mipmaps/generate` | `true` | sem mipmap, um planeta que ocupa 40 px cintila com o movimento orbital |
| `detect_3d/compress_to` | `0` | impede o editor de reescrever o próprio `.import` |

## ⚠️ sRGB não é um ajuste de importação no Godot 4

Este arquivo dizia `Flags/sRGB: Enable` para cor e `Disable` para máscaras. Esse
ajuste **não existe** no importador de texturas do Godot 4 — e o motivo é que a
decisão foi movida para onde ela é usada: quem decide se a amostra passa pela
curva sRGB é o **hint do uniforme no shader**.

```glsl
uniform sampler2D albedo_map : source_color, ...;   // cor    -> decodifica sRGB
uniform sampler2D night_map  : source_color, ...;   // cor    -> decodifica sRGB
uniform sampler2D cloud_map  : filter_linear_mipmap, repeat_enable;  // dado -> cru
```

A máscara de nuvens **não** leva `source_color`, e é por isso que os percentis
medidos em bytes (p25 38, p50 115, p75 191) se aplicam diretamente aos limiares
`cloud_low` e `cloud_high` do shader. Se ela levasse, os mesmos bytes chegariam
ao shader com a curva sRGB aplicada e os limiares estariam todos errados —
sutilmente, e sem nenhum erro.

## Normal maps

`compress/normal_map=1` faz o Godot usar RGTC, que guarda dois canais em vez de
três e **reconstrói** o terceiro por `b = √(1 − r² − g²)`. Está ligado em
`moon_normal.png`, e só é correto porque os vetores desse mapa têm comprimento 1
exatamente — que é uma das coisas que `scripts/validate_textures.py` mede. Num
mapa cujos vetores não fossem unitários, o RGTC devolveria um terceiro canal
errado e o erro apareceria como uma iluminação estranha sem causa visível.

`process/normal_map_invert_y` fica **desligado**: o Godot usa a convenção
OpenGL, com o verde a crescer para o topo da imagem, que é o norte nas texturas
equirretangulares deste projeto.

## Áudio

| ajuste | valor | por quê |
|---|---|---|
| `Force/8 Bit` | não | |
| `Force/Mono` | sim | tudo o que soa na cabine é mono; a posição do som não carrega informação aqui |
| `Loop Mode` | ver abaixo | |

⚠️ O modo de laço de `engine_loop.wav` e `ventilation.wav` é definido **em
código**, em `scripts/audio/audio_director.gd`, e não no `.import`. Pela mesma
razão de sempre: a propriedade fica onde ela é lida, e um `.import` versionado
seria um segundo lugar que pode discordar.

## Verificação depois de importar

1. abrir o simulador e olhar para a Terra em `COCKPIT`;
2. `F3` e confirmar que o quadro não passou de 16,7 ms;
3. `F8` até `1000x` e confirmar que a textura não borra — se borrar, faltam
   mipmaps ou a resolução é baixa demais para a escala exagerada.
