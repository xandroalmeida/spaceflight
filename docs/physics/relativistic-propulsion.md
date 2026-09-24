# Propulsão Relativística

Status: **formulação fechada, implementação em curso** (Milestone 4)
Pré-requisitos: `docs/physics/relativity-roadmap.md` §3, `docs/physics/propulsion-model.md` §3
Última revisão: 2026-09-13

§37 do enunciado exige que este documento exista e esteja consistente **antes** de
qualquer código. O que segue é a redução em componentes da forma covariante já
fixada, os limites que ela precisa reproduzir, e os testes analíticos que a
verificam.

---

## 1. Estado matemático

```
r     posição              [m]     referencial SSB/J2000
u     velocidade própria   [m/s]   u ≡ γv = p/m₀
m₀    massa de repouso     [kg]
τ     tempo próprio        [s]
q     orientação (MCRF)    quaternion
```

com `t` (tempo coordenado TDB) como variável independente.

Grandezas derivadas, **sem cancelamento em nenhum ponto**:

```
γ = √(1 + |u|²/c²)              (soma de positivos: exato para todo u)
v = u/γ                         (|v| < c identicamente, para todo u finito)
β = |v|/c = |u|/(γc)
γ − 1 = (|u|/c)² / (γ + 1)      (a forma sem subtrair, ver §7)
```

**Por que `u` e não `v`** está em `relativity-roadmap.md` §3.1 e não se repete
aqui. O ponto operacional: `|v| < c` deixa de ser algo a vigiar e passa a ser a
forma da função. Não existe `u` finito que produza `|v| ≥ c`, então não existe
lugar onde um clamp pudesse ser escrito (§13).

A orientação é definida **no referencial de repouso instantâneo** (MCRF). Os
eixos de uma nave são propriedade dela, não do observador; é no MCRF que o bico
aponta para onde o piloto acha que aponta.

---

## 2. Equações

### 2.1 Forma covariante (já fixada em `propulsion-model.md` §3)

```
dP^μ/dτ = −q U^μ + F ê^μ ,      P^μ = m₀ U^μ ,   ê·U = 0 ,  ê·ê = 1
```

Separando as partes paralela e ortogonal a `U`:

```
dm₀/dτ = −q                      (consumo)
m₀ dU^μ/dτ = F ê^μ               (4-aceleração de módulo F/m₀)
```

com `F = η q w` (o empuxo próprio) e `ê` o versor tipo-espaço ao longo do eixo do
motor **no MCRF**.

### 2.2 Redução a componentes

Com `U^μ = (γc, u)` e `dτ/dt = 1/γ`:

```
dr/dt  = v = u/γ
du/dt  = F ê_espacial / (m₀ γ)
dm₀/dt = −q/γ
dτ/dt  = 1/γ
```

onde `ê_espacial` é o resultado de levar `(0, n̂)` do MCRF para o referencial de
coordenadas por um boost de velocidade `v`:

```
ê_espacial = n̂ + (γ − 1)(n̂·β̂)β̂
```

`n̂` é a direção do bico no MCRF — exatamente o eixo `+x` do corpo rotacionado
pelo quaternion de atitude.

### 2.3 Os dois limites que a fórmula precisa dar

**Empuxo paralelo ao movimento** (`n̂ = β̂`):

```
ê_espacial = β̂ [1 + (γ − 1)] = γ β̂
⇒ du/dt = (F/m₀) n̂ = a_própria · n̂
```

A velocidade própria cresce à taxa da **aceleração própria**, sem fator `γ`
nenhum. É o resultado que torna `u` a variável natural: a equação de movimento
longitudinal é literalmente a lei de Newton escrita em `u`.

**Empuxo perpendicular** (`n̂·β̂ = 0`):

```
ê_espacial = n̂
⇒ du/dt = (F/(m₀ γ)) n̂
```

A aceleração transversal é reduzida por `γ`: é mais difícil curvar uma trajetória
rápida do que acelerá-la em linha reta. Um modelo que aplicasse `a = F/m` em
ambos os casos erraria a curva por um fator 22 a `β = 0,999`.

### 2.4 Aceleração própria × coordenada

```
a_própria    = F/m₀                    (o que um acelerômetro a bordo mede)
a_coordenada = dv/dt = (du/dt)/γ − (v/γ)(d γ/dt)/c² · c² ...
```

A forma útil, obtida derivando `v = u/γ` com `γ = √(1+u²/c²)`:

```
dv/dt = (1/γ) [ du/dt − (v·du/dt) v/c² ]
```

Ou seja, `a_coordenada` cai como `1/γ³` no caso longitudinal e como `1/γ` no
transversal. **O código nunca integra `a_coordenada`** — ela existe só para o
mostrador, e está aqui porque §37 pede.

---

## 3. Conservação de momento

O que a formulação garante, e como se verifica:

* **Massa-energia.** `dm₀/dτ = −q` e a energia sai pelo jato e pelo calor na
  proporção de `propulsion-model.md` §4.4. A identidade
  `convertida = cinética do jato + desperdício` já é testada e não muda aqui.
* **4-momento.** `dP^μ/dτ = −q U^μ + F ê^μ` é a afirmação de que o 4-momento que
  a nave perde é exatamente o que o escapamento leva. A parte `−q U^μ` é a massa
  de repouso saindo *com a nave*; a parte `F ê^μ` é o empuxo.
* **Vínculo de camada de massa.** `g_μν u^μ u^ν = −c²`, que em espaço plano é
  `γ²c² − |u|² = c²`. Com `γ` **calculado a partir de `u`**, isso é uma
  identidade algébrica, não uma restrição: verifica a aritmética, não a física
  (`relativity-roadmap.md` §4). Em espaço curvo passará a verificar a física, e
  aí será monitorado — **diagnóstico, nunca correção**.

---

## 4. Equação do foguete

Integrando `dφ/dτ = a_própria/c` com `a_própria = ηqw/m₀` e `q = −dm₀/dτ`:

```
Δφ = (η w / c) · ln(m₀/m₁)              φ = artanh(β)
β  = tanh(Δφ)
```

`Δφ` **não depende** de como a queima foi distribuída no tempo, nem do `β`
inicial — rapidez é aditiva, velocidade não. Isso torna o teste independente da
trajetória.

Custo, para `w = 0,1c` e `η = 0,98`:

| `β` alvo | `φ = artanh β` | razão de massa `m₀/m₁` |
|---|---|---|
| 0,1 | 0,100335 | 2,78 |
| 0,5 | 0,549306 | 2,72·10² |
| 0,9 | 1,472219 | 3,34·10⁶ |
| 0,99 | 2,646652 | 5,36·10¹¹ |

A tirania da equação do foguete fica **pior** em regime relativístico, e o modelo
mostra isso sem precisar ser avisado.

---

## 5. Limites newtonianos

Cada um é um teste, não uma observação:

| Grandeza | Limite `β → 0` |
|---|---|
| `γ` | `1 + β²/2 + O(β⁴)` |
| `u` | `v (1 + β²/2)` → `v` |
| `du/dt` longitudinal | `F/m₀` → idêntico a Newton |
| `du/dt` transversal | `F/(m₀γ)` → `F/m₀` |
| `dm₀/dt` | `−q/γ` → `−q` |
| `dτ/dt` | `1/γ` → `1` |
| `Δφ` | `Δv/c` → Tsiolkovsky `Δv = v_eff ln(m₀/m₁)` |

O propagador relativístico tem de **convergir numericamente** para o newtoniano
quando `β ≪ 1`, não aproximadamente parecer com ele. A `β = 0,01` a diferença
relativa prevista é `β²/2 = 5,0·10⁻⁵`.

---

## 6. Testes analíticos

### 6.1 Movimento hiperbólico (aceleração própria constante)

O caso com solução fechada completa:

```
φ(τ) = a τ / c
β(τ) = tanh(a τ / c)
u(τ) = c sinh(a τ / c)
t(τ) = (c/a) sinh(a τ / c)
x(τ) = (c²/a) [cosh(a τ / c) − 1]
```

A 1 g (`a = 9,80665 m/s²`):

```
τ = 0,1 ano  →  β = 0,102864401   t = 0,100178 ano
τ = 1,0 ano  →  β = 0,774827263   t = 1,187312 ano
```

Verifica de uma vez: `u`, `β`, `τ`, a posição e a relação entre tempo próprio e
coordenado.

### 6.2 Escada de `β` (§31 do enunciado)

```
β = 0,01    γ = 1,000050004      φ = 0,010000333
β = 0,1     γ = 1,005037815      φ = 0,100335348
β = 0,5     γ = 1,154700538      φ = 0,549306144
β = 0,9     γ = 2,294157339      φ = 1,472219490
β = 0,99    γ = 7,088812050      φ = 2,646652412
β = 0,999   γ = 22,366272042     φ = 3,800201167
```

Em cada degrau: equação do foguete, tempo próprio, e o limite `|v| < c`.

### 6.3 O limite é estrutural, não vigiado

Aplicar `Δφ = 10` (razão de massa `e^{10/0,098} ≈ 10^{45}`, absurda de propósito)
tem de dar `β = tanh(10) = 0,9999999979` — **abaixo de 1**, sem nenhum clamp no
caminho. O teste existe para demonstrar que não há onde um clamp seria necessário.

### 6.4 Anisotropia

Mesma queima, mesma duração, aplicada paralela e perpendicularmente ao
movimento a `β = 0,9`: `|Δu|` na transversal tem de ser `γ = 2,294157` vezes
menor. Um modelo que ignorasse a diferença passaria em todos os testes
longitudinais e falharia neste.

---

## 7. Armadilhas numéricas já conhecidas

* **`γ` a partir de `v`** perde dígitos quando `β → 1` (`relativity-roadmap.md`
  §3.1). Calcule sempre de `u`.
* **`γ − 1` por subtração** perde dígitos quando `β → 0` e devolve exatamente
  zero abaixo de `β ≈ 3·10⁻⁹` — já corrigido no Milestone 2, com
  `γ − 1 = β²/(s(1+s))`. A forma equivalente em `u` é `(|u|/c)²/(γ+1)`.
* **Especificar o estado por `v`** perde precisão perto de `c`: em `β = 0,999999`
  os dígitos de `v` que importam já não cabem. Cenários relativísticos devem ser
  escritos em **rapidez** ou em `u`, e a interface de cenário oferecerá as duas.
* **`v` satura em `c` antes de a física acabar.** Como `β = 1 − 1/(2γ²)`, a
  diferença `1 − β` cai abaixo de meio ulp de 1 (1,11·10⁻¹⁶) em `γ ≈ 6,7·10⁷`.
  Além disso, `v = u/γ` **arredonda exatamente para `c`** e `β` lê 1,0 — não por
  overflow nem por clamp, mas porque a velocidade coordenada daquele estado não é
  um `double` distinguível. Medido:

  ```
  u/c = 1·10⁷   γ = 1,0·10⁷   1 − β = 5,00·10⁻¹⁵
  u/c = 6,7·10⁷ γ = 6,7·10⁷   1 − β = 1,11·10⁻¹⁶
  u/c = 1·10⁸   γ = 1,0·10⁸   1 − β = 0
  ```

  **O estado não é afetado**: `u`, `γ` e a rapidez continuam exatos ali, e todas
  as equações integradas são escritas em `u`. É o argumento mais forte a favor da
  escolha da variável, e só aparece quando se vai procurar.
* **Tempo coordenado** precisa da representação em duas partes
  (`coordinate-system.md` §5): a `β = 0,99` um ulp de tempo de um `double` único
  vale 56 m de posição.

---

## 8. O que este documento NÃO autoriza

Gravidade em regime relativístico. A escolha de manter `a_coordenada` de
gravidade como termo aditivo em `du/dt` vale enquanto `Φ/c² ≪ 1` **e** o
tratamento newtoniano do campo for aceitável — o que deixa de ser verdade muito
antes de `β → 1`. A formulação correta (métrica 1PN + geodésica exata) está em
`relativity-roadmap.md` §5 e é trabalho separado.

Portanto: **a propulsão relativística é implementada e testada em espaço plano**,
sem gravidade, exatamente como os testes de Tsiolkovsky do Milestone 1 já são. Um
cenário que ligue o regime relativístico junto com gravidade de N corpos está
misturando uma aproximação válida com uma inválida, e o código recusa.

## 9. O jogo aceita a mistura de §8, deliberadamente

Até aqui a sessão do jogo integrava com cinemática **newtoniana**: o padrão de
`IntegratorConfig`. Isso passou despercebido enquanto nada a bordo chegava perto
de `c`, e deixou de ser aceitável com o modo RELATIVÍSTICO
([`propulsion-model.md`](propulsion-model.md) §4.7): três dias dele, integrados
por Newton, levariam a nave a `0,95 c · ln 20 = 2,85 c`.

A sessão agora integra com `Kinematics::SpecialRelativistic` e liga
`allow_gravity_with_relativistic_kinematics` — a mistura que §8 recusa por
padrão, aceita **pelo chamador que assume a afirmação**, como a flag exige. A
afirmação, com números:

* o empuxo, a massa e a velocidade passam a ser exatos em relação a `c`: é a
  mesma cinemática dos testes de espaço plano;
* a gravidade continua o campo de N corpos com J2, somado a `du/dt` como
  aceleração coordenada. O erro disso é da ordem de `β² g` (mais `Φ/c² g`, 10⁻⁸
  g):

| onde | `β` | `g` | `β² g` |
|---|---|---|---|
| órbita baixa, 7,7 km/s | 2,6·10⁻⁵ | 8,7 m/s² | 6·10⁻⁹ m/s² |
| 1 h depois de acender o RELATIVÍSTICO | 0,012 | 6·10⁻³ m/s² | 8·10⁻⁷ m/s² |
| cruzeiro a 0,993 c, a 1 UA do Sol | 0,993 | 6·10⁻³ m/s² | 6·10⁻³ m/s² |
| chegada a Marte, a 1 000 km, freando | 1,7·10⁻⁴ | 2,2 m/s² | 6·10⁻⁸ m/s² |

Neste Sistema Solar a nave é rápida só onde a gravidade é fraca, e forte só onde
a nave é lenta — o produto nunca passa de milímetros por segundo ao quadrado.
Antes, com Newton, o erro de ordem `β²` estava na cinemática **inteira**, e não
só no termo gravitacional.

**O estado público continua `v`.** O propagador carrega `u = γv` só por dentro
(`state_from_array` converte antes de qualquer modelo de força, e `dense_output`
devolve `v`), então nada fora dele converte — e quem divide por `γ` "para obter
`v`" divide duas vezes. O primeiro rascunho desta mudança fez exatamente isso e
o teste a pegou: `β` final de 0,7047 em vez de 0,9933, que é `0,9933/√(1+0,9933²)`.

**O planejador de Lambert continua newtoniano** (`FlightSession::planning_integrator`):
as suas campanhas foram qualificadas assim, e às poucas km/s de uma transferência
o voo relativístico difere dele em 10⁻¹⁰.

Verificado em `tests/presentation/test_presentation_flight.cpp`,
`the_game_flies_relativistic_kinematics_and_never_reaches_c`: o tanque inteiro
do RELATIVÍSTICO, queimado na própria sessão do jogo, dá `β = 0,993276` — a
equação do foguete — com o relógio de bordo em 3,19 dias contra 5,54 de tempo
coordenado.

A formulação sem essa aproximação (métrica 1PN e geodésica) continua sendo a de
`relativity-roadmap.md` §5.
