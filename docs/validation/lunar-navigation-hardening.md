# Navegação Terra–Lua: diagnóstico, correção e campanha

**Milestone 6.1, Parte A.** Bloqueador P0 do Milestone 6: 18 sucessos estritos
em 100 épocas.

**Resultado: PASS.** 100/100 nas mesmas 100 épocas e **365/365** ao longo de um
ano, com o critério de sucesso muito mais estrito que o anterior. Toda falha
possui classificação; nenhuma falha reporta "solver failed".

Reprodução:

```bash
cmake -S . -B build && cmake --build build -j
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --epochs 100
scripts/lunar_campaign.sh 365 8       # o ano inteiro, em processos paralelos
ctest --test-dir build -R lunar_transfer --output-on-failure
```

---

## 1. O que a campanha do Milestone 6 estava realmente medindo

A primeira coisa foi **não mexer em tolerância nenhuma** (seção 2 do brief) e
reexecutar a campanha antiga guardando a saída completa de cada época. O
resultado não foi o relatado — foi mais informativo:

```text
41 capturas          (o relatório contou 18 "sucessos estritos" porque também
53 não capturadas     exigia que o corretor de posição tivesse convergido)
 6 timeouts
 1 impacto lunar
```

E então a linha que decide tudo. Em **52** das 100 épocas a propagação final
terminou com:

```text
propagation stopped: trajectory entered a body -- trajectory entered Earth
```

seis minutos depois da ignição — **depois** de um estágio de plano B que
reportava `converged`. A correlação com o resultado é perfeita:

| | entrou na Terra | não entrou |
|---|---:|---:|
| capturada | **0** | 41 |
| não capturada | 52 | 7 |

O solver não estava falhando. Estava convergindo — sobre uma trajetória que
atravessa a Terra.

### Por que ele podia fazer isso

As trajetórias de sonda do corretor rodam com `stop_inside_body` desligado, e
isso está certo: um corretor precisa de um mapa suave, e um iterado que raspa o
alvo é um ponto de dado perfeitamente útil. Mas o mapa que ele invertia não
tinha como saber que a cônica de partida que ele estava moldando mergulhava
abaixo da superfície da Terra. Então ele levava o ponto de mira a 0,04 km do
periastro lunar pedido, sobre um arco que a nave não pode voar.

### E por que a geometria era impossível

O pipeline antigo **não tinha busca**. O ponto de partida era onde a órbita de
estacionamento estivesse na época, e o tempo de voo era a constante 4,5 dias.
Com os dois fixos, o ângulo de transferência é o que o calendário mandar:

| época | ângulo | Δv de Lambert | entrou na Terra |
|---|---:|---:|---|
| 2026-01-01 | 170,00° | 3 164 m/s | não |
| 2026-01-05 | 223,09° | 4 640 m/s | **sim** |
| 2026-01-11 | 306,83° | 10 037 m/s | **sim** |
| 2026-01-16 | 9,57° | 12 659 m/s | não |
| 2026-01-23 | 105,87° | 6 458 m/s | não |

Acima de cerca de 215° a solução de Lambert que liga aqueles dois pontos exige
uma velocidade de partida 4,6 a 13 km/s distante da velocidade orbital, apontada
numa direção cuja cônica resultante reentra na Terra. **Não há como corrigir
isso.** Só há sair em outro momento, ou voar por outro tempo — que é o que uma
missão faz.

Os seis timeouts eram um mecanismo à parte e igualmente físico: o estágio 1
mirava no **centro** do corpo, que por definição é um impacto, então as sondas
do estágio 2 começavam passando a 600 m do centro da Lua. Ali a aceleração de
ponto material é 10¹¹ m/s² e o controlador de erro tritura o passo até o piso.
Um dos casos levou 160 s para reportar um stall.

---

## 2. O mapa que explica a campanha inteira

A seção 7 do brief pede um mapa `época de partida × tempo de voo`, classificado.
Ele é barato — tudo até `FEASIBLE` é aritmética de dois corpos — e explica o
Milestone 6 de uma vez:

```bash
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --map
```

```text
320 células, época 2026-01-01

  DEGENERATE_GEOMETRY             10
  DEPARTURE_CONIC_HITS_BODY      264
  FEASIBLE                        46

ramo prógrado   ( . sem solução   x degenerado   E cônica de partida atinge a Terra
                  $ inviável       # factível )
  tof   3.00 d  EEEEEEEEEE##EEEE
  tof   3.25 d  #EEEExEEEEE#EEEE
  tof   3.50 d  #EEEExEEEEE#EEEE
  tof   3.75 d  #EEEExEEEEE#EEEE
  tof   4.00 d  #EEEEEEEEEExEEEE
  tof   4.25 d  #EEEEEEEEEE#EEEE
  tof   4.50 d  #EEEEEEEEEE##EEE
  tof   4.75 d  #EEEEEEEEEE##EEE
  tof   5.00 d  #EEEEEEEEEE##EEE
  tof   5.50 d  #EEEEExEEEE##EEE
```

**82,5 % da grade é invoável**, e é invoável por um motivo só. Uma elipse de
transferência que alcança a Lua tem o perigeu muito abaixo do raio de
estacionamento a menos que o ponto de partida esteja perto desse perigeu — e
Lambert devolve a solução de qualquer jeito. As colunas `#` são os poucos pontos
da órbita de estacionamento de onde se pode sair.

O Milestone 6 voava uma célula por época: a que o calendário entregava. A maior
parte das vezes, um `E`.

---

## 3. A taxonomia

Nada falha como "solver failed" (seção 4). Cada recusa, cada divergência e cada
geometria invoável tem nome, e a campanha conta por categoria.
`core/navigation/lunar_transfer.hpp` define 20 razões; um teste verifica que
nenhuma delas cai no caso `UNCLASSIFIED` e que todas são distintas.

| grupo | razões |
|---|---|
| busca | `NO_LAMBERT_SOLUTION`, `BAD_LAMBERT_BRANCH`, `NO_FEASIBLE_TRAJECTORY` |
| partida | `DEPARTURE_CORRECTOR_DIVERGED`, `DEPARTURE_CORRECTOR_STAGNATED`, `DEPARTURE_CONIC_HITS_CENTRAL_BODY`, `INSUFFICIENT_DEPARTURE_DV` |
| chegada | `INVALID_BPLANE`, `BPLANE_CORRECTOR_DIVERGED`, `LUNAR_IMPACT`, `PERIAPSIS_TOO_HIGH`, `PERIAPSIS_TOO_LOW` |
| captura | `CAPTURE_BURN_TOO_EARLY`, `CAPTURE_BURN_TOO_LATE`, `INSUFFICIENT_CAPTURE_DV`, `POST_BURN_HYPERBOLIC`, `TARGET_ORBIT_NOT_ACHIEVED` |
| execução | `NUMERICAL_FAILURE`, `TIMEOUT` |

Três acrescentadas ao mínimo do brief, e cada uma porque o mínimo as
confundiria:

- **`DEPARTURE_CONIC_HITS_CENTRAL_BODY`** — o mecanismo de 46 das 59 falhas.
  Chamá-la de `BAD_LAMBERT_BRANCH` seria verdade e esconderia que o ramo está
  correto e a **geometria de partida** é que não está.
- **`INSUFFICIENT_DEPARTURE_DV`** — simétrica da de captura.
- **`TARGET_ORBIT_NOT_ACHIEVED`** — capturado, mas não na órbita pedida
  (seção 13).

`TIMEOUT` é contado em **passos do integrador**, não em segundos de relógio, para
que a mesma época classifique igual numa máquina rápida e numa lenta. Os seis
"timeouts" do Milestone 6 eram todos um mecanismo só, e reportá-los como "passou
de 30 segundos" não dizia nada sobre ele.

---

## 4. O que mudou

### 4.1 Busca, onde antes havia uma escolha do calendário

`departure_window` × `time_of_flight_days` × `{prógrado, retrógrado}`. O padrão
são 16 pontos de partida ao longo de duas horas — pouco mais de uma revolução da
órbita de 400 km — e dez tempos de voo entre 3,0 e 5,5 dias. 320 geometrias por
época contra a única do Milestone 6.

Ambos os ramos são tentados e o escolhido é **registrado**: "a primeira solução
que o solver devolveu" não é uma razão (seção 6). Nas 365 épocas o ramo prógrado
venceu todas as vezes, e isso é um resultado, não um pressuposto — um ramo
retrógrado a partir de uma órbita de estacionamento prógrada paga uma mudança de
plano que nenhum ganho de geometria compensa.

### 4.2 O perigeu da cônica de partida: um preço, não uma recusa

O periastro da cônica pós-injeção em torno do corpo central deveria limpar a
superfície:

```text
r_p(r1, v_lambert) >= R_terra + 120 km
```

Dois corpos, sem propagação, antes de qualquer corretor ver o candidato. Entre
249 e 288 das 320 células por época ficam abaixo desse piso.

**A primeira versão recusava esses candidatos**, que é o que a seção 8 parece
pedir com "hard constraints". A ablação da seção 5 mostrou que isso está errado,
e por uma razão que vale mais do que a correção: a peneira lê a cônica **não
corrigida**. O corretor depois move a velocidade de partida em 166 a 1 853 m/s
(mediana 341), e uma correção desse tamanho rotineiramente levanta um perigeu que
começou abaixo da superfície. Na configuração do Milestone 6 — partida fixa,
tempo de voo fixo, quatro geometrias ao todo — recusar dá 15 % de sucesso e
precificar dá **50 %**.

Hoje o déficit entra na **função de custo** a 0,01 por metro: um déficit de
1 000 km custa 10 000, o que põe o candidato atrás de qualquer um voável e ainda
permite tentá-lo quando não há mais nada. A restrição rígida continua existindo e
continua sendo aplicada — sobre o **arco voado**, onde `stop_inside_body` a
reporta e o caso é classificado `DEPARTURE_CONIC_HITS_CENTRAL_BODY`. É ali que
"sem colisão" pertence, porque é ali que uma colisão é um fato e não uma
extrapolação.

`refuse_departure_conic_below_floor` (CLI: `--refuse-departure-conic`) reproduz a
leitura estrita, e um teste exercita as duas.

### 4.3 O estágio 1 mira num sobrevoo, não num centro

O ponto de mira do estágio 1 é a posição do alvo **deslocada pelo vetor B** que
o periastro pedido implica, construído a partir do `v∞` da chegada de Lambert. A
estimativa não precisa ser boa — o estágio 2 mede e remira — só precisa ser um
sobrevoo em vez de uma colisão. É isso que elimina o mecanismo dos timeouts na
origem.

A tolerância do estágio 1 também mudou, e não por conveniência: seu alvo é a
posição **na época nominal de chegada**, e a aproximação máxima não acontece
nessa época, então o resíduo que ele tenta zerar tem um piso de alguns milhares
de quilômetros que iteração nenhuma remove. Medido com a tolerância de 10 km que
a CLI usava: 25 iterações, 192 propagações, parado em 4 560 km, toda vez. O
trabalho não estava sendo gasto num problema difícil — estava sendo gasto no
problema errado. Com a tolerância do estágio 1 em 10 000 km, a época leva **0,5
a 1 s em vez de 30 s**, e o estágio 2 continua fazendo toda a precisão.

Consequência que vale registrar: o estágio 1 converge em 330 das 365 épocas, e a
campanha tem 365 sucessos. **A convergência do estágio 1 não é condição de
sucesso** — o Milestone 6 a contava como se fosse, e foi por isso que reportou 18
em vez de 41.

### 4.4 Uma função de custo, e uma só regra de seleção

```text
custo = w1 · Δv_partida + w2 · Δv_inserção + w3 · |erro de periastro| + w4 · |correção|
```

com restrições rígidas — sem colisão, captura possível, combustível suficiente,
periastro na faixa — que **recusam** em vez de encarecer (seção 8).

Isto custou uma iteração de projeto que vale contar. A primeira versão ordenava
os candidatos por quão perto o voo de triagem chegava do alvo, e tentava os dois
melhores. Sob o modelo **impulsivo**, onde nada pune uma queima enorme, isso
escolheu partidas de **18 km/s** porque elas por acaso voavam mais perto: a
mediana do Δv de partida saiu em 10 952 m/s contra 3 167 para as mesmas épocas
sob queima finita.

Voar mais perto não é uma razão para preferir uma trajetória. É uma **previsão**
de quanta correção ela vai precisar — e a função de custo já precifica correção.
A distância é convertida na correção que implica (uma chegada translunar se move
cerca de 10⁶ m por m/s de velocidade de partida, medido e registrado em
`targeting.hpp`) e somada ao mesmo custo. Uma ordenação, uma regra, e a regra é a
da seção 8. Depois da correção: mediana 3 159 m/s, máximo 3 206 m/s.

### 4.5 A órbita pedida, não o sinal de uma energia

```text
periastro >= 80 km    apoastro <= 120 km    e <= 0.01
```

`specific_energy < 0` é satisfeito por uma órbita com periastro dentro da Lua e
apoastro além da Terra. A verificação de energia continua lá — e é feita nos
**dois lados** da queima, independentemente (seção 10) — mas o sucesso é a órbita.

---

## 5. Ablação: qual mudança fez o quê

Cada linha são as mesmas 100 épocas, com uma configuração diferente.

| configuração | janela de partida | grade de tof | cônica de partida | sucesso |
|---|---|---|---|---:|
| reprodução do Milestone 6 | fixa | 4,5 d | recusa | **15 %** |
| a mesma, com a cônica precificada | fixa | 4,5 d | preço | **50 %** |
| + grade de tempo de voo | fixa | 10 valores | preço | 52 % |
| + janela de partida | 2 h, 16 amostras | 4,5 d | preço | **100 %** |
| busca completa | 2 h, 16 amostras | 10 valores | preço | **100 %** |

As quatro primeiras linhas usam `--flown 2`, para que a comparação entre elas seja
entre iguais.

E a distribuição de falhas, que diz mais que a taxa:

```text
Milestone 6, cônica recusada        84 NO_FEASIBLE_TRAJECTORY
                                     1 DEPARTURE_CORRECTOR_STAGNATED

Milestone 6, cônica precificada     30 DEPARTURE_CORRECTOR_STAGNATED
                                    17 DEPARTURE_CONIC_HITS_CENTRAL_BODY
                                     1 LUNAR_IMPACT, 1 INVALID_BPLANE,
                                     1 NO_FEASIBLE_TRAJECTORY

+ grade de tempo de voo             35 DEPARTURE_CORRECTOR_STAGNATED
                                    13 DEPARTURE_CONIC_HITS_CENTRAL_BODY
```

Quatro leituras, e as duas últimas eu não esperava:

1. **A liberdade de ponto de partida é o botão dominante**: 52 % → 100 %. A
   reprodução do Milestone 6 dá 15 %, contra os 18 sucessos estritos relatados —
   o pipeline novo reproduz o antigo quando lhe tiram a busca, o que é a melhor
   evidência de que o diagnóstico está certo.
2. **A grade de tempo de voo quase não ajuda sozinha** (50 % → 52 %). Com o ponto
   de partida fixo, o que manda é o ângulo de transferência, e mais tempos de voo
   não consertam isso. Ela vale pelo que faz junto com a janela: a busca completa
   roda seis vezes mais rápido que a busca só com janela, porque não precisa
   forçar geometrias marginais.
3. **A peneira analítica da cônica de partida recusava transferências voáveis**:
   15 % → 50 % só por precificá-la em vez de recusá-la. Ela custava mais do que
   rendia exatamente na configuração onde a busca é pobre, que é onde ela parecia
   mais necessária. Vale ler a distribuição junto: recusando, 84 épocas viram
   `NO_FEASIBLE_TRAJECTORY`, que não diz nada; precificando, elas viram
   `DEPARTURE_CORRECTOR_STAGNATED` e `DEPARTURE_CONIC_HITS_CENTRAL_BODY`, que
   dizem o que aconteceu de fato.
4. **Com a busca completa a peneira não muda nada**: as execuções com e sem ela
   escolheram a **mesma** trajetória em 100 de 100 épocas. Os ramos ruins deste
   cenário são também os caros, e a ordenação por custo já os deixava de fora.
   Ela não é redundante como **classificação** — é o que permite ao mapa da
   seção 2 explicar a geometria — e deixaria de ser redundante para uma nave
   cujos ramos ruins não fossem também os caros.

Registrar que uma das "correções" era na verdade um defeito é o ponto da seção 2
do brief.

---

## 6. Planejamento e execução, separados

A seção 5 pede quatro estágios e a seção 11 pede a comparação. As três executam a
**mesma geometria**, fixada pela busca, e cada uma corrige e voa no seu próprio
modelo. Sem isso não é comparação: deixando cada modelo buscar sozinho, o
impulsivo escolheu uma geometria que pede 816 m/s de captura e a de queima finita
uma que pede 874, e os 7 % entre as duas não dizem nada sobre queimas finitas.

```text
  época                  modelo         e          r_p      r_a     queima   dv     lag méd/pico   resultado
  2025-12-31T23:58:51  IMPULSIVE     0.000003    99.87    99.88     0.00 s  835.09   0.000  0.000   SUCCESS
                       FINITE_BURN   0.001732    96.41   102.78    83.51 s  835.44   0.000  0.000   SUCCESS
                       AUTOPILOT     0.010468    79.89   118.34    83.53 s  835.60   1.650  2.064   PERIAPSIS_TOO_LOW
```

- **IMPULSIVE** entrega 99,87 × 99,88 km, `e = 3e-6`. O planejador está certo, e
  isso não é uma opinião: é o que resta quando se tira o erro de controle.
  Campanha de 100 épocas em modo impulsivo: 100/100, periastro 99,1 a 100,0 km,
  `e` entre 2e-6 e 6e-6.
- **FINITE_BURN** paga `e = 0,0017`: 835 m/s distribuídos em 83,5 s.
- **AUTOPILOT** paga `e = 0,0105` e sai da especificação, com o periastro em 79,9 km
  contra o piso de 80.

**Zero falhas de planejador, uma de execução por época**, com o mecanismo nomeado.

### O que o autopilot custa, e por quê

`GuidanceMode::Hull` foi acrescentado ao executor: o motor aponta ao longo do eixo
+x do casco, integrado, em vez de ao longo da lei de guiagem ideal. O que sobra
entre FINITE_BURN e AUTOPILOT é exatamente o atraso de rastreamento do
controlador PD — e o atraso é medido em cada passo aceito **dentro** da queima,
ponderado pelo passo.

Isso também exigiu consertar a medição. A primeira versão lia a atitude do estado
no **fim** da propagação, duas órbitas depois da queima, e dava 1,5° em qualquer
largura de banda do controlador. Isso deveria ter entregado o erro na hora: o
atraso de um PD vai com `1/ω_n`, e um número que não se mexe quando `ω_n`
quadruplica não está medindo o atraso.

Com a medição certa, varrendo `ω_n`:

| `ω_n` [rad/s] | atraso médio | atraso de pico | atraso × `ω_n` | órbita resultante | `e` | resultado |
|---:|---:|---:|---:|---|---:|---|
| 0,025 | 3,287° | 4,100° | 0,0822 | 61,6 × 144,0 km | 0,0224 | `PERIAPSIS_TOO_LOW` |
| 0,050 | 1,667° | 2,074° | 0,0834 | 81,4 × 119,6 km | 0,0104 | `TARGET_ORBIT_NOT_ACHIEVED` |
| 0,100 | 0,830° | 1,025° | 0,0830 | 92,1 × 108,1 km | 0,0044 | **SUCCESS** |
| 0,200 | 0,414° | 0,512° | 0,0828 | 97,5 × 102,5 km | 0,0013 | **SUCCESS** |
| 0,400 | 0,207° | 0,255° | 0,0828 | 99,5 × 100,4 km | 0,0003 | **SUCCESS** |

O produto `atraso × ω_n` é constante a 0,5 % ao longo de uma faixa de 16× — que é
a assinatura `1/ω_n` de um atraso de rastreamento de PD, e não uma coincidência
numérica. A constante medida, `0,0828 deg·rad/s`, implica `2ζ ω_efetivo =
1,446e-3 rad²/s`, isto é `ω_efetivo = 7,23e-4 rad/s`. Para comparação, a taxa
angular no periastro vale `1,339e-3 rad/s` antes da queima e `8,89e-4 rad/s`
depois: o alvo que o controlador persegue gira a algo pouco abaixo da taxa
pós-queima, e o número medido cai onde deveria sem ter sido ajustado para tal.

O **`ω_n = 0,05` é o valor do cenário** (`pointing_controller.hpp`), escolhido
para que uma manobra comandada pelo piloto pareça suave. É ele que a comparação
acima usa, porque o que o modelo AUTOPILOT deve reportar é o que a nave embarcada
faz. A partir de `ω_n = 0,1` a captura entra na especificação — e o RCS tem a
autoridade de sobra para isso: doze propulsores em seis binários num braço de
2 m dão 720 N·m sobre um tensor de cerca de 1 500 kg·m², ou 0,48 rad/s². O
conserto é enrijecer o controlador durante a queima de captura; ele não foi
aplicado ao cenário, e por isso o AUTOPILOT continua reprovando na tabela da
seção 6.

O periastro da órbita lunar de 100 km tem `ω_orbital = v/r = 1633/1,837e6 =
8,9e-4 rad/s`, e o atraso de regime de um PD criticamente amortecido seguindo uma
rampa é `2ζω/ω_n`. O que quebra a captura não é a perda de cosseno — 1,65° custa
0,04 % de 835 m/s — é a componente **lateral**: `sen(1,65°) = 2,9 %` do empuxo
empurrando fora do plano, o que gira o vetor velocidade e é o que leva
96 × 103 km a 80 × 118 km.

Ou seja: **o planejador orbital não é o problema** (seção 11), e a parte do
"motor real" que é o problema tem nome, número e uma fórmula que a explica.

---

## 7. Oberth: onde a queima tem de acontecer

```bash
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --oberth-sweep
```

```text
  offset     r no meio       v no meio    queima     e depois    periastro   apoastro   resultado
     [s]          [km]          [m/s]        [s]                      [km]       [km]
  -120.0         (impacto)                                                             LUNAR_IMPACT
   -60.0      1840.986       2045.125     82.652     0.043158       22.869    181.663   PERIAPSIS_TOO_LOW
   -30.0      1838.310       2046.589     82.652     0.020762       62.437    138.757   PERIAPSIS_TOO_LOW
     0.0      1836.842       2051.572     83.510     0.001732       96.414    102.778   SUCCESS
    30.0      1837.926       2046.588     82.652     0.024120       56.262    144.929   PERIAPSIS_TOO_LOW
    60.0      1840.219       2045.123     82.652     0.046510       16.700    187.825   PERIAPSIS_TOO_LOW
   120.0         (impacto)                                                              LUNAR_IMPACT
```

A janela é estreita e **simétrica**, o que confirma que o que está sendo medido é
a geometria de Oberth e não um viés de tempo: ±30 s já leva a excentricidade de
0,0017 a 0,021 e derruba o periastro a 56–62 km; ±120 s põe a órbita subsequente
dentro da Lua.

Nas campanhas, o raio no meio da queima fica entre 1 835,4 e 1 838,6 km — o
periastro previsto ±2 km — e um teste fixa isso: uma queima de 90 s centrada no
periastro não pode se afastar dele por mais de 1 % de 1 837 km.

---

## 8. As campanhas

### 8.1 As 100 épocas originais

| indicador | Milestone 6 | Milestone 6.1 |
|---|---:|---:|
| sucesso | 18 estritos / 41 capturas | **100** |
| definição de sucesso | `e < 1` | periastro ≥ 80 km, apoastro ≤ 120 km, `e ≤ 0,01` |
| cônica de partida na Terra | 52 | 0 |
| timeouts | 6 | 0 |
| impactos lunares | 1 | 0 |

```text
                                       min        mediana          p95           max
  erro de periastro do sobrevoo    0.001949      0.190605     1.220962      1.884245  km
  delta-v de partida            3102.798846   3180.281597  3296.296722   3353.581082  m/s
  delta-v de inserção            797.125031    830.881070   900.216420    919.138832  m/s
  excentricidade final             0.001540      0.001714     0.002085      0.002194
  altitude de periastro           95.102953     96.532458    97.237276     97.587216  km
  altitude de apoastro           101.125560    103.022739   103.864717    104.217047  km
  combustível usado                8.693805      8.955220     9.145809      9.323655  kg
  tempo de voo                     3.000000      3.750000     5.000000      5.500000  d
  ângulo de transferência        169.243612    176.169536   177.856681    177.909100  deg
  tempo por época                  0.754255      1.768972     4.110245      8.706217  s
```

### 8.2 O ano inteiro — 365 épocas, uma por dia

**365/365.** 804 s num único núcleo; `scripts/lunar_campaign.sh 365 8` faz o
mesmo em processos paralelos.

```text
                                       min        mediana          p95           max
  erro de periastro do sobrevoo    0.000048      0.168101     1.184177      1.925130  km
  delta-v de partida            3097.706862   3237.555617  3525.316796   4014.723400  m/s
  delta-v de inserção            796.004653    831.320934   905.443902    920.923848  m/s
  excentricidade final             0.001534      0.001716     0.002113      0.002207
  altitude de periastro           94.465286     96.590808    97.175801     97.867594  km
  altitude de apoastro           101.125560    103.033270   103.901625    104.321938  km
  combustível usado                8.672008      9.085758     9.671385     10.725429  kg
  tempo de voo                     3.000000      3.750000     5.000000      5.500000  d
  ângulo de transferência        162.786595    174.194999   177.598958    177.979986  deg
  tempo por época                  0.725708      1.922796     4.030082      9.713917  s
```

Verificações independentes sobre as 365:

- `ε > 0` antes da queima em 365/365 e `ε < 0` depois em 365/365 (seção 10),
  cada uma calculada do estado relativo e não inferida da excentricidade;
- `ε` antes e `v∞²/2` concordam até o arredondamento — duas grandezas obtidas de
  caminhos diferentes;
- perigeu da cônica de partida entre 268 e 407 km, sempre acima do piso de 120 km;
- raio no meio da queima de captura sempre dentro de 1 % do periastro;
- o estágio 2 do plano B convergiu em 365/365; o estágio 1, em 330/365, e isso
  não impediu nenhum sucesso.

Dados completos: [`lunar-navigation-campaign-v2.csv`](lunar-navigation-campaign-v2.csv),
uma linha por época com as ~90 colunas que a seção 3 pede.

---

## 9. O que continua sendo verdade e não é bom

Registrado porque a regra do brief é explicar, não maximizar verde.

- **A inclinação da órbita lunar final não é controlada.** Ela sai entre 0,1° e
  30,2° ao longo do ano. Ninguém pediu uma inclinação — a especificação da seção
  13 não a menciona — mas ela varia assim porque a busca escolhe ângulos de
  transferência entre 163° e 178°, e perto de 180° o plano da transferência é mal
  condicionado: os dois vetores de posição são quase antiparalelos e o plano que
  eles definem gira muito com pouco. Controlar a inclinação é o problema da
  [otimização do plano B](../physics/b-plane-optimization.md) e não foi feito.
- **A função de custo escolhe perto de 180°** porque é ali que a transferência é
  mais barata, e o custo não precifica condicionamento. Isso funciona — 365/365 —
  e é uma escolha que vale reexaminar se a inclinação passar a importar.
- **A restrição de combustível nunca morde.** A nave do cenário é um torch drive
  com 26 942 km/s de orçamento; `INSUFFICIENT_DEPARTURE_DV` e
  `INSUFFICIENT_CAPTURE_DV` existem, são verificadas, e nenhuma época chegou perto
  de acioná-las. A campanha **não** testa margem de combustível.
- **O autopilot não atinge a especificação** com os ganhos do cenário
  (`ω_n = 0,05`). Diagnosticado na seção 6; a correção — enrijecer o controlador,
  polarizar a queima, ou acrescentar uma queima de trim — não foi implementada.
- **Uma só órbita de estacionamento.** Todas as 365 épocas partem da mesma órbita
  de 400 km no plano da Lua. O que varia é a época, não a geometria inicial.
- **`NO_FEASIBLE_TRAJECTORY` nunca ocorreu** nas campanhas completas, então a
  cláusula da seção 15 sobre casos matematicamente inviáveis não foi exercitada
  por uma época real — só pelas ablações, onde ocorre por construção.

---

## 10. Critério de saída

A seção 15 pede ≥ 95 % para geometrias onde existe trajetória dentro dos limites,
e idealmente 100 % das factíveis, deterministicamente.

| | |
|---|---|
| 100 épocas originais | **100 %** |
| 365 épocas, uma por dia | **100 %** |
| 100 épocas, modelo impulsivo | **100 %** |
| `NO_FEASIBLE_TRAJECTORY` nas campanhas | 0 |
| determinismo | sem aleatoriedade em nenhum ponto: a busca é uma grade, os corretores são Newton com diferenças finitas, e duas execuções da mesma época produzem o mesmo CSV |

**NAVIGATION: PASS.**

---

## Evidências

- Módulo: `core/navigation/lunar_transfer.{hpp,cpp}`
- Ferramenta: `tools/lunar-campaign/main.cpp`, `scripts/lunar_campaign.sh`
- Testes: `tests/scientific/test_lunar_transfer.cpp`
- Dados: [`lunar-navigation-campaign-v2.csv`](lunar-navigation-campaign-v2.csv)
- Campanha anterior: [`lunar-mission-campaign.md`](lunar-mission-campaign.md)
- Plano B: [`../physics/b-plane.md`](../physics/b-plane.md),
  [`../physics/b-plane-optimization.md`](../physics/b-plane-optimization.md)
- Lambert: [`../physics/lambert.md`](../physics/lambert.md)
