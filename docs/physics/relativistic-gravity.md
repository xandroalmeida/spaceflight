# Gravidade em Regime Relativístico

Status: **formulação** — implementação em curso
Pré-requisitos: `docs/physics/relativity-roadmap.md` §5, `docs/physics/relativistic-propulsion.md` §8
Última revisão: 2026-09-13

## 1. O problema que isto resolve

Hoje o propagador **recusa** rodar cinemática relativística junto com um campo
gravitacional (`PropagationStatus::UnsupportedRegime`). A recusa está certa —
somar uma aceleração newtoniana a uma dinâmica de espaço plano é colar uma
aproximação válida numa inválida, e o erro é silencioso. Mas a consequência é que
a nave de `β = 0,9` que o Milestone 4 construiu só voa no vazio: ela **não pode
voar dentro do Sistema Solar**, que é o objetivo do projeto.

Este documento fecha isso.

## 2. A assimetria que torna o problema tratável

`relativity-roadmap.md` §5 já identificou o ponto, e ele merece ser repetido
porque é tudo:

* as **fontes** são lentas (`v/c ~ 10⁻⁴`) e o campo é fraco
  (`U/c² ≲ 10⁻⁸` a 1 UA do Sol, `6,5·10⁻¹⁰` em LEO);
* a **partícula-teste** é rápida, mas é partícula-teste: não gera campo.

Portanto: expandir a **métrica** em campo fraco (legítimo) e integrar a
**geodésica exata** para a nave (sem expandir em `v_nave/c`, onde expandir seria
ilegítimo).

## 3. A métrica

Gauge harmônico, campo fraco, fontes **quase estáticas**, com
`U = Σ GMₐ/|x − xₐ|` (positivo):

```
g₀₀ = −A ,   A = 1 − 2U/c² + 2U²/c⁴
gᵢⱼ = B δᵢⱼ ,  B = 1 + 2U/c²
g₀ᵢ = 0                      ← ver §7, é a aproximação principal
```

Gradientes, e uma economia agradável:

```
∇U = a_newtoniana            (∇(GM/r) = −GM r̂/r²)
∇A = (−2/c² + 4U/c⁴) ∇U
∇B = (2/c²) ∇U
```

O somatório de `∇U` é **exatamente** o que `PointMassGravity` já calcula. A
camada relativística reaproveita esse número em vez de recalculá-lo, e o teste de
limite newtoniano compara os dois.

## 4. Variável de estado: `u^i = dx^i/dτ`

Em espaço plano o Milestone 4 integra `u = γv` e ganha `|v| < c` como forma da
equação. Em espaço curvo a generalização correta é a **parte espacial da
4-velocidade**, com a componente temporal fixada pelo vínculo de camada de massa:

```
g_μν u^μ u^ν = −c²   ⟹   −A (u⁰)² + B |u|² = −c²

                    ⟹   u⁰ = √( (c² + B|u|²) / A )
```

`u⁰` é real e positivo para **todo** `u` finito. E daí

```
v = c u / u⁰            dτ/dt = c / u⁰            dt/dτ = u⁰/c
```

### O limite de velocidade, agora na forma curva

```
|v| = c |u| √A / √(c² + B|u|²)   →   c √(A/B)   quando |u| → ∞
```

O teto **não é `c`**: é a velocidade coordenada da luz no ponto, `c√(A/B)`, que
perto de uma massa é menor que `c`:

| Local | `c√(A/B)` |
|---|---|
| 1 UA do Sol | `c (1 − 1,97·10⁻⁸)` |
| órbita baixa da Terra | `c (1 − 1,31·10⁻⁹)` |
| superfície do Sol | `c (1 − 4,25·10⁻⁶)` |

E ele é **inatingível por construção**, exatamente como `c` era em espaço plano:
nenhum `u` finito o alcança. A promessa do §13 — o limite tem de sair da
formulação, não de vigilância — sobrevive à curvatura, e fica mais forte: agora o
teto é o teto *local*, e nem ele precisa ser vigiado.

Em `U → 0` tudo colapsa no Milestone 4: `A = B = 1`, `u⁰ = γc`, `v = u/γ`.

## 5. A equação da geodésica

Para esta métrica (estática, diagonal), os símbolos de Christoffel espaciais são

```
Γⁱ₀₀ = (1/2B) ∂ᵢA
Γⁱⱼₖ = (1/2B) [ δᵢₖ ∂ⱼB + δᵢⱼ ∂ₖB − δⱼₖ ∂ᵢB ]
Γⁱ₀ⱼ = 0        (estática)
```

e a geodésica `du^i/dτ = −Γⁱ_μν u^μ u^ν` se reduz a

```
du/dτ = − (1/2B) [ (u⁰)² ∇A + 2 u (∇B·u) − |u|² ∇B ]
```

Parametrizando por tempo coordenado, que é o que o integrador usa:

```
dr/dt  = v = c u / u⁰
du/dt  = (c/u⁰) · du/dτ
dτ/dt  = c / u⁰
```

**Nada aqui é expandido em `v_nave`.** A equação vale de `v = 0` a
`v → c√(A/B)` com a mesma fórmula.

### Empuxo entra do mesmo jeito

`du/dτ` recebe a aceleração própria do motor — é a mesma 4-aceleração do
`relativistic-propulsion.md` §2, agora somada ao termo geodésico. Gravidade não é
força, e por isso não aparece em `ForceResult.acceleration` neste modo: quem a
carrega é a métrica.

## 6. Os dois limites que a fórmula precisa dar

**Newtoniano** (`U ≪ c²`, `|u| ≪ c`): `A → 1`, `∇A → −2∇U/c²`, `u⁰ → c`, e

```
du/dτ → −(1/2)[c²(−2∇U/c²)] = ∇U = a_newtoniana        ✓
```

**Deflexão da luz** (`|u| → ∞`, movimento transversal ao gradiente):

```
du/dτ → |u|² (2 ∇U/c²)
```

— o **dobro** do valor newtoniano, que é o resultado clássico da RG e o teste que
distingue esta métrica de uma gravidade "com correção". Sai sozinho da forma de
`A` e `B`; ninguém o programou.

## 7. O que é desprezado, e por quê — com os números

A aproximação principal é `g₀ᵢ = 0`: nada de arraste de referencial, nada de
termos `∂ₜ`. O custo, relativo ao termo newtoniano:

```
arraste de referencial ~ 4 (v_nave/c)(v_corpo/c)
```

| Regime | Termo desprezado | 1PN estático (`U/c²`) mantido |
|---|---|---|
| velocidades planetárias (`β ~ 10⁻⁴`) | 4·10⁻⁸ | 10⁻⁸ … 6·10⁻¹⁰ |
| nave a `β = 0,9` | **3,6·10⁻⁴** | 10⁻⁸ … 6·10⁻¹⁰ |

**Isto é honesto e é desconfortável:** a `β = 0,9` o termo que estou jogando fora
é quatro ordens **maior** que os termos 1PN estáticos que estou mantendo. Manter
`U²/c⁴` e descartar o arraste é inconsistente como contagem de ordens.

Mantenho assim mesmo, e declaro por quê:

1. o que domina a `β → 1` não é nenhum dos dois — é o acoplamento `v²/c²` da
   geodésica, que esta formulação captura **exato** e que uma 1PN padrão trunca;
2. `3,6·10⁻⁴` do termo newtoniano é pequeno em valor absoluto para uma nave que
   passa perto de um corpo por pouco tempo, e o erro acumulado é proporcional ao
   tempo passado em campo forte — que, a `β = 0,9`, é curtíssimo;
3. incluir `g₀ᵢ` corretamente exige os termos `∂ₜ` junto, sob pena de quebrar a
   própria consistência de gauge, e isso é trabalho separado — não um parâmetro
   a mais.

Fica registrado como a **primeira coisa a melhorar** neste módulo, com o número
que quantifica o quanto se ganha.

Também fora: lente gravitacional na propagação da luz (o `light_time.hpp` usa
linha reta), efeitos de maré sobre a nave, e qualquer coisa perto de um objeto
compacto — `U/c² ≪ 1` é premissa, não detalhe.

## 8. Testes analíticos

### 8.1 Precessão do periélio de Mercúrio

O teste clássico, contra um número **medido**:

```
Δω = 6π GM / (c² a (1 − e²))        por órbita
```

Com `a = 5,790905·10¹⁰ m`, `e = 0,205630`, `GM_☉ = 1,32712440041·10²⁰`:

```
Δω = 5,018663·10⁻⁷ rad/órbita
415,20 órbitas/século  →  42,981 arcsec/século
```

contra os **42,98″/século** observados. Nenhuma constante deste modelo foi
ajustada para isso.

### 8.2 Dilatação gravitacional: os relógios do GPS

```
dτ/dt = c/u⁰ = √( A / (1 + B|u|²/c²) )
```

Para um relógio estático (`u = 0`): `dτ/dt = √A ≈ 1 − U/c²` — desvio para o
vermelho gravitacional. Para um em órbita, os dois efeitos juntos:

```
GPS  (r = 26 561 km):  U/c² = 1,669752·10⁻¹⁰   v²/2c² = 8,348759·10⁻¹¹
                       dτ/dt − 1 = −2,504628·10⁻¹⁰
solo (r = 6 371 km) :  dτ/dt − 1 = −6,961274·10⁻¹⁰

diferença: +38,51 µs/dia
```

O valor citado na literatura é 38,6 µs/dia, que inclui a rotação da Terra (o
relógio de solo se move a 465 m/s no equador). Sem ela, 38,5 — e a diferença
entre os dois números é o que a rotação vale.

### 8.3 Atraso de Shapiro

A velocidade coordenada da luz é `c√(A/B) ≈ c(1 − 2U/c²)`, então um sinal
rasante ao Sol chega atrasado:

```
Δt = (2GM/c³) ln(4 r₁ r₂ / d²)
```

Terra–Vênus com `d = R_☉`: **116,3 µs** de ida. Isto testa a métrica, não o
integrador, e por isso é um teste separado.

### 8.4 Convergência para o newtoniano

A `β ≪ 1` e `U/c² ≪ 1` a geodésica tem de **convergir numericamente** para
`PointMassGravity` — e a diferença entre as duas não é vaga. Depois de uma
revolução em LEO a órbita relativística precessou, pela mesma fórmula de
Mercúrio, e num círculo isso é um deslocamento ao longo da trajetória:

```
Δω = 6π GM / (c² a (1 − e²)) = 1,23335·10⁻⁸ rad/órbita   (r = 6778 km)
r·Δω = 8,3598 cm por órbita
```

Medido: **8,3598 cm**. É o efeito de Mercúrio observado em noventa minutos em
vez de um século.

> **Correção.** A primeira versão deste documento previa aqui apenas "a ordem de
> `U/c² + v²/c² ≈ 1,3·10⁻⁹`". O teste mediu 1,23·10⁻⁸, nove vezes maior, e eu
> estava errado: o que separa os dois modelos depois de uma órbita **inteira**
> não é o tamanho instantâneo dos termos, é o que eles acumulam — o `6π` da
> precessão. O teste agora afirma a forma fechada, não a ordem de grandeza.

E a taxa de tempo próprio na mesma órbita:

```
1 − dτ/dt = U/c² + v²/2c² = 9,81490·10⁻¹⁰   (medido 9,81490·10⁻¹⁰)
```

### 8.5 O teto de velocidade é estrutural

Para `|u|/c` de 1 a 10⁹ em LEO, `|v|` tem de ficar em `c√(A/B)` ou abaixo, e o
vínculo `g_μν u^μ u^ν = −c²` tem de valer identicamente — porque `u⁰` é
*definido* por ele. Não existe caminho no código onde um clamp pudesse ser
inserido: o limite é a **forma** de `u⁰`, não uma comparação (regra 13).

Duas ressalvas de aritmética, ambas do teste e nenhuma do modelo:

* **o resíduo do vínculo não pode ser medido contra `c²`.** A `|u|/c = 10⁹` os
  dois termos de `−A(u⁰)² + B|u|²` valem 10¹⁸ c² cada um, e o `c²` em que deviam
  diferir está 18 ordens abaixo do arredondamento deles. Normalizado pelo
  tamanho dos termos, o resíduo é plano — 8,9·10⁻¹⁷, 6,7·10⁻¹⁷, 9,1·10⁻¹⁷,
  1,1·10⁻¹⁶ para `|u|/c` = 1, 10³, 10⁶, 10⁹ — isto é, alguns ulp, de ponta a
  ponta;
* **acima de `γ² = 1/ε` a desigualdade estrita deixa de ser representável.**
  `|v| = c√(A/B)·(1 − 1/2γ²)` e, para `γ ≳ 6,7·10⁷`, `1 − 1/2γ²` arredonda para
  1: `|v|` cai exatamente sobre o teto local. É a grade do ponto flutuante, a
  mesma fronteira já documentada para `β` em
  [relativistic-propulsion.md](relativistic-propulsion.md). O teste exige `<`
  abaixo dela e `≤` acima.

### 8.6 A deflexão da luz, sem programar o fator 2

Nada no código diz "dobre a gravidade para fótons". Mas a aceleração geodésica
transversal, medida em tempo coordenado e dividida pela newtoniana no mesmo
ponto, vale:

```
|u|/c = 1      →  1,5
|u|/c = 10     →  1,9901
|u|/c = 10³    →  2,000000
|u|/c = 10⁶    →  2,000000
|u|/c = 3·10⁻⁵ →  1,000000   (partícula lenta)
```

O 1 e o 2 saem os dois das **formas** de `A` e `B` — `A` contribui com `∇U` e
`B` com outro `∇U` quando o movimento é ultrarrelativístico. É o fator que
Eddington mediu em 1919, e aqui ele é consequência, não constante.

O limite lento é `1 + O(U/c²)`, não 1 exato (resíduo medido 3,8·10⁻⁸ a 1 UA,
ou 3,9 `U/c²`): `B` no denominador e o termo `2U²/c⁴` de `A` sobrevivem nessa
ordem.


## 9. Onde isto vive, e o que vem depois

| Peça | Arquivo |
|---|---|
| a métrica e suas derivadas | `core/gravity/weak_field_metric.{hpp,cpp}` |
| o terceiro modo do integrador | `core/propagation/dormand_prince_54.cpp` |
| os sete testes | `tests/scientific/test_relativistic_gravity.cpp` |

O modo se seleciona por `IntegratorConfig::kinematics = Kinematics::GeneralRelativistic`
e exige `set_metric()`; sem métrica o propagador **recusa** (`UnsupportedRegime`)
em vez de cair silenciosamente no newtoniano. Neste modo o `ForceModel` não
recebe gravidade nenhuma: ela está na geometria, e somar as duas seria contá-la
duas vezes — o propagador também recusa qualquer aceleração que não seja empuxo.

A próxima melhoria é o **arrasto de referencial** (`g₀ᵢ ≠ 0`), pela razão dada
na §7: a 0,9 c o termo que jogamos fora é 3,6·10⁻⁴, quatro ordens **acima** dos
termos 1PN estáticos que mantivemos. Enquanto `g₀ᵢ = 0`, este modelo é honesto
para trajetórias sub-relativísticas perto de corpos em rotação e para
trajetórias relativísticas longe deles — não para as duas coisas ao mesmo tempo.
