# Ótica relativística: onde cada conta acontece

Status: implementado (Milestone 5, metade visual)
Física: `docs/physics/relativistic-rendering.md`
Precisão e origem flutuante: `docs/architecture/rendering.md`
Última revisão: 2026-09-13

## 1. A pergunta desta camada

`relativistic-rendering.md` diz **o que** calcular. Este documento diz **onde**, e a
resposta tem uma restrição que não é de desempenho:

> ADR-0002: `core/` não conhece Godot. §39: a física vem antes do efeito visual.

Um shader que calcula aberração é o renderizador fazendo física. Um `.gd` que
calcula qualquer coisa é pior ainda: é física numa linguagem sem teste, sem tipo e
sem revisão científica. A regra desta camada é portanto:

```
core/          calcula a física
gdextension/   converte e entrega
.gdshader      converte número em pixel
.gd            liga um ao outro e não calcula nada
```

## 2. A divisão, corpo a corpo

| O quê | Onde | Por quê ali |
|---|---|---|
| tempo de luz até o centro de um corpo | CPU, `core/relativity/light_time.hpp` | minutos de atraso, trajetória curva, precisa da efeméride |
| aberração da direção de cada estrela | CPU, `core/render/relativistic_sky.cpp` | 8 786 estrelas × ~60 flops = 0,5 Mflop/quadro. Mede-se; não é o gargalo |
| fator Doppler `D` de cada estrela | CPU, mesma passagem | sai de graça da mesma direção |
| cor a partir de `T' = D·T` | **GPU**, tabela gerada pela CPU | uma consulta a textura por vértice |
| brilho a partir de `D⁴` e de `η(DT)/η(T)` | **GPU** | idem, e é aqui que a curva de resposta age |
| tempo retardado **por vértice** | **GPU**, vertex shader | é por vértice por definição; não cabe em outro lugar |
| tudo o mais | GDScript | posicionar nós, montar arrays, ler teclas |

O critério não é "o que é rápido no GPU". É: **uma conta só migra para o shader
quando ela é por-fragmento ou por-vértice por natureza.** Aberração de estrela não
é — uma estrela é um ponto, e o ponto já foi calculado. Tempo retardado de vértice
é.

## 3. O contrato: o que atravessa a fronteira

### 3.1 Por quadro, por estrela

```
ARRAY_VERTEX   vec3   direção aberrada × raio do céu   ← optics.hpp, na CPU
ARRAY_CUSTOM0  vec4   (T_repouso, F_V_repouso, D, 0)   ← optics.hpp, na CPU
```

`CUSTOM0` usa `ARRAY_CUSTOM_RGBA_FLOAT`; sem esse formato o canal viraria 8 bits
por componente e uma temperatura de 25 944 K não caberia em `[0,1]`.

O shader recebe `D` **pronto**. Ele não sabe o que é `β`, nem `γ`, nem qual é a
fórmula da aberração. Ele sabe uma coisa só, que é o significado de `D`:

```glsl
float T_shifted = T_rest * D;                 // seção 4: T' = D T
vec4  rest      = planck_lut(T_rest);
vec4  shifted   = planck_lut(T_shifted);
float band      = exp(shifted.a - rest.a);    // seção 10.1: eta(DT)/eta(T)
float L         = F_rest * pow(D, 4.0) * band;
float R         = L / (L + half_saturation);  // seção 10.4: estrutural, não clamp
```

Cinco linhas, e nenhuma delas é uma reimplementação: `T' = D·T` e `I' = D⁴I` são o
que `D` **significa**, não como ele foi obtido.

### 3.2 Uma vez, na carga: a LUT de Planck

`core/render/blackbody.hpp` gera uma textura `1024 × 1` `RGBAF`:

```
índice   u = T/(T + 6000 K)        bijeção [0,∞) → [0,1), seção 9.2
RGB      cromaticidade sRGB linear, normalizada
A        ln η(T)                   seção 10.1, em log porque varre e^-17000 a e^-2
```

A tabela é **gerada pelo core**, não escrita à mão nem exportada de uma ferramenta.
Isso é o que mantém a colorimetria testável: o mesmo código que preenche a textura
é o que `tests/scientific/test_blackbody_colour.cpp` compara com o lugar planckiano
publicado.

Resolução, para o registro: 1024 amostras dão 8,7 K por texel a 1 297 K, 45 K a
5 800 K e 332 K a 25 944 K — sempre muito abaixo da escala em que a cromaticidade
se move.

### 3.3 Por quadro, por corpo

```
uniform float u_doppler          D do centro do corpo        ← optics.hpp
uniform vec3  u_relative_velocity   v do corpo relativa ao observador, em unidades
                                    de cena por segundo
uniform float u_light_speed         c nas MESMAS unidades
```

E a posição do nó já é a **aparente**: retardada por `light_time.hpp` e aberrada
por `optics.hpp`, na CPU.

## 4. Unidades dentro do shader

O ponto que mais facilmente vira bug: o vertex shader de §11.1 compara
`|p − vΔτ| = cΔτ`, e `p` está em **unidades de cena** (metros × `render_scale`,
`rendering.md` §3). Então `v` e `c` também precisam estar.

```
v_cena = v_m_por_s · render_scale
c_cena = c         · render_scale
```

A escala **cancela** em `Δτ`, que sai em segundos de verdade, e reaparece no
deslocamento `v_cena · Δτ`, que sai em unidades de cena. Como cancela, a razão
`|v|/c` é preservada exatamente, e a garantia estrutural `|v| < c` de §11.1
sobrevive à conversão para `float`. Essa é a razão de passar `c` como uniforme em
vez de usar a constante: um `c` em metros por segundo com um `p` em unidades de
cena daria `Δτ` errado por sete ordens de grandeza e o efeito simplesmente
sumiria — o modo de falha silencioso deste sistema.

## 5. A única fórmula que existe duas vezes

`core/render/terrell.hpp` e o vertex shader contêm ambos a quadrática de §11.1.
Não há como evitar: ela é por-vértice, e o core não emite GLSL.

O que se faz a respeito, em vez de fingir que não é duplicação:

* o GLSL traz o nome da função C++ que espelha, em comentário;
* o teste não checa o *código*, checa o **resultado**: `arcsin β` até 10⁻¹³
  (`tests/scientific/test_terrell_rotation.cpp`), que é uma propriedade que
  qualquer erro de transcrição quebra imediatamente;
* a forma fechada tem seis linhas e nenhum ramo, que é o tamanho em que uma
  transcrição é verificável a olho.

## 6. O que o GDScript faz

Tudo o que `main.gd` toca nesta camada:

```gdscript
var sky := simulation.get_sky_arrays()       # o core já calculou
mesh.add_surface_from_arrays(Mesh.PRIMITIVE_POINTS, sky["arrays"], [], {},
                             sky["format"])
material.set_shader_parameter("half_saturation", exposure)
```

Nenhuma multiplicação por `γ`, nenhum `sqrt(1 - b*b)`, nenhum `pow(D, 4)`. Se
alguma dessas aparecer num `.gd`, a separação foi perdida — e o sintoma será uma
imagem que não bate com `optics.hpp` sem que nenhum teste reclame, porque não há
teste de GDScript e não vai haver.

## 7. Custo, medido

Por quadro, com o catálogo inteiro:

| Etapa | Custo |
|---|---|
| aberração + Doppler de 8 786 estrelas | ~0,5 Mflop |
| empacotar em `PackedVector3Array` + `PackedFloat32Array` | 8 786 × 28 B = 246 kB |
| reconstruir a superfície do `ArrayMesh` | 1 chamada |

A alternativa — mandar `β` como uniforme e aberrar no vertex shader — economiza os
246 kB e custa a física migrar para o GLSL. A troca foi decidida a favor da
física, e este parágrafo existe para que a decisão seja revisitável com o número
na mão e não por impressão.

## 8. A seta, de novo

```
core ──────────────────► gdextension ──────► .gdshader ──────► pixels
 optics.hpp              converte e          T'=DT, D⁴,        desenha
 light_time.hpp          empacota            resposta
 blackbody.hpp           double → float      nunca sabe o que é β
 star_catalog.hpp
```

Igual à de `rendering.md` §7, com um segmento a mais na ponta. E a mesma condição
de falha: se a seta apontar para trás — um shader decidindo uma direção, um `.gd`
calculando um fator de Lorentz — o que se perde não é desempenho, é a propriedade
de que a imagem é uma consequência verificável do estado.
