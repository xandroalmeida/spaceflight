# Renderização Relativística

Status: **formulação** (Milestone 5). A camada de núcleo é implementada aqui; a
parte visual (shaders, starfield real) é trabalho separado.
Pré-requisitos: `docs/architecture/rendering.md` §6, `docs/physics/relativity-roadmap.md` §6
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

Resolve-se por iteração de ponto fixo:

```
t_r ← t − |x_obs(t) − x_corpo(t_r)| / c
```

A taxa de convergência é `v_corpo/c ≈ 10⁻⁴` por iteração, então três ou quatro
passos chegam à precisão da máquina. Escalas envolvidas:

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
observador que seja um corpo — a nossa iteração contra a do JPL. Para uma nave,
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

---

## 7. Domínio de validade

| Hipótese | Quando quebra |
|---|---|
| fontes pontuais para o tempo de luz | sobrevoo próximo, onde cada ponto da superfície tem seu retardo — e é onde Terrell aparece |
| espaço plano para a propagação da luz | perto de um corpo massivo: lente gravitacional, atraso de Shapiro |
| corpo negro para as estrelas | linhas espectrais; o deslocamento de uma linha não é deslocamento de temperatura |
| MCRF no evento de recepção | sempre válido: é a definição do referencial do observador |
| sem extinção interestelar, sem poeira | distâncias interestelares |

Nada disto afeta a **dinâmica**. Tudo isto afeta a **imagem**, e a separação é
o ponto: o `RenderTransform` lê o estado e não escreve nele
(`docs/architecture/rendering.md` §2).

---

## 8. O que o núcleo implementa neste milestone

```
core/relativity/optics.hpp     aberração, fator Doppler, beaming, temperatura
core/relativity/light_time.hpp iteração de tempo retardado contra o EphemerisProvider
```

e a interface `apparent_state(corpo, t, observador)`, **explicitamente separada**
de `state()`, porque geométrico e aparente são perguntas diferentes e confundi-las
é o tipo de erro que não aparece até alguém medir.

O que fica para a sessão visual: shaders de cor e brilho, starfield a partir de um
catálogo real, e o deslocamento por vértice que produz Terrell.
