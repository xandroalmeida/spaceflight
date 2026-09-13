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
F / P = η w / c²          com  P = q c² (potência total consumida)
```

No limite `w → c, η → 1` isso vira `F = P/c`: o foguete de fótons. O modelo
reproduz esse limite conhecido sem ter sido construído para isso — é o teste de
sanidade mais forte que temos dele.

### 4.4 O preço de um motor interessante (por que a matemática importa)

Um motor com `w = 0,1c`, `η = 0,5`, `q_max = 0,01 kg/s`:

```
F   = η q w  = 0,5 · 0,01 · 3,0·10⁷  = 1,5·10⁵ N
P   = q c²   = 0,01 · 8,99·10¹⁶      = 9,0·10¹⁴ W  (900 TW)
P_perdida = (1−η) P = 450 TW  a dissipar
```

Isto é, meio quatrilhão de watts de calor residual. O modelo **expõe** esse custo
em vez de escondê-lo — e é exatamente por isso que ele vale mais que
`fuel -= 1`. A decisão de gameplay (radiadores? `η` mais alto? aceitar e ignorar?)
passa a ser uma decisão informada.

---

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

O formato exato (subconjunto de YAML ou JSON) e o parser serão decididos no
Milestone 1; o que está fixado agora é que **os parâmetros não moram no código**.

---

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
