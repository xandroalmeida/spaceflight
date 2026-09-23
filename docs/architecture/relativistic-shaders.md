# Ótica relativística: onde cada conta acontece

Status: implementado (Milestone 5, metade visual)
Física: `docs/physics/relativistic-rendering.md`
Precisão e origem flutuante: `docs/architecture/rendering.md`
Última revisão: 2026-09-23 (apresentação própria, ADR-0009)

## 1. A pergunta desta camada

`relativistic-rendering.md` diz **o que** calcular. Este documento diz **onde**, e a
resposta tem uma restrição que não é de desempenho:

> ADR-0009 (antes ADR-0002): `core/` não conhece a apresentação. §39: a física vem antes do efeito visual.

Um shader que calcula aberração é o renderizador fazendo física. Uma conta de
física espalhada pela apresentação é pior ainda: é física fora do lugar onde ela
tem teste e revisão científica. A regra desta camada é portanto:

```
core/                calcula a física
app/session/         converte e entrega (StarSky, FlightSession)
app/shaders/         convertem número em pixel (star.*, body.*)
app/presentation/    liga um ao outro e não calcula nada
app/gfx/             sobe os buffers e emite os draws
```

Até o Milestone 8 esses papéis eram de `gdextension/`, `.gdshader` e
`.gd`; o que mudou foi o endereço, não a divisão.

## 2. A divisão, corpo a corpo

| O quê | Onde | Por quê ali |
|---|---|---|
| tempo de luz até o centro de um corpo | CPU, `core/relativity/light_time.hpp` | minutos de atraso, trajetória curva, precisa da efeméride |
| aberração da direção de cada estrela | CPU, `core/render/relativistic_sky.cpp` | 8 786 estrelas × ~60 flops = 0,5 Mflop/quadro. Mede-se; não é o gargalo |
| fator Doppler `D` de cada estrela | CPU, mesma passagem | sai de graça da mesma direção |
| cor a partir de `T' = D·T` | **GPU**, tabela gerada pela CPU | uma consulta a textura por vértice |
| brilho a partir de `D⁴` e de `η(DT)/η(T)` | **GPU** | idem, e é aqui que a curva de resposta age |
| tempo retardado **por vértice** | **GPU**, vertex shader | é por vértice por definição; não cabe em outro lugar |
| tudo o mais | `app/presentation`, `app/gfx` | posicionar corpos, subir buffers, ler teclas |

O critério não é "o que é rápido no GPU". É: **uma conta só migra para o shader
quando ela é por-fragmento ou por-vértice por natureza.** Aberração de estrela não
é — uma estrela é um ponto, e o ponto já foi calculado. Tempo retardado de vértice
é.

## 3. O contrato: o que atravessa a fronteira

### 3.1 Por quadro, por estrela

```
position  vec3   direção aberrada × raio do céu                 ← optics.hpp, na CPU
custom    vec4   (T_repouso, F_V_repouso, D_cor, D_brilho)       ← optics.hpp, na CPU
```

É o `StarVertex` de `app/session/star_sky.hpp`: sete `float` por estrela, subidos
como **dados de instância** de um quad alinhado à tela (`app/shaders/star.vert`;
`Renderer::upload_stars` em `app/gfx/renderer.cpp`). Os quatro canais de `custom`
são `float` de 32 bits; num formato normalizado de 8 bits uma temperatura de
25 944 K não caberia em `[0,1]`. `D_cor` e `D_brilho` são o mesmo `D`, separados
para que `Alt+D` e `Alt+B` desliguem Doppler e *beaming* de forma independente
(`core/render/relativistic_sky.hpp`).

O shader recebe `D` **pronto**. Ele não sabe o que é `β`, nem `γ`, nem qual é a
fórmula da aberração. Ele sabe uma coisa só, que é o significado de `D`:

```glsl
float T_shifted = T_rest * D;                          // seção 4: T' = D T
vec4  rest      = planck_sample(planck_table, T_rest, ref);
vec4  shifted   = planck_sample(planck_table, T_shifted, ref);
float band      = exp(shifted.a - rest.a);             // seção 10.1: eta(DT)/eta(T)
float L         = F_rest * pow(D_beam, 4.0) * band;
float R         = L / (L + half_saturation);           // seção 10.4: estrutural, não clamp
```

(resumo de `app/shaders/star.vert`; `planck_sample` está em `app/shaders/common.glsl`
e interpola a tabela com `texelFetch`, sem depender do filtro do sampler.)

Cinco linhas, e nenhuma delas é uma reimplementação: `T' = D·T` e `I' = D⁴I` são o
que `D` **significa**, não como ele foi obtido.

### 3.2 Uma vez, na carga: a LUT de Planck

`core/render/blackbody.hpp` gera uma tabela `1024 × 1` que o renderizador sobe como
textura `R32G32B32A32_FLOAT` (`Renderer::create_shared`):

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
BodyVertex.optics.x      D do centro do corpo (e .y, o D do brilho)   ← optics.hpp
BodyVertex.velocity_c.xyz   v do corpo relativa ao observador, em unidades
                            de cena por segundo
BodyVertex.velocity_c.w     c nas MESMAS unidades
```

(bloco uniforme de `app/shaders/body.vert`.) E a posição do corpo — a translação
da matriz `model` — já é a **aparente**: retardada por `light_time.hpp` e aberrada
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

`core/render/terrell.hpp` e o vertex shader (`app/shaders/body.vert`,
`retarded_light_time`) contêm ambos a quadrática de §11.1.
Não há como evitar: ela é por-vértice, e o core não emite GLSL.

O que se faz a respeito, em vez de fingir que não é duplicação:

* o GLSL traz o nome da função C++ que espelha, em comentário;
* o teste não checa o *código*, checa o **resultado**: `arcsin β` até 10⁻¹³
  (`tests/scientific/test_terrell_rotation.cpp`), que é uma propriedade que
  qualquer erro de transcrição quebra imediatamente;
* a forma fechada tem seis linhas e nenhum ramo, que é o tamanho em que uma
  transcrição é verificável a olho.

## 6. O que a apresentação faz

Tudo o que `app/presentation` e `app/gfx` tocam nesta camada:

```cpp
const auto stars = sky.vertices();               // StarSky: o core já calculou
// copia para um buffer de vértices e desenha 6 vértices × N instâncias
body.half_saturation = exposure;                 // celestial_view.cpp
```

Nenhuma multiplicação por `γ`, nenhum `sqrt(1 - b*b)`, nenhum `pow(D, 4)`. Se
alguma dessas aparecer em `app/presentation` ou `app/gfx`, a separação foi
perdida — e o sintoma será uma imagem que não bate com `optics.hpp`. A diferença
em relação ao GDScript é que agora essa camada é C++ tipado e tem testes
(`presentation.*`), mas eles verificam instrumentos e roteiros, não a ótica: a
ótica continua verificada no core.

## 7. Custo, medido

Por quadro, com o catálogo inteiro:

| Etapa | Custo |
|---|---|
| aberração + Doppler de 8 786 estrelas | ~0,5 Mflop |
| empacotar em `StarVertex` e subir por um transfer buffer | 8 786 × 28 B = 246 kB |
| desenhar o céu | 1 draw instanciado |

A alternativa — mandar `β` como uniforme e aberrar no vertex shader — economiza os
246 kB e custa a física migrar para o GLSL. A troca foi decidida a favor da
física, e este parágrafo existe para que a decisão seja revisitável com o número
na mão e não por impressão.

## 8. A seta, de novo

```
core ──────────────────► app/session ──────► app/shaders ─────► pixels
 optics.hpp              converte e          T'=DT, D⁴,        desenha
 light_time.hpp          empacota            resposta
 blackbody.hpp           double → float      nunca sabe o que é β
 star_catalog.hpp
```

Igual à de `rendering.md` §7, com um segmento a mais na ponta. E a mesma condição
de falha: se a seta apontar para trás — um shader decidindo uma direção, a
apresentação calculando um fator de Lorentz — o que se perde não é desempenho, é a propriedade
de que a imagem é uma consequência verificável do estado.
