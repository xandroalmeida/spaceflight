# Renderização Relativística

Status: **implementado** (Milestone 5, núcleo e parte visual).
Pré-requisitos: `docs/architecture/rendering.md` §6, `docs/physics/relativity-roadmap.md` §6
Onde cada conta acontece: `docs/architecture/relativistic-shaders.md`
Última revisão: 2026-09-13

§38 do enunciado exige este documento antes do código, e proíbe uma coisa
específica:

> Não representar contração de Lorentz simplesmente escalando meshes.

A razão está em §6 e é mais interessante que a proibição.

---

## 1. O que a imagem ainda mente

`docs/architecture/rendering.md` §6 registrou a dívida desde o Milestone 2: tudo
o que a cena desenha hoje é **geométrico**. Os corpos aparecem onde *estão* no
instante `t`, com a cor que têm, com o brilho que têm. Nada disso é o que um
olho a bordo receberia.

Cinco efeitos separam o que **é** do que se **vê**, e eles se compõem nesta ordem:

```
posição verdadeira em t
      │  1. tempo de trânsito da luz     → posição aparente (retardada)
      ▼
direção de chegada no referencial de coordenadas
      │  2. aberração                    → direção no referencial da nave
      ▼
frequência e intensidade recebidas
      │  3. Doppler  4. beaming          → cor e brilho
      ▼
forma do objeto
      │  5. rotação de Terrell           → NÃO contração
      ▼
imagem
```

---

## 2. Tempo de trânsito e posição aparente

Um observador em `x_obs(t)` vê o corpo onde ele estava no **instante retardado**
`t_r`, definido implicitamente por

```
|x_obs(t) − x_corpo(t_r)| = c (t − t_r)
```

Resolve-se como raiz de `F(L)=L-|x_corpo(t-L)-x_obs(t)|/c`, por Newton:

```
L ← L − F(L) / [1 + r_hat·v_corpo(t-L)/c]
t_r = t-L
```

Para planetas, a iteração anterior também convergia porque
`v_corpo/c≈10⁻⁴`; ela falhava para a referência artificial a `0.99c`. Newton
converge nesse caso e mantém a atualização de ponto fixo apenas como fallback
diagnóstico para uma efeméride inválida. Escalas envolvidas:

| Corpo | Tempo de luz |
|---|---|
| Lua | 1,28 s |
| Sol | 8,32 min |
| Júpiter | 32,7 a 53,8 min |

Não é detalhe: em 8 minutos a Terra percorre 14 000 km, muito mais que o próprio
diâmetro. **O Sol que se vê nunca é o Sol que está lá.**

### Verificação independente

O SPICE resolve exatamente esta equação quando se pede correção `"LT"` em
`spkezr_c`. Como o Milestone 0 deliberadamente usa `"NONE"` (§`ADR-0003`), temos
duas implementações da mesma coisa, e o teste compara uma com a outra para um
observador que seja um corpo — a nossa raiz contra a do JPL. Para uma nave,
que não é um corpo do SPICE, só a nossa serve, e é por isso que precisa estar
certa.

---

## 3. Aberração

Um fóton que chega com direção de propagação `n̂` no referencial `S` chega com
direção `n̂'` no referencial da nave (que se move com `β` em `S`):

```
            n̂ + (γ − 1)(n̂·β̂)β̂ − γβ
n̂'  =  ─────────────────────────────────
                γ (1 − β·n̂)
```

Em termos da **direção até a fonte** `ŝ = −n̂`, com `θ` o ângulo entre `ŝ` e a
velocidade, a forma escalar é

```
cos θ' = (cos θ + β) / (1 + β cos θ)
```

O céu se amontoa para a frente:

| `β` | `γ` | fonte a 90° aparece a | fração do céu com metade das fontes |
|---|---|---|---|
| 0,0100 | 1,0001 | 89,43° | 49,5 % |
| 0,0896 | 1,0040 | 84,86° | 45,5 % |
| 0,5000 | 1,1547 | 60,00° | 25,0 % |
| 0,9048 | 2,3483 | **25,20°** | **4,8 %** |
| 0,9900 | 7,0888 | 8,11° | 0,5 % |

A `β = 0,9048` — o que o tanque cheio em modo CRUZEIRO alcança — **metade de todas
as estrelas do céu se comprime num cone de 25°** à frente. Atrás, o céu se esvazia.

Nota de conceito: isto **não** é a aberração anual clássica (`v/c ≈ 10⁻⁴`, o que o
SPICE chama de `"+S"`). A fórmula acima é exata para todo `β`; a clássica é o
primeiro termo dela.

---

## 4. Doppler

Com `n̂` a direção de propagação do fóton em `S`, o fator Doppler é

```
D ≡ f_observada / f_emitida = γ (1 − β·n̂)
```

Para uma fonte à frente, `β·n̂ = −β` e `D = γ(1 + β) > 1`: azul. Atrás,
`D = γ(1 − β)`: vermelho. Os dois são **recíprocos exatos**, porque
`γ²(1 − β²) = 1`:

| `β` | `D` à frente | `D` à ré | produto |
|---|---|---|---|
| 0,0896 | 1,0940 | 0,9141 | 1,000000 |
| 0,9048 | 4,4731 | 0,2236 | 1,000000 |
| 0,9900 | 14,1067 | 0,0709 | 1,000000 |

### O resultado que torna isto renderizável de verdade

**Um corpo negro deslocado por Doppler continua sendo um corpo negro**, com

```
T' = D · T
```

Não é aproximação: a forma de Planck é invariante sob deslocamento de frequência
acompanhado da transformação de intensidade, e o que muda é só a temperatura.
Como estrelas são aproximadamente corpos negros, a cena pode ser **exata** em vez
de estilizada — basta deslocar a temperatura de cor:

```
β = 0,0896:  estrela de 5800 K →  6345 K à frente,  5302 K à ré
β = 0,9048:  estrela de 5800 K → 25944 K à frente,  1297 K à ré
```

A 25 944 K a estrela sai do visível pelo azul; a 1 297 K, pelo vermelho. O céu à
frente vira ultravioleta e o de trás, infravermelho — **ambos invisíveis**, cada
um por um lado.

---

## 5. Beaming

A intensidade específica obedece a `I_ν/ν³` invariante, de onde a intensidade
bolométrica transforma como

```
I' = D⁴ I
```

Consistente com Stefan–Boltzmann: `σT'⁴ = σ(DT)⁴ = D⁴σT⁴`. É o mesmo `D` da
seção anterior, e o expoente 4 é o que torna o efeito violento:

| `β` | brilho à frente | brilho à ré | razão frente/ré |
|---|---|---|---|
| 0,0896 | 1,4× | 0,70× | 2,0 |
| 0,9048 | **400×** | **0,0025×** | 160 000 |
| 0,9900 | 39 601× | 3·10⁻⁵× | 1,3·10⁹ |

Somando §3, §4 e §5: a `β = 0,9048`, metade do céu se concentra num cone de 25°,
400 vezes mais brilhante e deslocada para o ultravioleta. O resto é escuro,
vermelho e vazio. **Não se parece nada com ir rápido num jogo.**

---

## 6. Rotação de Terrell, e por que escalar meshes é errado

A contração de Lorentz é uma afirmação sobre posições **simultâneas no
referencial do observador**. Uma câmera não registra isso: ela registra luz que
**chegou simultaneamente**, e essa luz partiu de pontos diferentes do objeto em
instantes diferentes. A luz da face mais distante saiu antes.

O resultado (Terrell 1959, Penrose 1959) é que um objeto pequeno, passando
rápido, aparece **girado** por

```
θ_Terrell = arcsin β
```

e não achatado. Para `β = 0,9048`, isso é **64,8°** de rotação aparente.

| `β` | rotação aparente |
|---|---|
| 0,0896 | 5,14° |
| 0,5000 | 30,00° |
| 0,9048 | 64,80° |
| 0,9900 | 81,89° |

A contração existe e está lá — mas ela é exatamente o encurtamento que a rotação
aparente produziria, e o olho lê o conjunto como giro. Escalar a mesh por `1/γ`
mostraria um objeto achatado que **ninguém veria**: seria desenhar a coordenada
em vez da imagem.

Implementação correta: cada vértice tem seu próprio instante retardado. Para um
corpo distante isso é desprezível (o corpo inteiro está à mesma distância dentro
da precisão da tela) e para um sobrevoo próximo não é — e é aí que a rotação
aparece sozinha, sem ninguém programá-la. Essa é a forma certa de obtê-la:
**não como efeito, mas como consequência do tempo de trânsito aplicado por
vértice.**

Há uma sutileza nisso que só apareceu ao medir, e ela está em §11.5: a contração
**entra na conta** — como passo, não como resposta. Omiti-la não dá um objeto
"sem contração"; dá um borrão.

---

## 7. Domínio de validade

| Hipótese | Quando quebra |
|---|---|
| fontes pontuais para o tempo de luz | sobrevoo próximo, onde cada ponto da superfície tem seu retardo — e é onde Terrell aparece |
| espaço plano para a propagação da luz | perto de um corpo massivo: lente gravitacional, atraso de Shapiro |
| corpo negro para as estrelas | linhas espectrais; o deslocamento de uma linha não é deslocamento de temperatura |
| MCRF no evento de recepção | sempre válido: é a definição do referencial do observador |
| sem extinção interestelar, sem poeira | distâncias interestelares |
| `B−V → T_eff` pela fórmula de Ballesteros (§12) | estrelas O/B quentes e gigantes vermelhas: −15 % em Achernar, +5 % em Aldebarã |
| aberração **rígida** por corpo (§11.4) | um corpo de raio angular grande a `β` alto: a aberração varia através do disco e a forma se distorce |
| reflectância cinza para os planetas (§9.4) | a cor de um planeta cujo albedo varia forte com λ dentro da banda deslocada |
| movimento retilíneo durante a travessia de luz do corpo (§11.2) | nunca, na prática: 21 ms para a Terra, contra o tempo em que sua velocidade muda |

Nada disto afeta a **dinâmica**. Tudo isto afeta a **imagem**, e a separação é
o ponto: o `RenderTransform` lê o estado e não escreve nele
(`docs/architecture/rendering.md` §2).

---

## 8. O que o núcleo implementa

```
core/relativity/optics.hpp     aberração, fator Doppler, beaming, temperatura
core/relativity/light_time.hpp iteração de tempo retardado contra o EphemerisProvider
core/render/blackbody.hpp      Planck → CIE XYZ → sRGB linear, e a eficiência de banda η(T)
core/render/star_catalog.hpp   o Yale BSC5 lido de verdade: α, δ, V, B−V → direção, fluxo, T
core/render/relativistic_sky.hpp  aplica optics.hpp a cada estrela, uma vez por quadro
core/render/terrell.hpp        o tempo retardado por vértice, em forma fechada
core/render/tone_response.hpp  a resposta do detector — o limite estrutural do brilho
```

e a interface `apparent_state(corpo, t, observador)`, **explicitamente separada**
de `state()`, porque geométrico e aparente são perguntas diferentes e confundi-las
é o tipo de erro que não aparece até alguém medir.

As seções que seguem são a metade visual: a colorimetria que transforma `T'` em
pixels (§9), o que o `D⁴` de §5 vira quando o detector tem uma banda (§10), o
tempo retardado por vértice de onde Terrell sai sozinho (§11), e o catálogo real
que substituiu o ruído (§12).

---

## 9. De `T'` a pixels: a colorimetria

§4 termina dizendo que a cena pode ser exata porque um corpo negro deslocado
continua corpo negro. Falta a outra metade: **que cor tem um corpo negro a `T`**.
Isso não é física relativística, é colorimetria, e tem uma resposta definida.

### 9.1 O caminho

```
T  →  B_λ(T)  (Planck)  →  ∫ B_λ x̄(λ) dλ  →  XYZ  →  sRGB linear  →  cromaticidade
```

As funções de casamento de cor CIE 1931 (2°) entram pela aproximação analítica
multi-lobo de **Wyman, Sloan & Shirley (2013)**, *JCGT* 2(2) — somas de gaussianas
partidas, com erro declarado de ~1 % do pico. Escolhida em vez de uma tabela de
471 linhas por um motivo verificável: o resultado é conferido contra o **lugar
planckiano publicado** (CIE 15 / Wyszecki & Stiles), e o erro medido é

| `T` | `x` nosso | `x` publicado | erro em `x`, `y` |
|---|---|---|---|
| 1 000 K | 0,6422 | 0,6528 | 1,1·10⁻² |
| 2 000 K | 0,5242 | 0,5267 | 2,5·10⁻³ |
| 3 000 K | 0,4359 | 0,4369 | 1,0·10⁻³ |
| 5 772 K | 0,3264 | — | — |
| 6 500 K | 0,3135 | 0,3135 | < 5·10⁻⁵ |
| 10 000 K | 0,2807 | 0,2807 | < 1·10⁻⁴ |
| 20 000 K | 0,2566 | 0,2565 | 1,0·10⁻⁴ |

Melhor que 1,1·10⁻³ em toda a faixa acima de 3 000 K, e o pior caso — 1,1·10⁻² a
1 000 K — está onde as **caudas** das gaussianas mandam, que é justamente onde a
aproximação é mais fraca. É essa a tolerância do teste, e é essa a justificativa
(§32).

### 9.2 O limite quando `T → ∞`, que é o que torna a tabela possível

Uma LUT precisa de um índice limitado, e `T' = D·T` não é limitado: a `β → 1` o
`D` cresce sem cota. A saída **não** é recortar a faixa — regra 13, o limite tem
que ser estrutural. A saída é notar que a **cromaticidade converge**: no limite de
Rayleigh–Jeans `B_λ ∝ λ⁻⁴` dentro da banda, e a cor para de mudar.

```
T →  ∞ :  x = 0,2401   y = 0,2340      (limite exato de Rayleigh–Jeans)
T = 10⁵ K: x = 0,2428   y = 0,2380
T = 10⁶ K: x = 0,2403   y = 0,2344
```

Então o índice da tabela é uma **bijeção** `[0, ∞) → [0, 1)`:

```
u = T / (T + T₀)          T₀ = 6000 K
```

Sem recorte, sem ramo, sem faixa. A resolução cai onde `u → 1`, e é exatamente lá
que a função é plana: a tabela perde precisão só onde não há o que resolver. A
mesma forma reaparece em §10 para o brilho, e isso não é coincidência — é a forma
canônica de um limite estrutural.

### 9.3 A aritmética em espaço logarítmico

A ré de §4 pede `T' = 1297 K` a `β = 0,9048`, e `142 K` a `β = 0,99`. A `T = 12 K`
o integrando de Planck no visível vale `e^{−2236}`: zero em `double`, e a
cromaticidade vira `0/0`.

A integral é portanto avaliada em espaço logarítmico, fatorando o máximo
(*log-sum-exp*): `ln B_λ = ln(2hc²) − 5 ln λ − ln(e^x − 1)`, com `ln(e^x − 1) → x`
para `x` grande. `X`, `Y`, `Z` saem com um fator comum `e^M` que **se cancela** na
cromaticidade e é recuperado em `ln η`. Funciona de 1 K a 10⁶ K sem um único caso
especial:

```
T = 1 K       ln η = −17 326
T = 142 K     ln η =    −128,2
T = 5 772 K   ln η =      −2,004
T = 10⁶ K     ln η =     −14,47
```

Uma consequência honesta: onde `η < e^{−40}` a cromaticidade tabelada é lixo (as
caudas de Wyman não são as caudas reais das CMF além de 750 nm). Não importa, e o
motivo é estrutural: **a cor é multiplicada pelo fluxo de banda**, e ali o fluxo é
`10⁻¹⁷`. O fator é errado; o produto está certo.

### 9.4 Planetas não são corpos negros

Uma estrela emite; um planeta **reflete**. O modelo usado é o mais simples que
não mente: reflectância cinza vezes o iluminante solar,

```
cor_observada = reflectância · planck_rgb(D · 5772 K) / planck_rgb(5772 K)
```

de modo que a `D = 1` a cor é exatamente o albedo tabelado, e o deslocamento age
sobre o *iluminante*, que é quem de fato sofre Doppler. Entra em §7 como
hipótese: um planeta cujo albedo varie forte com λ dentro da banda deslocada não é
descrito por isto.

---

## 10. O beaming que o detector realmente vê

§5 está certo e é bolométrico: `I' = D⁴ I`. Um olho e uma câmera, porém, têm
**banda**. E §4 já avisou o que isso implica — "a 25 944 K a estrela sai do visível
pelo azul" — sem tirar a consequência. A consequência é grande.

### 10.1 A conta

Defina a **eficiência de banda**, a fração da potência bolométrica que cai na
banda fotópica:

```
η(T) ≡ ∫ B_λ(T) ȳ(λ) dλ / (σT⁴/π)
```

O catálogo dá a magnitude `V`, isto é, o fluxo **já na banda**: `F_V = F · η(T)`.
Observado:

```
F'_V = D⁴ · F · η(DT) = F_V · D⁴ · η(DT)/η(T)
```

Duas consultas à mesma tabela. E como `η` é tabelado em `ln`, o shader calcula
`exp(ln η(DT) − ln η(T))` — uma subtração em vez de uma divisão de números que vão
de `10⁻⁵⁶` a `10⁻¹`.

### 10.2 O que isso muda

| `β` | `D` | `T'` | bolométrico (§5) | **banda visível** |
|---|---|---|---|---|
| 0,0896 à frente | 1,094 | 6 345 K | 1,43× | 1,48× |
| 0,0896 à ré | 0,914 | 5 302 K | 0,698× | 0,655× |
| 0,9048 à frente | 4,473 | 25 944 K | **400×** | **51,5×** |
| 0,9048 à ré | 0,224 | 1 297 K | **2,5·10⁻³×** | **3,3·10⁻⁷×** |
| 0,9900 à frente | 14,11 | 81 819 K | 39 601× | 239× |
| 0,9900 à ré | 0,071 | 411 K | 2,5·10⁻⁵× | 2,2·10⁻²³× |

A `β = 0,9048` a frente é **oito vezes menos brilhante** do que o número
bolométrico promete, e a ré é **7 600 vezes mais escura**. A razão frente/ré sai de
1,6·10⁵ para **1,6·10⁸**: levar a banda em conta não suaviza o efeito, ele o torna
mil vezes mais extremo. Um céu de estrelas que somem, não que avermelham.

### 10.3 O expoente que cai de 4 para 1

A quantidade `D⁴ η(DT)/η(T)` tem inclinação logarítmica `d ln F'_V / d ln D`:

| `β` | `D` | expoente efetivo |
|---|---|---|
| 0,0896 | 1,094 | 4,16 |
| 0,5000 | 1,732 | 2,81 |
| 0,9048 | 4,473 | 1,59 |
| 0,9900 | 14,11 | 1,17 |
| 0,99999 | 447,2 | 1,005 |

e o limite é **exato**: no regime de Rayleigh–Jeans `Y ∝ T` enquanto o bolométrico
`∝ T⁴`, logo `η ∝ T⁻³` e

```
D⁴ · η(DT)/η(T)  →  D⁴ · D⁻³  =  D
```

O beaming visível é `D`, não `D⁴`. A frente **nunca** para de clarear, mas clarea
linearmente em `D` e não na quarta potência. Esse "4 → 1" é um teste: mede-se a
inclinação numérica e compara-se com 4 num extremo e 1 no outro.

### 10.4 O limite estrutural do brilho (regra 13)

`D⁴` varre nove ordens de grandeza; a tela tem duas. Recortar em 1,0 é exatamente
o `if (v > c) v = c` proibido. O limite aqui é o **detector**, e detectores reais
saturam segundo a equação de Naka–Rushton (1966), medida em fotorreceptores:

```
R = L / (L + L½)
```

Isso é uma bijeção `[0, ∞) → [0, 1)`: monótona, suave, sem ramo, e **sem perder
ordem** — duas estrelas com brilhos diferentes continuam com valores diferentes por
mais brilhantes que fiquem. Não é um corte, é a curva de resposta.

`L½` é a **exposição**, e é um parâmetro de apresentação nomeado, como o
`body_scale_exaggeration` de `rendering.md` §3 — não um fator escondido. O padrão
`L½ = 10^(−0,4·2) = 0,158` põe uma estrela de magnitude 2 no meio da escala:

| `V` | `L` | `R` |
|---|---|---|
| −1,46 (Sirius) | 3,83 | 0,960 |
| 0,00 | 1,00 | 0,863 |
| 2,00 | 0,158 | 0,500 |
| 4,00 | 0,025 | 0,137 |
| 6,00 (limite a olho nu) | 0,0040 | 0,0245 |

E quando o `underflow` acontece — `exp(−269)` na ré a `β = 0,99` — ele vai a zero,
que é **a resposta certa**: não há fótons na banda. É o único lugar do projeto
onde o *flush-to-zero* do ponto flutuante é a física, e não um bug.

---

## 11. O tempo retardado por vértice, de onde Terrell sai sozinho

§6 diz *o que* fazer: "cada vértice tem seu próprio instante retardado". Esta seção
diz como, e mostra que a rotação sai exata sem ninguém escrever `arcsin β`.

### 11.1 A forma fechada

Para um vértice em `p` (relativo ao observador) num corpo rígido de velocidade `v`
relativa ao observador, o atraso `Δτ` do **próprio vértice** satisfaz

```
|p − v Δτ| = c Δτ
```

Elevando ao quadrado, é uma quadrática em `Δτ`, e a raiz positiva é

```
          √( (p·v)² + (c² − v²)|p|² )  −  (p·v)
Δτ  =  ─────────────────────────────────────────
                      c² − v²
```

Nenhuma iteração, nenhum `clamp`, e — importante — nenhum caso especial: o
discriminante `(p·v)² + (c² − v²)|p|²` é **estruturalmente positivo** porque
`|v| < c` é garantido pelo estado (regra 13, o propagador não recorta, ele
constrói). A `v = 0` dá `|p|/c`, como tem que dar.

A posição aparente do vértice é `p − v Δτ`. Só isso. **Ninguém gira nada.**

### 11.2 Por que a velocidade pode ser constante aqui

A trajetória real é curva, mas o que esta conta cobre é a travessia de luz **do
corpo**, não a distância até ele:

| | travessia de luz | a velocidade muda de | erro de posição `½aΔt²` |
|---|---|---|---|
| nave (20 m) | 67 ns | 6·10⁻¹⁰ m/s | 2·10⁻¹⁷ m |
| Terra (6 371 km) | 21 ms | 1,3·10⁻⁴ m/s | 1,3·10⁻⁶ m |

Contra os minutos de tempo de luz **até** Júpiter, em que ele anda 20 000 km por
um caminho curvo. Daí a divisão de §11.4: o centro do corpo vai pelo solver de
efemérides de §2; o **diferencial dentro do corpo** vai pela forma fechada acima.

### 11.3 A verificação: `arcsin β`, exato

Um cubo passa; nada no código conhece Terrell. Mede-se o ângulo aparente da face
lateral:

| `β` | `arcsin β` previsto | medido pela forma fechada | erro |
|---|---|---|---|
| 0,0896 | 5,1406° | 5,1406° | < 10⁻¹³ |
| 0,3000 | 17,4576° | 17,4576° | < 10⁻¹³ |
| 0,5000 | 30,0000° | 30,0000° | < 10⁻¹³ |
| 0,7000 | 44,4270° | 44,4270° | < 10⁻¹³ |
| 0,9048 | 64,7964° | 64,7964° | < 10⁻¹³ |
| 0,9900 | 81,8904° | 81,8904° | < 10⁻¹³ |

Concordância até o último bit, no limite de objeto pequeno em que o teorema de
Terrell vale. E o **sentido** também está certo: a face que aparece é a de **trás**,
porque a luz dela saiu antes e foi arrastada para longe na direção do movimento.
É a assinatura do efeito, e não teria como aparecer se alguém tivesse
simplesmente aplicado uma rotação.

### 11.4 Onde cada metade acontece

```
centro do corpo   →  CPU, core/relativity/light_time.hpp
                     minutos de tempo de luz, trajetória curva, efeméride real
diferencial       →  GPU, vertex shader
                     milissegundos, movimento retilíneo, forma fechada de §11.1
aberração         →  CPU, rígida por corpo (limitação em §7)
```

A aberração rígida é a aproximação que fica: ela desloca o corpo inteiro sem
distorcer o disco. Para um corpo de raio angular `α`, o erro de forma é da ordem
de `α` vezes a variação do jacobiano da aberração através do disco — desprezível
para tudo que não seja um sobrevoo rasante a `β` alto.

### 11.5 A contração é um passo, não a resposta

Aqui a implementação contradisse a leitura ingênua de §38, e a medida decidiu.

Uma mesh é a forma do objeto **no referencial dele**. Antes de perguntar quando a
luz saiu, é preciso saber onde cada vértice **está**, no referencial do
observador, num instante — e essa conversão é a transformação de Lorentz:
encurtamento por `1/γ` ao longo do movimento, nada através dele.

Então o pipeline completo tem dois passos, nesta ordem:

```
mesh (referencial do corpo)
      │  contração de Lorentz por 1/γ ao longo de v
      ▼
posições simultâneas no referencial do observador
      │  tempo retardado por vértice (§11.1)
      ▼
imagem
```

Omitir o primeiro passo não produz "um objeto sem contração". Produz um objeto
errado. O juiz é o **teorema de Penrose (1959)**: uma esfera em movimento tem
silhueta **circular**, e não há nada ajustável numa circunferência. Medido para
uma esfera a `β = 0,9048` com raio angular `10⁻⁴ rad`:

| | fora-de-circularidade da silhueta |
|---|---|
| sem a contração | **1,60** — 160 %, um borrão |
| com a contração | **9,0·10⁻⁵** |

e os `9,0·10⁻⁵` são **lineares no tamanho angular** (`9,0·10⁻³` a `10⁻²` rad,
`9,0·10⁻⁵` a `10⁻⁴` rad) e não mudam ao refinar a amostragem de 8·10⁴ para
5·10⁶ pontos — isto é, são o limite de objeto pequeno do próprio teorema, não
ruído numérico.

O que §38 proíbe continua proibido, e agora dá para dizer com precisão o que é:

> **proibido**: escalar a mesh por `1/γ` e **desenhar isso**. É a coordenada, não
> a imagem — um objeto achatado que ninguém vê.
>
> **obrigatório**: escalar por `1/γ` e **continuar** — aplicar o tempo retardado
> por vértice. O achatamento some no processo e o que sobra é a rotação.

Os três números do mesmo caso, que separam as duas coisas:

| | valor |
|---|---|
| raio angular aparente | `arcsin(R/γd)`, medido `2,43995·10⁻³°` contra `2,43987·10⁻³°` |
| distância aparente | `γd` exatamente — a luz saiu quando o corpo estava `γ` vezes mais longe |
| deslocamento fora do eixo | `64,7964°` = `arcsin β`, porque `arctan(βγ) ≡ arcsin β` |

O corpo parece **menor**, e não por alguém ter escalado a mesh: porque a luz que
chega agora partiu de `γd`. E parece **girado** de `arcsin β`, pela mesma conta
que deu o ângulo da aresta em §11.3 — duas medidas independentes do mesmo
número, uma da forma e outra da posição.

---

## 12. O catálogo real

O starfield do Milestone 2 era ruído com semente fixa. Agora é o **Yale Bright
Star Catalogue** (Hoffleit & Warren 1991, BSC5, VizieR V/50): 9 110 registros, o
céu a olho nu inteiro.

| | |
|---|---|
| registros | 9 110 |
| sem coordenadas | 14 — entradas que o próprio catálogo marca como inexistentes (novas, erros históricos) |
| sem `B−V` | 310 — sem cor não há temperatura, e são descartadas |
| **usadas** | **8 786** |
| faixa de `V` | −1,46 (Sirius) a 7,96 |
| faixa de `B−V` | −0,28 a 5,74 → 15 882 K a 1 439 K |

Posições em α/δ J2000, que é o mesmo eixo do frame de integração
(`docs/architecture/coordinate-system.md`): a direção unitária entra na cena sem
rotação nenhuma.

### 12.1 `B−V → T_eff`

A fórmula de **Ballesteros (2012)**, *EPL* 97, 34008, obtida tratando a estrela
como dois corpos negros nas bandas B e V:

```
T_eff = 4600 K · [ 1/(0,92(B−V) + 1,70)  +  1/(0,92(B−V) + 0,62) ]
```

Conferência em estrelas conhecidas:

| Estrela | `B−V` | `T` da fórmula | `T` publicada | erro |
|---|---|---|---|---|
| Sol | +0,65 | 5 778 K | 5 778 K | calibração |
| Vega (A0V) | 0,00 | 10 125 K | 9 600 K | +5 % |
| Sirius (A1V) | 0,00 | 10 125 K | 9 940 K | +2 % |
| Aldebarã (K5III) | +1,54 | 3 734 K | 3 900 K | −4 % |
| Achernar (B3V) | −0,16 | 12 692 K | ~15 000 K | **−15 %** |

Boa a ~5 % entre 4 000 e 10 000 K, e ruim no extremo quente — é uma aproximação de
duas bandas, e estrelas O/B põem a maior parte da energia fora das duas. Está em
§7. Vale a pena porque `B−V` existe para 8 786 estrelas e `T_eff` espectroscópica
não existe para quase nenhuma.

### 12.2 `V → fluxo`

```
F_V = 10^(−0,4 V)
```

relativo a `V = 0`. A escala absoluta não importa: ela é absorvida pela exposição
`L½` de §10.4, que é o parâmetro de apresentação.

### 12.3 O teste que o catálogo permite

A afirmação de §3 — "metade das estrelas num cone de 25°" — passa a ser
**mensurável contra o céu real**, e o resultado é melhor que "aproximadamente":

| `β` | cone `arccos β` | fração do catálogo dentro |
|---|---|---|
| 0,0100 | 89,427° | 49,49 % |
| 0,0896 | 84,859° | 49,49 % |
| 0,5000 | 60,000° | 49,49 % |
| 0,9048 | 25,204° | 49,49 % |
| 0,9900 | 8,110° | 49,49 % |

**O mesmo número para todo `β`**, e não por acaso: a aberração leva o hemisfério
`θ < 90°` sobre o cone `θ' < arccos β` **estrela por estrela**, exatamente. A
fração é invariante porque o *conjunto* é invariante. Os 49,49 % em vez de 50 % são
o catálogo, não a física: o BSC5 é limitado em magnitude e se adensa no plano
galáctico — sobre 400 eixos aleatórios, a fração num cone de 25,2° tem média
4,80 % contra 4,76 % de um céu uniforme, com desvio 1,36 % e máximo 8,66 %.

É a melhor espécie de teste: tolerância **zero**, origem #1 da política de §32 — o
resultado é exato, e um erro de sinal ou de convenção `n̂`/`ŝ` quebraria a
invariância na hora.
