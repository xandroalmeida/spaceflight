# Política de Tolerâncias

Status: vigente a partir do Milestone 0
Última revisão: 2026-09-13

## 1. A regra

§32 do enunciado:

> Nenhum teste científico deve simplesmente usar `EXPECT_NEAR(a, b, 0.01)` sem
> justificativa.

Aqui isso não é uma convenção, é uma restrição do compilador. Toda comparação
aproximada do harness exige uma string de justificativa como último argumento:

```cpp
CHECK_NEAR_ABS(position_error, 0.0, 25.0,
               "DE440 vs Horizons/DE441 ... medido: Sol 0,000 m, Terra 0,030 m, Lua 2,421 m ...");
```

Não existe sobrecarga sem a justificativa. Um número mágico **não compila**.

A justificativa é impressa junto com o resultado, de modo que a saída do teste é
a própria documentação da tolerância:

```
ok    position_error  = 2.42, expected 0, abs error 2.42, tolerance 25
      tolerance from: DE440 (de440s.bsp) vs Horizons/DE441 neste epoch, medido: ...
```

## 2. Origens legítimas de uma tolerância

Em ordem de preferência:

| # | Origem | Exemplo neste repositório |
|---|---|---|
| 1 | **Zero**: o resultado é exato | J2000 = 0 s por definição; `3-4-12-13` é pitagórico exato |
| 2 | **Aritmética de ponto flutuante**: n ulps de uma grandeza conhecida | ortogonalidade de `a × b`: `8·ε·|a|²·|b|` |
| 3 | **Precisão do valor de referência** | `GM_☉ = 1,32712440018e20` publicado com 12 dígitos |
| 4 | **Diferença medida entre duas fontes independentes** | DE440 × DE441: 0,03 m (Terra), 2,42 m (Lua) |
| 5 | **Erro do integrador derivado da tolerância pedida** | `atol × n_passos` como cota superior linear |
| 6 | **Truncamento de uma expansão** | termo de maré `2GMr/d³` truncado em `O(r/d)` → 5,3 % para a Lua |

Origens **ilegítimas**: "passou com esse número", "parecia razoável",
"10 % está bom".

## 3. Tolerâncias em vigor

Todas foram medidas na execução de 2026-09-13 (Apple clang 21, arm64,
`de440s.bsp`). O valor medido está sempre **abaixo** do limite, com a margem
indicada.

### 3.1 Efemérides (`tests/scientific/test_spice_positions.cpp`)

| Quantidade | Medido | Limite | Origem |
|---|---|---|---|
| posição Sol vs Horizons | 0,000 m | 25 m | diferença DE440/DE441 medida, ×10 |
| posição Terra vs Horizons | 0,030 m | 25 m | idem |
| posição Lua vs Horizons | 2,421 m | 25 m | idem (pior caso) |
| velocidade (pior caso, Lua) | 6,8·10⁻⁶ m/s | 10⁻⁴ m/s | idem |
| baricentro Terra–Lua `Σm·r` | ~10⁻¹² rel | 10⁻⁹ rel | arredondamento das constantes publicadas |

O limite de 25 m é ~10⁴ vezes **menor** que qualquer erro de frame, de unidade ou
de época (que apareceriam como 10⁵ m ou mais) e ~10⁴ vezes **maior** que o ruído
numérico. É a faixa em que o teste distingue "outra release do JPL" de "bug".

### 3.2 Tempo (`tests/integration/test_time_conversion.cpp`)

| Quantidade | Medido | Limite | Origem |
|---|---|---|---|
| J2000 | 0 s | 0 s | definição |
| TT − TAI | 32,1840000153 s | ±10⁻⁶ s | exato por definição; ulp de 8,3·10⁸ s = 1,2·10⁻⁷ s |
| TDB − TT | 8·10⁻⁵ … 1,6·10⁻³ s | < 2·10⁻³ s | amplitude conhecida 1,657 ms |
| TDB − UTC (2026) | 69,184 s | ±2·10⁻³ s | 32,184 + 37 leap seconds + termo periódico |

### 3.3 Integração (`tests/scientific/test_two_body.cpp`, `test_integrator_convergence.cpp`)

Com `rtol = 10⁻¹²`, `atol_r = 10⁻⁶ m`, `atol_v = 10⁻⁹ m/s`:

| Quantidade | Medido | Limite | Origem |
|---|---|---|---|
| fechamento após 1 órbita circular | ~10⁻⁴ m | 10⁻² m | `atol × n_passos`, cota linear |
| deriva de energia, 10 órbitas | ~10⁻¹¹ rel | 10⁻¹⁰ rel | medido + 1 ordem |
| deriva de momento angular, 10 órbitas | 1,2·10⁻¹¹ rel | 10⁻¹⁰ rel | RK explícito **não** preserva invariantes quadráticos |
| vs solução de Kepler, 5 órbitas | 6,2·10⁻¹⁰ rel | 10⁻⁸ rel | cota linear `atol × n_passos` = 2,4·10⁻¹⁰ + crescimento secular |
| ida e volta 6 h | 1,2·10⁻³ m | 10⁻² m | soma dos dois erros globais |
| erro em 1 órbita, rtol 10⁻¹² | 5,9·10⁻⁴ m | 10⁻² m | `rtol·10⁷ × 480 passos` |

**Nota importante sobre momento angular.** A tentação é escrever "força central ⇒
torque nulo ⇒ conservação exata". Isso vale para o *fluxo exato*, não para o
integrador: métodos Runge–Kutta explícitos não preservam invariantes quadráticos
(só os métodos de colocação de Gauss preservam). A primeira versão deste teste
usava 10⁻¹² com essa justificativa errada e falhou — corretamente.

### 3.4 Gravidade (`tests/scientific/test_solar_system_gravity.cpp`)

| Quantidade | Medido | Limite | Origem |
|---|---|---|---|
| `GM_☉/r²` na Terra | 6,1329·10⁻³ m/s² | 10⁻³ rel | valor tabelado 5,93·10⁻³ a 1 UA, com 3 algarismos, escalado por `1/r²` |
| superposição = soma das partes | 0 | 10⁻¹⁸ m/s² | mesma ordem de somatório ⇒ bit a bit |
| maré lunar em LEO (exata) | 1,17·10⁻⁶ m/s² | 10⁻⁹ rel | diferença exata de duas inverse-square |
| maré lunar vs `2GMr/d³` | — | 10 % | truncamento em `O(r/d)`: `3r/d` = 5,3 % |
| maré solar vs `2GMr/d³` | — | 10 % | `3r/d` = 1,4·10⁻⁴ |
| remover o Sol do catálogo | 0,53·(½aT²) | ±25 % | cota de queda livre `½aT²`, reduzida pela resposta orbital |

### 3.5 Achatamento J₂ (`tests/scientific/test_j2_oblateness.cpp`)

| Quantidade | Medido | Limite | Origem |
|---|---|---|---|
| `\|a\|` no equador vs `(3/2)J₂GM R²/r⁴` | — | 10⁻¹⁴ rel | forma fechada recalculada das mesmas constantes |
| `\|a\|` no polo vs o dobro do equatorial | — | 10⁻¹⁴ rel | identidade algébrica |
| razão J₂/pontual em LEO | 1,4380·10⁻³ | 10⁻³ rel | valor citado em `gravity-model.md` §4, 4 algarismos |
| deriva de `L·n̂` em 6 h | 4,04·10⁻¹² rel | 10⁻¹¹ rel | invariante exato; resíduo = erro do integrador |
| deriva de `\|L\|` na mesma corrida | 3,16·10⁻⁴ rel | > 100× `L·n̂` | se fosse conservado, J₂ não estaria agindo |
| regressão nodal, 30 órbitas | −5,0149 °/dia (previsto −5,0027) | 1,5 % | termos de curto período omitidos pela teoria secular de 1ª ordem, amplitude `J₂(R/p)² = 0,055°` contra 9,65° de sinal |
| precessão apsidal, i = 30/51,6/80° | erro 0,7 % / 0,3 % / 0,2 % | 3 % | idem, amplitude `J₂(R/p)²/e` |
| obliquidade da Terra via polo do SPICE | 23,439292° | ±0,02° | valor de referência 23,4393° + nutação (até 9″) |

A escolha de `e = 0,1` no teste apsidal **é** parte da tolerância: com `e = 2·10⁻³`
a oscilação de excentricidade induzida por J₂ (`~J₂(R/p)² ≈ 10⁻³`) é da ordem da
própria excentricidade, a direção do periastro passeia mais do que precessa, e a
medida não significa nada. A primeira versão do teste usava `e = 2·10⁻³` e errava
por um fator 3 — o defeito estava no experimento, não no código.

### 3.6 Dense output (`tests/scientific/test_dense_output.cpp`)

| Quantidade | Medido | Limite | Origem |
|---|---|---|---|
| gravar muda a trajetória? | 0 | **0** exato | gravar lê os estágios já calculados; qualquer diferença é acoplamento indevido |
| passos / avaliações de força com e sem gravação | idênticos | igualdade exata | idem |
| `y(θ=0)` vs estado inicial do passo | 0 | **0** exato | `c₁ = y₀` é identidade algébrica |
| `y(θ=1)` vs estado final do passo | — | 10⁻⁹ m | `c₁+c₂ = y₁`; só a divisão por `h` arredonda |
| erro interpolado vs Kepler, 2000 amostras | 3,07·10⁻⁴ m | 10⁻² m | é o erro **global do integrador** em uma órbita, não da interpolação |
| erro interpolado vs erro nos extremos | iguais até o último dígito | ≤ 2× | a extensão contínua de 4ª ordem não acrescenta nada mensurável |

Um achado colateral: o solver de Kepler dos testes usava critério de convergência
**absoluto** (`|ΔE| < 10⁻¹⁵`), que trava para `E ≈ 2π`, onde um ulp já vale
8,9·10⁻¹⁶ — a iteração oscilava entre dois doubles vizinhos até estourar o limite
de iterações. Só apareceu ao amostrar 2000 pontos ao longo da órbita. O critério
agora é relativo.

### 3.7 Propulsão (`tests/unit/test_engine.cpp`, `tests/scientific/test_propulsion.cpp`)

| Quantidade | Medido | Limite | Origem |
|---|---|---|---|
| `F = ηqw`, linearidade no throttle | — | 10⁻¹⁵ rel | recálculo das mesmas constantes |
| limite de fótons `F = P/c` | — | 10⁻¹⁵ rel | identidade do modelo em `w = c, η = 1` |
| `convertida = jato + desperdício` | — | 10⁻¹² rel | identidade, para todo `(w, η)` |
| fração convertida, motor químico | 4,5·10⁻¹⁰ | 10⁻⁶ rel | `(w/c)²/2` — o defeito de massa da reação |
| Tsiolkovsky em espaço livre | — | 10⁻¹⁰ rel | é a solução **exata** ali; resíduo = erro do integrador |
| massa final após queima | — | 10⁻¹² rel | `dm/dt` constante ⇒ linear ⇒ RK exato |
| corte por tanque vazio | — | 10⁻¹² rel | instante de exaustão em forma fechada, `t = propelente/q` |
| escala da perda gravitacional | 99,19 | ±25 % | lei `(nΔt)²`; previsto 100 para 10× de empuxo |
| queima curta vs plano impulsivo | — | 10⁻⁴ rel | `(nΔt)²/24 ≈ 6·10⁻⁸` para queima de 1 s |
| `Δv` de Hohmann vs vis-viva | — | 10⁻¹⁴ rel | mesma álgebra, ordem de operações diferente |
| Hohmann LEO→GEO executada | `a` exato, `e = 4,9·10⁻⁶` | `a` 2·10⁻³, `e` 5·10⁻³ | nível em que a transferência deixaria de ser utilizável |

**O coeficiente da perda gravitacional não é universal.** A primeira versão do
documento afirmava `κ = 1/24`, que vale para guiamento inercial (o empuxo perde
alinhamento). Com guiamento *prograde* o mecanismo é outro — a nave sobe durante
a queima — e o valor medido é `κ ≈ 0,31`. O teste verifica o **expoente**, que é
robusto; o coeficiente está documentado com o valor medido.

### 3.8 Lambert e targeting (`tests/scientific/test_lambert.cpp`, `tests/unit/test_targeting.cpp`)

| Quantidade | Medido | Limite | Origem |
|---|---|---|---|
| velocidade reconstruída de um arco conhecido | 4·10⁻¹² … 7·10⁻¹¹ | 10⁻⁹ rel | tolerância da bisseção (10⁻¹⁰ no tempo de voo) propagada |
| chegada verificada por propagação numérica | 0,5 … 2 mm | 10⁻² m | bisseção + erro global do integrador a `rtol = 10⁻¹³` |
| `Δv` de Lambert a 179,9° vs Hohmann | — | 5·10⁻³ rel | a diferença que 0,1° de geometria faz |
| soma dos ângulos prógrado + retrógrado | 0 | 10⁻¹² | identidade `= 2π` |
| ramos de Stumpff no limiar | 8·10⁻¹⁰ | 10⁻⁷ rel | o **fechado** perde ~7 dígitos por cancelamento ali — é a razão de a série existir |
| corretor: recuperar velocidade conhecida | 2,4·10⁻⁷ m | 10⁻³ m | mapa suave e invertível; Newton chega em 3 iterações |
| corretor: intercepto lunar | 3,5 km (de 267 573 km) | — | limitado pela não linearidade perto do alvo |

O passo de diferença finita **é** uma tolerância, e foi medido em vez de
escolhido: 0,5 m/s estagna em 36 km, 0,1 m/s em 155 km, 10⁻³ m/s converge em
3,5 km (`docs/physics/lambert.md` §6).

### 3.9 Regressão (`tests/regression/test_reference_states.cpp`)

| Quantidade | Limite | Origem |
|---|---|---|
| posições fixadas | 10⁻³ m | determinismo; ulp de 1,5·10¹¹ m é 3,3·10⁻⁵ m |
| propagação LEO fixada | 10⁻³ m | idem, mais o cancelamento SSB→geocêntrico |
| contagem de passos | exata | o controle de passo é determinístico |

## 4. Quando uma tolerância pode mudar

Apenas com uma destas razões, registrada no commit:

1. **A referência mudou** (nova release de efeméride, nova constante publicada) —
   atualize o valor de referência *e* a origem declarada;
2. **A física mudou** (J₂ entrou no modelo) — o teste passa a medir outra coisa;
3. **A tolerância estava errada** — como no caso do momento angular acima. Corrija
   a *justificativa*, não só o número.

Afrouxar uma tolerância para fazer um teste passar, sem alterar a justificativa,
é o modo exato pelo qual uma suíte de testes deixa de significar alguma coisa.

## 5. Testes pulados não são testes verdes

Testes que dependem de kernels e não os encontram terminam com código 77 e
aparecem como `Skipped` no CTest:

```
$ SPACEFLIGHT_KERNEL_DIR=/nope ./build/bin/test_spice_positions; echo $?
77
```

Uma suíte inteiramente pulada **não** é uma suíte aprovada, e a saída do CTest
deixa isso explícito.
