# Modelo de Propulsão

Status: **especificação**. Implementação a partir do Milestone 1 (forma
newtoniana) e Milestone 4 (forma relativística, após
`docs/physics/relativistic-propulsion.md`).
Última revisão: 2026-09-13

> O motor é fictício. A matemática não.

---

## 1. Rejeição do modelo ingênuo

O que **não** será implementado (§17 do enunciado):

```cpp
thrust = throttle * maxThrust;
fuel  -= arbitraryNumber;
```

O problema não é estético: esse modelo viola conservação de energia e de momento
simultaneamente, e torna impossível responder à pergunta "quanto Δv esta nave
tem?" de forma consistente, porque empuxo e consumo ficam desacoplados.

---

## 2. Variáveis do modelo

| Símbolo | Significado | Unidade |
|---|---|---|
| `m₀` | massa de repouso total da nave (seca + propelente) | kg |
| `q` | taxa de consumo de massa de repouso, `q ≡ −dm₀/dτ ≥ 0` | kg/s |
| `w` | velocidade de exaustão no referencial instantâneo da nave | m/s |
| `η` | eficiência de direcionamento, `0 < η ≤ 1` | — |
| `T` | throttle, `0 ≤ T ≤ 1` | — |
| `γ_w` | `1/√(1 − w²/c²)` do jato | — |
| `μ` | taxa de massa de repouso *do jato* | kg/s |
| `F` | empuxo próprio (no referencial instantâneo da nave) | N |

Restrição dura, verificada na construção do motor:

```
0 < w ≤ c
```

Um `w > c` não é "um motor melhor": é um modelo inválido, e o construtor lança
exceção.

---

## 3. Derivação (conservação, não postulado)

Trabalhamos no referencial instantaneamente comóvel (MCRF), durante um intervalo
de tempo próprio `dτ`.

**Hipóteses do modelo:**

1. A nave converte massa de repouso em energia a uma taxa `q c²` (tecnologia
   fictícia: conversão massa-energia de alta eficiência).
2. Uma fração `η` dessa energia sai como **jato direcionado**, com velocidade `w`.
3. A fração `(1 − η)` é radiada **isotropicamente** (calor residual): carrega
   energia, mas momento líquido nulo.

**Balanço de energia** (por unidade de tempo próprio):

```
q c²  =  γ_w μ c²        +   (1 − η) q c²
        └ jato ┘             └ perdas ┘

⇒  γ_w μ = η q                                                   (1)
```

**Balanço de momento** (as perdas isotrópicas não contribuem):

```
m₀ (dv/dτ)|MCRF  =  γ_w μ w                                      (2)
```

Substituindo (1) em (2):

```
┌────────────────────────────────────┐
│   F = η q w                        │   empuxo próprio
│   a_própria = F/m₀ = η q w / m₀    │   aceleração própria
│   dm₀/dτ = −q                      │   consumo
│   μ = η q / γ_w                    │   massa de repouso do jato
└────────────────────────────────────┘
```

Forma covariante (o que será implementado no Milestone 4):

```
dP^μ/dτ = −q U^μ  +  F ê^μ ,      ê·U = 0 ,  ê·ê = 1
```

com `P^μ = m₀ U^μ`. Separando as partes paralela e ortogonal a `U`, isso dá
exatamente `dm₀/dτ = −q` e `m₀ dU^μ/dτ = F ê^μ` — ou seja, a 4-aceleração tem
módulo `F/m₀` e aponta na direção do eixo do motor. A redução dessa expressão a
componentes no frame de coordenadas (`du/dt` com `u = γv`) é responsabilidade de
`docs/physics/relativistic-propulsion.md`, conforme §37 do enunciado.

---

## 4. Consequências verificáveis

### 4.1 Equação do foguete relativístico

De `dφ/dτ = a_própria/c` (com rapidez `φ = artanh(β)`) e `q = −dm₀/dτ`:

```
dφ = −(η w/c) · dm₀/m₀      ⇒      Δφ = (η w / c) · ln(m₀/m₁)
```

e a velocidade final é `β = tanh(Δφ)` — que **nunca** atinge 1 para razão de
massa finita. O limite `v < c` é, mais uma vez, estrutural.

### 4.2 Limite newtoniano

Para `Δφ ≪ 1`: `Δv ≈ c·Δφ = η w · ln(m₀/m₁)`, isto é, Tsiolkovsky com

```
v_eff = η w              (velocidade de exaustão efetiva)
Isp   = v_eff / g₀       (com g₀ = 9,80665 m/s², exato por definição)
```

Este é o modelo implementado no Milestone 1, e é o **mesmo modelo**, não um
modelo paralelo: apenas a forma de baixa velocidade das mesmas equações.

### 4.3 Empuxo por potência

```
F / (q c²) = η w / c²
```

No limite `w → c, η → 1` o jato não leva massa de repouso nenhuma (`μ = ηq/γ_w →
0`), toda a massa consumida vira energia, e a relação vira `F = P/c`: o foguete
de fótons. O modelo reproduz esse limite conhecido sem ter sido construído para
isso — é o teste de sanidade mais forte que temos dele.

### 4.4 O preço de um motor, e o que `η` realmente significa

A contabilidade completa de energia, por unidade de tempo próprio:

```
fluxo bruto de energia de repouso   q c²
massa de repouso que sai no jato    μ = η q / γ_w
energia de repouso convertida       (q − μ) c² = q c² (1 − η/γ_w)
   ↳ vira energia cinética do jato  (γ_w − 1) μ c² = η q c² (1 − 1/γ_w)
   ↳ vira calor residual            (1 − η) q c²
```

As duas últimas somam exatamente a terceira — identidade do modelo, verificada em
`tests/unit/test_engine.cpp` para todo par `(w, η)`.

**Motor de classe química** (`w = 9 km/s = 3·10⁻⁵ c`, `η = 1`, `q = 15 kg/s`):

```
v_eff  = 8 993,8 m/s        Isp = 917 s
fração da massa convertida  = 1 − 1/γ_w = (w/c)²/2 = 4,5·10⁻¹⁰
potência no jato            = ½ q w² = 6,07·10⁸ W
calor residual              = 0
```

O modelo reproduz sozinho o **defeito de massa** de uma reação química: a massa
convertida é 4,5·10⁻¹⁰ da consumida. Ninguém disse isso a ele; é a forma da
equação de energia no limite `w ≪ c`. É essa a razão para confiar nele no outro
extremo.

**Motor de fusão/aniquilação** (`w = 0,1c`, `η = 0,5`, `q = 0,01 kg/s`):

```
F                  = η q w                 = 1,50·10⁵ N
energia convertida = q c²(1 − η/γ_w)       = 4,52·10¹⁴ W  (452 TW)
   ↳ no jato       = η q c²(1 − 1/γ_w)     = 2,25·10¹² W  (2,25 TW)
   ↳ calor         = (1 − η) q c²          = 4,49·10¹⁴ W  (449 TW)
```

E aqui está a lição que o modelo entrega de graça:

> **`η = 0,5` a `w = 0,1c` significa aniquilar metade do propelente à toa.**
> O calor residual é ~200 vezes a energia que chega ao jato.

`η` **não** é uma "eficiência" no sentido intuitivo de "quão bem o motor
funciona". É a fração da energia de repouso consumida que sai como jato dirigido
(massa de repouso do jato **mais** sua energia cinética). Um motor sensato tem
`η → 1`; o restante é literalmente massa destruída sem produzir empuxo.

O efeito de `η` no desempenho é direto — `v_eff = η w`, então `η = 0,5` custa
metade do `Δv` por quilo — e o efeito na energia é brutal. Os cenários de exemplo
em `tests/scenarios/` usam `η ≈ 0,98` por essa razão, e não por gosto.

Isto é exatamente o tipo de coisa que `thrust = throttle * maxThrust` esconde.

### 4.5 Os dois motores dos cenários, lado a lado

`config/engines/` traz os dois, e a comparação é o argumento inteiro:

| | classe química | tocha (Mk II) |
|---|---|---|
| `w` | 8 993,8 m/s | 8 993 800 m/s (`0,03 c`) |
| `q_max` | 15 kg/s | 0,015 kg/s |
| **empuxo** | **134 907 N** | **134 907 N** |
| `Isp` | 917 s | 917 100 s |
| massa convertida | 4,5·10⁻¹⁰ | 4,5·10⁻⁴ |
| potência no jato | 0,61 GW | 607 GW |
| budget (600 kg secos + 400 de propelente) | 4,59 km/s | 4 594 km/s = 0,0153 c |

O empuxo é **igual de propósito**: a velocidade de exaustão subiu mil vezes e o
fluxo de massa desceu mil vezes. A nave voa exatamente igual — mesma aceleração,
mesma duração de queima, mesma trajetória — e gasta a milésima parte do
propelente. Uma queima de 1 704 m/s que custava 173 kg passa a custar 0,19 kg.

O preço aparece onde tem de aparecer: a fração da massa de repouso convertida sobe
de um defeito de massa químico para 4,5·10⁻⁴, e a potência do jato de 0,61 GW para
607 GW. Com `η = 1` nada disso vira calor residual; com `η = 0,5` seriam 607 GW no
jato e ~10¹⁵ W a dissipar (§4.4). O modelo cobra a conta sozinho.

## 5. Throttle e consumo

```
q(T)   = T · q_max
F(T)   = η · q(T) · w = T · F_max
m₀(t)  = m_seca + m_propelente(t),     dm_propelente/dτ = −q
```

O empuxo é linear no throttle **porque** o consumo é. Quando o tanque esvazia,
`q = 0` e `F = 0` — sem caso especial, sem clamp: o modelo simplesmente não tem
mais massa para consumir.

**O propelente tem massa** (§16): a massa da nave cai enquanto o motor queima, e
portanto a aceleração sobe a empuxo constante. Isso é observável na
instrumentação e é testado.

---

## 6. Configuração fora do código (§18)

```yaml
# config/engines/fusion-torch.yaml
engine:
  name: "Fusion Torch Mk I"
  max_mass_flow_kg_s: 0.01        # q_max
  exhaust_velocity_fraction_c: 0.1 # w/c  (0 < w/c <= 1)
  efficiency: 0.5                  # eta  (0 < eta <= 1)

propellant:
  initial_mass_kg: 40000.0

spacecraft:
  dry_mass_kg: 12000.0
```

Validação na carga (falha ruidosa, nunca silenciosa):

```
0 < exhaust_velocity_fraction_c <= 1
0 < efficiency <= 1
max_mass_flow_kg_s > 0
initial_mass_kg >= 0
dry_mass_kg > 0
```

O formato adotado é **JSON com comentários de linha** (ADR-0007); o equivalente
do arquivo acima é:

```json
{
  // Fusion Torch Mk I -- tecnologia fictícia, matemática real.
  "engine": {
    "name": "Fusion Torch Mk I",
    "max_mass_flow_kg_s": 0.01,          // q_max
    "exhaust_velocity_fraction_c": 0.1,  // w/c, em (0, 1]
    "efficiency": 0.5                    // eta, em (0, 1]
  },
  "propellant": { "initial_mass_kg": 40000.0 },
  "spacecraft":  { "dry_mass_kg": 12000.0 }
}
```

---

## 6.1 Como o modelo entra no integrador

A massa de repouso é **variável de estado**, integrada junto com posição e
velocidade — não atualizada "por fora" depois do passo. O motivo é que a
aceleração depende da massa instantânea, e o integrador avalia a derivada sete
vezes dentro de um único passo, em instantes distintos:

```
dr/dt = v
dv/dt = a_gravidade + (η q w / m) ê        ê = direção do empuxo
dm/dt = −q                                 q = throttle · q_max
```

Atualizar a massa só no fim do passo faria os sete estágios usarem a massa
errada, degradando o método de 5ª ordem para algo entre 1ª e 2ª durante toda
queima. Consequências práticas:

* o vetor de estado tem 8 componentes: `[x y z vx vy vz τ m]`;
* a massa participa do controle de erro (com tolerância absoluta própria, em kg),
  porque o erro dela realimenta a aceleração;
* ligar e desligar o motor é uma descontinuidade na derivada, e por isso a
  propagação é quebrada nos instantes de chaveamento em vez de atravessá-los
  (`docs/architecture/navigation.md` §4).

## 7. Testes previstos

| Teste | Verificação |
|---|---|
| Tsiolkovsky | queima newtoniana reproduz `Δv = v_eff ln(m₀/m₁)` |
| conservação de massa | `m₀(t) = m₀(0) − ∫q dτ` dentro do erro do integrador |
| tanque vazio | empuxo cai a zero sem descontinuidade na posição/velocidade |
| limite de fótons | `w → c, η → 1` ⇒ `F → P/c` |
| foguete relativístico | `Δφ = (ηw/c)ln(m₀/m₁)` em `β = 0,1 … 0,999` |
| limite newtoniano | a forma relativística converge para Tsiolkovsky quando `β → 0` |
| rejeição de entrada | `w > c` e `η > 1` são recusados na construção |
