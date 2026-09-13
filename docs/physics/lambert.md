# O Problema de Lambert

Status: **implementado** (uma revolução, órbitas elípticas e hiperbólicas)
Última revisão: 2026-09-13
Uso arquitetural: `docs/architecture/navigation.md` §6

## 1. O problema

> Dados dois pontos `r⃗₁` e `r⃗₂` em torno de um corpo de parâmetro `μ`, e um tempo
> de voo `Δt` entre eles, qual é a órbita que os liga?

A resposta é `v⃗₁` e `v⃗₂` — as velocidades necessárias na partida e na chegada.
Com elas:

```
Δv_partida = v⃗₁ − v⃗_nave(t₁)
Δv_chegada = v⃗_alvo(t₂) − v⃗₂
```

É assim que se planeja uma interceptação: onde a Lua **estará** em `t₂` vem da
efeméride, não de onde ela está agora.

O teorema de Lambert garante que `Δt` depende apenas de `r₁ + r₂`, da corda
`c = |r⃗₂ − r⃗₁|` e do semieixo maior `a` — não da forma da órbita. É isso que
torna o problema resolúvel por uma única variável.

## 2. Formulação escolhida: variáveis universais

Implementamos a formulação de **variáveis universais** (Bate–Mueller–White;
Vallado, *Fundamentals of Astrodynamics and Applications*, algoritmo de Lambert
por `ψ`), com **bisseção** sobre `ψ`.

Definições:

```
cos Δν = (r⃗₁ · r⃗₂)/(r₁ r₂)                       (ângulo de transferência)
A = sin Δν √( r₁ r₂ / (1 − cos Δν) )
```

Funções de Stumpff:

```
          ⎧ (1 − cos √ψ)/ψ            ψ > 0
C(ψ)  =   ⎨ (cosh √(−ψ) − 1)/(−ψ)     ψ < 0
          ⎩ 1/2                       ψ = 0

          ⎧ (√ψ − sin √ψ)/ψ^{3/2}                 ψ > 0
S(ψ)  =   ⎨ (sinh √(−ψ) − √(−ψ))/(−ψ)^{3/2}       ψ < 0
          ⎩ 1/6                                   ψ = 0
```

Iteração: para cada `ψ`,

```
y(ψ) = r₁ + r₂ + A (ψ S(ψ) − 1)/√C(ψ)
χ    = √( y/C(ψ) )
Δt(ψ) = ( χ³ S(ψ) + A √y ) / √μ
```

`Δt(ψ)` é **monotonicamente crescente** em `ψ`, o que é exatamente a propriedade
que autoriza bisseção sem risco: `ψ → −∞` dá trajetórias hiperbólicas rápidas,
`ψ → 4π²` dá a órbita que quase completa uma revolução.

Recuperação das velocidades (coeficientes de Lagrange):

```
f  = 1 − y/r₁
g  = A √(y/μ)
ġ  = 1 − y/r₂

v⃗₁ = (r⃗₂ − f r⃗₁)/g
v⃗₂ = (ġ r⃗₂ − r⃗₁)/g
```

### Por que bisseção e não Newton

Newton converge em ~5 iterações contra ~50 da bisseção, mas a derivada
`dΔt/dψ` tem forma feia perto de `y → 0` e o método diverge para transferências
próximas do limite. A bisseção **não pode divergir**: o intervalo só encolhe. Em
um problema que vai ser resolvido algumas centenas de vezes por planejamento —
não milhões — 50 iterações de uma função barata é irrelevante, e robustez vale
mais que velocidade. Se um dia o custo aparecer num perfil, a resposta é Izzo
(2014), não Newton mal-condicionado.

## 3. Domínio de validade e casos recusados

| Caso | Tratamento |
|---|---|
| `Δν = 0` ou `Δν = 2π` | **recusado**: `A = 0`, os pontos coincidem em direção |
| `Δν = π` exato (pontos antípodas) | **recusado**: o plano da transferência é indeterminado — qualquer plano contendo os dois pontos serve. Fisicamente real, matematicamente sem solução única |
| `Δt ≤ 0` | recusado |
| `Δt` menor que o mínimo parabólico | recusado por não convergência dentro do intervalo de `ψ` |
| múltiplas revoluções (`Δt` > um período) | **não implementado**: devolvemos a solução de revolução única, que existe e é válida, mas pode não ser a de menor `Δv` |

O caso `Δν = π` merece ênfase porque parece um bug e não é: uma transferência de
180° entre dois pontos é o caso de Hohmann idealizado, e é justamente onde o
problema de Lambert **não** tem solução única. O planejador deve pedir 179,9° ou
180,1°. Documentado, testado, e a mensagem de erro diz isso.

## 4. Precisão e verificação

A tolerância da bisseção é relativa em `Δt`: paramos quando
`|Δt(ψ) − Δt_pedido| ≤ 10⁻¹⁰ Δt_pedido`, o que em uma transferência de 3 dias é
26 µs. O limite prático não é esse — é que `Δt(ψ)` é avaliado em `double`.

Os testes (`tests/scientific/test_lambert.cpp`) verificam três coisas
independentes:

1. **Ida e volta pela solução fechada.** Resolver Lambert para `(r⃗₁, r⃗₂, Δt)`,
   depois propagar `(r⃗₁, v⃗₁)` por `Δt` com o propagador de Kepler dos testes, e
   exigir chegar em `r⃗₂`. Esta é a verificação forte: usa dois códigos
   independentes (Lambert e f&g de Kepler) para a mesma órbita.
2. **Consistência com Hohmann.** Para `Δν = 180° − ε` entre duas órbitas
   circulares coplanares e `Δt` igual a meio período da elipse de transferência,
   os `Δv` devem reproduzir as fórmulas de Hohmann (§7 de `navigation.md`).
3. **Simetria.** Resolver de `r⃗₂` para `r⃗₁` no sentido retrógrado dá a órbita
   reversa; a energia específica tem que ser a mesma.

## 5. O que Lambert não é

Lambert dá a órbita de **dois corpos** que liga dois pontos. A trajetória real,
no campo de N corpos com J₂ deste simulador, **não** passa exatamente por
`r⃗₂`. A diferença é a perturbação acumulada ao longo de `Δt` e, para uma
transferência Terra–Lua de 3 dias, chega a milhares de quilômetros.

Portanto Lambert é o **primeiro chute**, não a resposta:

```
plano de Lambert  →  propagar no modelo completo  →  medir o erro na chegada
                  →  corrigir (targeting diferencial)  →  repetir
```

## 6. Targeting diferencial (implementado)

`core/navigation/targeting.hpp` fecha esse laço. Ele trata a trajetória como
caixa-preta: recebe uma função `velocidade de partida → posição de chegada` — que
por dentro executa a queima finita e propaga no modelo completo — e a inverte por
Newton com jacobiana estimada por diferenças finitas (três trajetórias extras por
iteração).

Duas decisões não óbvias, ambas encontradas medindo:

**Busca linear.** O passo de Newton cheio piora o resultado. A primeira versão
desistia na primeira iteração (erro de 267 574 km intacto). Com retrocesso
(1, ½, ¼, …) até o erro efetivamente cair, o mesmo problema converge.

**Passo de diferença finita pequeno.** A chegada de uma injeção translunar se
move ~10⁶ m por m/s de velocidade de partida. Um passo de 0,1 m/s já sai do
regime linear e a "jacobiana" vira uma corda, não uma tangente. Medido no
intercepto lunar:

| passo de diferença finita | erro final |
|---|---|
| 0,5 m/s | 36 km (estagna) |
| 0,1 m/s | 155 km (estagna) |
| 0,001 m/s | **3,5 km (converge)** |

O padrão é 10⁻³ m/s. O ruído numérico não é o limite: o erro do propagador é
~10⁻³ m contra um sinal de ~10³ m nesse passo.

Resultado no cenário `tests/scenarios/lunar-intercept.json`:

```
erro inicial (só Lambert) : 267 573 km
erro final (corrigido)    :       3,5 km      13 iterações, 56 trajetórias
```

### Durante o targeting, a nave atravessa o alvo

O propagador normalmente **para** quando a trajetória entra no raio de um corpo.
Isso é certo para voar uma missão e errado para mirar uma: uma iteração
intermediária que raspa a Lua ainda é um dado útil para a jacobiana, e parar ali
colocaria um penhasco no meio do mapa. Por isso `IntegratorConfig::stop_inside_body`
é desligado durante a correção e religado para o voo final.

### O que ainda falta

O corretor casa a **posição em um instante fixo**. Isso basta para um impacto e é
frágil para um sobrevoo: perto do alvo, a gravidade do próprio alvo torna o mapa
fortemente não linear, e é por isso que 3,5 km é onde ele para. Mirar uma
**altitude de sobrevoo** pede targeting no plano-B (parametrizar a hipérbole de
aproximação em vez da posição), que não está implementado — e a CLI diz isso na
cara quando a trajetória resultante é de impacto.
