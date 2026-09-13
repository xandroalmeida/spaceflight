# Transporte de spin: precessão de Thomas e geodética

> Como a orientação de uma nave muda **sem que nada a tenha girado**.
>
> Pré-requisitos: [`relativistic-propulsion.md`](relativistic-propulsion.md)
> (redução em componentes, `u = γv`), [`attitude.md`](attitude.md) (quaternions,
> Euler), [`relativistic-gravity.md`](relativistic-gravity.md) (a métrica).

---

## 1. O problema que isto resolve

Uma nave a `β = 0,8` faz uma curva. Os giroscópios estão travados, nenhum
torque é aplicado, o RCS está desligado. Ao completar a volta, ela **aponta
para outro lugar** — 4,19 radianos adiante, se `γ = 5/3`.

Não é erro numérico e não é um torque escondido. É que a composição de dois
*boosts* não colineares não é um boost: é um boost **e uma rotação** (rotação de
Wigner). Uma trajetória curva é uma sequência contínua desses boosts, e a
rotação que eles acumulam é a **precessão de Thomas**.

O mesmo fenômeno, em queda livre num campo gravitacional, chama-se precessão
**geodética** (de Sitter), e foi medido: 6 601,8 ± 18,3 mas/ano pelo Gravity
Probe B, contra 6 606,1 previstos.

Até este ponto o propagador ignorava as duas. O comentário em
`dormand_prince_54.cpp` dizia, literalmente, que a precessão de Thomas "não está
incluída; precisa da sua própria derivação e pertence ao trabalho da geodésica".
Este documento é essa derivação.

---

## 2. A lei: transporte de Fermi–Walker

Um vetor de spin `S^μ` carregado por um observador acelerado, sem torque, obedece

```
dS^μ/dτ = (1/c²) [ (S·A) U^μ − (S·U) A^μ ]
```

com `U^μ` a quadrivelocidade e `A^μ = dU^μ/dτ` a quadriaceleração. O lado
direito é o mínimo necessário para manter `S·U = 0` (o spin permanece puramente
espacial no referencial próprio) e `S·S` constante. Se `A = 0`, o transporte é
paralelo: nada acontece. É por isso que o efeito **é** a aceleração.

Reduzido às componentes espaciais no referencial inercial, isto vira uma rotação
rígida de `S` a uma velocidade angular:

```
ω_T = (γ²/(γ+1)) (a × v)/c²          a = dv/dt,  v velocidade coordenada
```

(Jackson, *Classical Electrodynamics*, 3ª ed., eq. 11.119.)

### 2.1 A forma que o código usa

O propagador não carrega `v` nem `dv/dt`: carrega `u = γv` e `du/dt`. A
conversão não é cosmética — ela **cancela todos os γ**:

```
dv/dt = (1/γ)[ du/dt − v (v·du/dt)/c² ]

a × v = (1/γ)(du/dt × v)                         porque v × v = 0
      = (1/γ²)(du/dt × u)

ω_T = (γ²/(γ+1)) · (1/γ²)(du/dt × u)/c²
```

```
                    du/dt × u
        ω_T  =  ─────────────────
                   (γ + 1) c²
```

Nenhuma divisão por `γ`, nenhuma subtração, e `γ + 1 ≥ 2` sempre. A expressão é
estável de `β = 10⁻¹⁵` a `β = 1 − 10⁻¹⁵`, e é **exata** — não é uma expansão.

Três propriedades saem da forma, não de verificações:

* **empuxo colinear não precessiona.** Se `du/dt ∥ u`, o produto vetorial é
  nulo. Uma nave que acelera em linha reta mantém a orientação, como tem de ser;
* **a precessão é retrógrada.** Para movimento circular `du/dt × u` aponta
  contra o vetor de rotação orbital;
* **o limite newtoniano é zero, não "pequeno".** `du/dt × u → v̇ × v` e o
  prefator `1/((γ+1)c²) → 1/(2c²)`: o efeito é `O(β²)` e desaparece sozinho.

### 2.2 O caso circular tem forma fechada

Para movimento circular uniforme, `|a × v| = ω³r² = ω v²`, logo

```
|ω_T| = (γ²/(γ+1)) β² ω = (γ² − 1)/(γ + 1) · ω = (γ − 1) ω
```

usando `γ²β² = γ² − 1`. Depois de **uma volta completa** no referencial
inercial (`t = 2π/ω`), a orientação girou exatamente

```
Δθ = 2π (γ − 1)
```

A `β = 0,8`, `γ = 5/3`: `Δθ = 2π/3 = 2,0944 rad`. É a rotação de Wigner
acumulada, e é o teste de integração desta feature (§6.4).

---

## 3. Gravidade: a precessão geodética

Em queda livre não há aceleração própria — `A^μ = 0` — e o transporte de
Fermi–Walker vira transporte paralelo. Mas o transporte paralelo numa métrica
curva **também** gira o vetor. A 1PN, para a métrica estática da
[`relativistic-gravity.md`](relativistic-gravity.md):

```
                3  v × ∇U
        ω_G  =  ─  ───────
                2     c²
```

com `U = Σ GMᵢ/rᵢ > 0`, de modo que `∇U` é exatamente a aceleração newtoniana
que `PointMassGravity` já calcula — pela terceira vez neste projeto o mesmo
número serve a um propósito diferente.

O fator `3/2` se decompõe em `1` de curvatura espacial (o termo `B` da métrica) e
`1/2` do mesmo efeito de Thomas da §2, agora com a aceleração gravitacional. Ao
contrário de Thomas, é **progrado**.

### 3.1 Os dois números medidos

**Gravity Probe B** (`a = 7 027,4 km`, órbita polar):

```
GM/(rc²) = 6,3109·10⁻¹⁰      ω = 1,07170·10⁻³ rad/s
ω_G = 1,5 · 6,3109·10⁻¹⁰ · 1,07170·10⁻³ = 1,01458·10⁻¹² rad/s
    = 6 604,1 mas/ano
```

contra **6 606,1 mas/ano** da previsão completa da RG e **6 601,8 ± 18,3**
medidos em 2011. A diferença de 2 mas para a previsão completa é a excentricidade
da órbita e a correção de `J₂`, que este modelo não inclui — e é 9 vezes menor
que a barra de erro experimental.

**A Lua**, no campo do Sol, medida por *lunar laser ranging*:

```
GM_☉/(r c²) = 9,8705·10⁻⁹     ω = 1,99099·10⁻⁷ rad/s  (1 ano)
ω_G = 19,187 mas/ano
```

contra **19,2 mas/ano** observados. O sistema Terra–Lua é um giroscópio de
384 000 km de raio.

---

## 4. Como isto entra no propagador

O estado já carrega o quaternion corpo→inercial `q` e a taxa no corpo `ω_corpo`.
A equação cinemática era

```
dq/dt = ½ q ⊗ (0, ω_corpo) / γ
```

O `1/γ` está lá porque `ω_corpo` é medida no relógio da nave. A precessão é
outra coisa: é o referencial de repouso **em si** girando em relação ao
inercial, a uma taxa definida em tempo **coordenado**. Então ela entra sem
`1/γ`, e expressa no corpo para reaproveitar a mesma função:

```
ω_total_corpo = ω_corpo/γ + q⁻¹(ω_T + ω_G)
dq/dt = ½ q ⊗ (0, ω_total_corpo)
```

As equações de Euler **não mudam**: elas já estão escritas no referencial de
repouso local, e é justamente a rotação *desse* referencial que acabamos de
acrescentar. Somar a precessão ao torque seria contar duas vezes.

No modo `Newtonian` nada disto se aplica: `ω_T` é `O(β²)` e `ω_G` é `O(U/c²)`,
e o modo newtoniano é por definição a teoria onde esses termos não existem.
Inseri-los ali seria misturar aproximações, o mesmo erro que o propagador já
recusa cometer com gravidade.

---

## 5. O que é desprezado, e quanto vale

| Termo | Tamanho | Por quê |
|---|---|---|
| **Lense–Thirring** (arrasto de referencial) | 39,2 mas/ano no GP-B, 0,6 % da geodética | `g₀ᵢ = 0` na métrica; a mesma dívida já declarada em `relativistic-gravity.md` §7 |
| correções `O(β)` no termo geodético | `(3/2)(U/c²)ω · O(β²)` | `ω_G` é 1PN, primeira ordem em `v/c`; `ω_T` é exata. A `β = 0,9` perto de um corpo massivo os dois regimes se sobrepõem e o resultado é apenas indicativo |
| `J₂` na precessão geodética | ~1 mas/ano no GP-B | abaixo da barra de erro experimental (18,3) |
| precessão de Sitter do próprio Sol sobre a nave | 19 mas/ano a 1 UA | incluída automaticamente: `∇U` soma sobre o catálogo inteiro |

A primeira linha é a que importa: assim como na métrica, o termo descartado é
**maior** que refinamentos que poderíamos fazer nos termos mantidos. A ordem de
trabalho é `g₀ᵢ` primeiro, tudo o mais depois.

---

## 6. Testes — medidos

`tests/scientific/test_spin_transport.cpp`, 8 testes.

### 6.1 A reescrita algébrica está certa

`(du/dt × u)/((γ+1)c²)` contra `(γ²/(γ+1))(a × v)/c²` calculado independentemente
a partir de `v` e `dv/dt`, para rapidez de `10⁻⁶` a `21` (`β = 1 − 5·10⁻¹⁹`).
Concordância **na última casa** em todo o intervalo — e é a rota do livro-texto
que perde dígitos no extremo, porque ela forma
`dv/dt = (1/γ)[du/dt − v(v·du/dt)/c²]`, uma diferença de vetores quase iguais.

### 6.2 Estrutura

* empuxo colinear ao longo de um eixo → `ω_T` **bit-a-bit zero**;
* empuxo colinear numa direção arbitrária → zero a `10⁻¹⁶` do que a mesma queima
  transversal produziria. Não é zero exato porque `u.normalized()` não é
  exatamente paralelo a `u` em binário — aritmética, não física, e o teste diz
  qual das duas coisas está medindo;
* movimento circular → `ω_T` antiparalelo ao vetor de rotação orbital.

### 6.3 O limite lento

`|ω_T|/ω_orbital` medido contra `γ−1`, exato, e contra `β²/2`:

```
β = 10⁻⁴ :  −5,0000000·10⁻⁹     (β²/2 = −5·10⁻⁹)
β = 10⁻² :  −5,0003750·10⁻⁵     (β²/2 = −5·10⁻⁵)
β = 10⁻¹ :  −5,0378153·10⁻³     (β²/2 = −5·10⁻³)
```

O fator ½ que Thomas encontrou em 1926, explicando por que o cálculo ingênuo do
acoplamento spin-órbita do elétron dava o dobro do observado.

> **Achado.** A primeira versão deste teste comparava com `gamma - 1.0`, escrito
> como subtração. A `β = 10⁻⁴` isso são 5·10⁻⁹ obtidos subtraindo 1 de um double
> perto de 1: sobra ruído de 2,2·10⁻¹⁶, ou 4·10⁻⁸ relativos, e o teste falhou.
> Este projeto **já tem** `lorentz_factor_minus_one` exatamente para isso, e o
> teste passou a usá-la. O erro era meu, no teste; a implementação estava certa.

### 6.4 Integração: a rotação de Wigner

Nave a `β = 0,8` sob empuxo puramente transversal, uma volta completa, sem
torque nenhum e com `ω_corpo = 0` do início ao fim:

```
trajetória fecha em     4,3·10⁻¹³ do raio
β ao final              0,8 (2,9·10⁻¹⁴ relativos)
orientação girou        4,188790 rad
2π(γ−1) com γ = 5/3     4,188790 rad      →  concordância 10⁻⁹
eixo                    a normal da órbita
```

Ponta a ponta: atravessa o integrador, a redução em componentes do empuxo e a
cinemática do quaternion. **Nada aplicou torque** — o tensor de inércia é
irrelevante neste teste, e a nave mesmo assim voltou apontando 240° adiante.

### 6.5 Gravity Probe B e a Lua

```
GP-B (a = 7 027,4 km):  6 603,88 mas/ano
     previsão RG completa  6 606,1
     medido em 2011        6 601,8 ± 18,3      →  dentro da barra de erro

Lua no campo do Sol:      19,188 mas/ano
     medido por LLR        19,2
```

A diferença de 2,2 mas para a previsão completa é excentricidade e `J₂`, e é
8 vezes menor que a incerteza experimental. O que **não** está aqui é
Lense–Thirring, 39,2 mas/ano, que o GP-B também mediu: precisa de `g₀ᵢ`.

### 6.6 Consistência com o zero

No modo `Newtonian`, uma volta inteira a `β = 0,5` sob empuxo transversal deixa
o quaternion **exatamente** na identidade — ângulo `0,0`, não `10⁻¹⁵`. Os termos
de precessão não são avaliados nesse modo.

E o giroscópio estático: `ω_G = 0` para `v = 0` e para movimento puramente
radial (`v × ∇U = 0`). Um giroscópio caindo em linha reta não precessiona; ele
precisa dar a volta em alguma coisa.

---

## 7. Uma recusa que isto revelou

Escrevendo o teste da §6.4 eu passei ao propagador uma velocidade **própria**
onde ele espera a **coordenada**. `u = γ·0,8c = 4·10⁸ m/s` é maior que `c`;
`relativity::proper_velocity` documenta que devolve zero nesse caso; e o
propagador rodou 200 passos, não moveu nada e **reportou sucesso**.

Isso agora é recusado: `UnsupportedRegime`, com a mensagem dizendo para declarar
a condição inicial em rapidez. Uma recusa, não um clamp — nada é alterado para
tornar a entrada aceitável, o chamador é informado. Teste em
`test_relativistic_propulsion.cpp`.
