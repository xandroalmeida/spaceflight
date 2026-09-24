# Transferência direta: empuxo contínuo guiado

O modo RELATIVÍSTICO ([`propulsion-model.md`](propulsion-model.md) §4.7) empurra
a 100 g. Com isso a rota mais rápida para qualquer destino deixa de ser uma cônica
de Lambert — alguns segundos de queima e dias de voo balístico — e passa a ser
**empurrar o caminho inteiro**: acelerar até perto do meio, virar, frear. Este
documento é a derivação do que o computador de bordo faz quando o motor está em
RELATIVÍSTICO e o piloto pede um plano.

## 1. O problema

Levar a nave, a partir do estado atual `(r₀, v₀)`, a um estado de chegada
prescrito — posição e velocidade **relativas ao destino** — num instante `t_f`,
com empuxo limitado a `F_max` e massa que diminui.

O destino se move (a Lua a 1 km/s em torno da Terra, Marte a 24 km/s em torno do
Sol), e a sua posição em `t_f` é **conhecida exatamente**: é a efeméride. A
gravidade sobre a nave é a do modelo inteiro, N corpos e J2.

## 2. A lei: energia mínima com tempo fixo e aceleração final nula

Para o integrador duplo `r̈ = a`, o controle que leva `(r, v)` a `(r_f, v_f)` em
exatamente `t_go` segundos minimizando `∫|a|² dt` é linear no tempo:
`a = 6 ZEM/t_go² − 2 ZEV/t_go`, com

```
ZEM = r_f − (r + v t_go)          erro de posição se o motor desligasse agora
ZEV = v_f − v                      erro de velocidade idem
```

**Essa lei não serve para voar, e o primeiro voo mostrou por quê.** Ela termina
no seu pico — ainda freando forte em `t_go = 0` — e o motor tem de cortar antes
da singularidade: 30 s antes deixaram 30 s × 700 m/s² ≈ **28 km/s** sem frear, e
a nave atravessou a Lua. Exigindo também `a(t_f) = 0`, o perfil passa a
`α(T−τ) + β(T−τ)²`; impondo `∫a = ZEV` e `∫(T−τ)a = ZEM` sai `αT³ = 18 ZEV T −
24 ZEM` e `βT⁴ = 36 ZEM − 24 ZEV T`, e a aceleração **agora** é

```
a   = 12 ZEM / t_go²  −  6 ZEV / t_go          (e a → 0 em t_f)
```

Recalculada a cada instante — no caso, a cada avaliação do modelo de forças pelo
integrador —, ela é uma **realimentação**: qualquer coisa que o modelo ignorou
(a gravidade, a saturação do motor, a massa que cai) aparece como `ZEM` e `ZEV`
residuais e é corrigida no instante seguinte. Os ganhos crescem como `1/t_go` e
`1/t_go²`, e é isso que leva o erro terminal a zero.

**A gravidade não entra no `ZEM`.** É uma perturbação que a realimentação absorve:
no pior caso (a partida da órbita baixa) são 8,7 m/s² contra 981 do motor, 0,9 %,
e cai com o quadrado da distância em minutos. A lei é contínua em `r`, `v` e `t`
— o que o integrador adaptativo exige de uma força.

Num voo unidimensional do repouso ao repouso: `12d/T²` na partida, zero a
**um terço** do caminho — onde a nave vira —, `−4d/T²` no pico da frenagem e zero
na chegada. O pico é 3 vezes o de um perfil "acelera–vira–freia" de aceleração
constante, e a viagem 73 % mais longa que ele; em troca a lei é suave, se corrige
sozinha e termina com o motor quase parado, que é o que permite cortá-lo.

## 3. De aceleração coordenada a empuxo próprio, exatamente

A lei fala em aceleração **coordenada** `a = dv/dt`. O motor produz uma força no
referencial de repouso da nave (`proper_thrust`), e o integrador, que carrega
`u = γv`, a aplica como ([`relativistic-propulsion.md`](relativistic-propulsion.md) §2.3)

```
du/dt = M n F / (γ m),     M = I + (γ − 1) β̂β̂ᵀ
```

Derivando `u = γv`: `du/dt = γ a + γ³ (v·a) v / c²`. Invertendo `M`
(`M⁻¹ = I − ((γ − 1)/γ) β̂β̂ᵀ`):

```
F = γ m M⁻¹ (du/dt)
```

que se desdobra nas duas componentes conhecidas: `F∥ = γ³ m a∥` (a "massa
longitudinal") e `F⊥ = γ² m a⊥`. Não é uma aproximação: o empuxo pedido produz
exatamente a aceleração coordenada comandada. Na cinemática newtoniana, que o
planejador de Lambert ainda usa, `F = m a`; o executor sabe em qual está.

Se `|F| > F_max` o motor entrega `F_max` na mesma direção. É o limite do motor,
não um grampo na física: a realimentação vê o atraso e o recupera.

## 4. O tempo de voo

`t_f` é escolhido, não buscado numa grade: a demanda da lei **na ignição** — o
pico do perfil — tem de ser 75 % do que o motor dá com a nave cheia. `|a(t₀; T)|` cai com `T` (como
`12d/T²` para `T` grande), então uma bissecção em `log T` resolve, e cada avaliação
pergunta à efeméride onde o destino estará em `t₀ + T` — o destino que se move é
parte da equação, não uma correção. Os 25 % de margem cobrem a gravidade e a
velocidade inicial.

Da órbita baixa, a 1º de janeiro de 2026, voado no modelo inteiro
(`tests/scientific/test_direct_transfer.cpp`):

| destino | tempo de voo | pico de `β` | propelente | erro na chegada | órbita, 1 volta depois |
|---|---|---|---|---|---|
| Lua (100 km) | 39,7 min | 0,00088 | 37 kg | 0,2 m, 0,34 m/s | 99,7 × 100,5 km |
| Mercúrio (500 km) | 16,1 h | 0,021 | 869 kg | 0,03 m | 499,9 × 500,0 km |
| Vênus (500 km) | 18,0 h | 0,023 | 965 kg | 0,08 m | 499,6 × 500,0 km |
| Marte (500 km) | 21,3 h | 0,028 | 1 141 kg | 0,03 m | 499,8 × 500,0 km |
| Fobos, Deimos (20 km) | 21,3 h | 0,028 | 1 140 kg | 0,01 m | estacionamento |

## 5. A partida

A lei não sabe que a Terra existe: aponta para o destino em quase linha reta. Da
órbita baixa, metade das posições da órbita têm a Terra no caminho. A partida é
buscada ao longo de **uma órbita** a partir de agora (a nave voada balisticamente
no modelo inteiro, amostrada): a primeira posição de onde o segmento até o
destino em `t_f` passa a mais de `R_origem + 300 km` do centro da origem. O voo de
verificação (§7) tem a parada por impacto ligada, e uma partida que ainda assim
bata é recusada e a seguinte é tentada.

## 6. A chegada

**Órbita circular** na altitude pedida, no plano que contém a direção de chegada
e é prógrado em relação à órbita do destino em torno do seu primário: posição
`r_orb n̂` do lado de onde a nave vem (então a aproximação não atravessa o
destino) e velocidade `√(GM/r_orb)` ao longo de `ĥ × n̂`.

**Estacionamento**, quando a órbita pedida sai da esfera de Hill do destino
(`r_orb > 0,5 R_Hill`). Fobos tem `R_Hill` = 16,4 km do centro, e uma "órbita" a
100 km de Fobos é uma órbita de Marte. Nesse caso a chegada é à mesma distância,
com velocidade relativa **zero**: um ponto de encontro, que a gravidade de Marte
desfaz devagar depois. O plano diz qual dos dois é.

A lei diverge em `t_go → 0`, então o motor corta **1 s** antes da chegada (ou
0,1 % de `T`, se for menos). Com a aceleração final levada a zero, o último
segundo vale bem menos de 1 m/s: é o erro de velocidade medido, e o resto é
balístico.

## 7. Como vira um plano

Uma **única** queima guiada (`GuidanceMode::Rendezvous`), escrita como **duas
pernas** encostadas: `injection` até um terço do tempo — onde o empuxo vira — e
`insertion` dali até o corte. A lei é a mesma nas duas, então o empuxo é contínuo na emenda; os nomes
existem para que o resto do sistema — as fases da missão, o painel, o registro do
resultado — funcione sem saber que o plano é de outro tipo: o terço que acelera
é a injeção, o resto, que freia, é a captura.

O plano é **voado antes de ser oferecido**: o modelo de forças inteiro, o
executor com esta lei, cinemática relativística, até uma órbita depois da
chegada. O que o painel mostra — tempo de voo, Δv, propelente, órbita final — é
o que esse voo produziu, não uma estimativa.

## 8. O que isto não faz

* Não otimiza propelente: a lei minimiza `∫|a|²`, não `∫|a|`, e o perfil com
  aceleração final nula gasta mais que o linear. Numa viagem a Marte são 1,1 t de
  19, e o painel mostra quanto.
* Não otimiza tempo: 75 % de demanda no pico e o perfil quadrático deixam a
  viagem bem mais lenta que um "acelera–vira–freia" a 100 g cravados. É o preço
  de uma lei que se corrige sozinha e termina com o motor quase parado.
* Não desvia de corpos no meio do caminho além da origem (§5). Um planeta entre a
  nave e o destino fica para uma versão com restrições de trajetória.
* Os destinos são os que o diretório aceita hoje (Mercúrio, Vênus, Lua, Marte,
  Fobos, Deimos). De Júpiter em diante os kernels só dão o baricentro do sistema,
  e o projeto recusa isso como destino.
