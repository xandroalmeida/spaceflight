# Relatório do Milestone 6.2

**Data:** 14 de setembro de 2026
**Escopo:** integração do planejador em produção e endurecimento do controle de voo
**Resultado global:**

```text
PLANNER INTEGRATION:   PASS
CAMPAIGN (public path): PASS   365/365
AUTOPILOT HARDENING:   PASS
FULL MISSION REGRESSION: PASS
EXECUTION CAMPAIGN:    PASS
```

---

## Resumo executivo

O Milestone 6.1 fechou a navegação Terra–Lua com 365 capturas em 365 épocas. O
que este milestone descobriu ao tentar usar esse resultado é que ele descrevia
software que o jogador nunca executava.

Havia **dois planejadores**. A campanha media
`core/navigation/lunar_transfer.hpp`. A tecla **J** executava
`godot/gdextension/src/mission_planner.cpp` — 532 linhas com a sua própria busca
de partida, a sua própria grade de tempo de voo, o seu próprio corretor de plano
B e a sua própria regra de custo. E as duas já discordavam onde mais importava:
o caminho do jogo **não tinha a triagem da cônica de partida**, que é o mecanismo
por trás de 46 das 59 falhas do Milestone 6.

As 469 linhas de astrodinâmica do GDExtension foram **apagadas**, não movidas. O
que sobrou é uma ponte de 81 linhas cuja única aritmética é subtrair a posição do
corpo central. Campanha e jogo entram pela mesma porta —
`sf::navigation::plan_lunar_transfer` — com o mesmo tipo de requisição.

A campanha foi refeita por esse caminho público: **365/365**, e
**bit-a-bit idêntica** à anterior em todas as catorze colunas comparadas.

O autopiloto, que entregava `e ≈ 0,0105` contra `e ≈ 0,0017` das queimas finitas,
teve o mecanismo identificado em forma fechada (o atraso de rastreamento de um PD
seguindo uma rampa, `θ = 2ζω/ω_n`, que prevê o pico medido com 1,4 % de erro), varrido
em seis larguras de banda e especificado.

| Área | Milestone 6.1 | Milestone 6.2 | Evidência |
|---|---|---|---|
| Planejador único | **FAIL** (dois) | **PASS** (um) | [navigation-integration](../architecture/navigation-integration.md) |
| Campanha 365 | PASS (caminho científico) | **PASS** (caminho público) | [campanha CSV](lunar-navigation-campaign-v2.csv) |
| Autopiloto | e ≈ 0,0105, sem especificação | **PASS** | [autopilot-hardening](autopilot-hardening.md) |
| Missão completa | sem regressão | **PASS** | [full-mission-regression](full-mission-regression.md) |
| Predito × realizado | não registrado | **PASS** | este documento, §6 |
| Campanha de execução | não existia | **PASS** | [CSV](mission-execution-campaign.csv) |
| Combustível insuficiente | nunca alcançável | **PASS** | §8 |
| Cobertura gráfica | implícita | **declarada** | [graphics-compatibility](graphics-compatibility.md) |

---

## 1. Uma implementação autoritativa (§§1–5)

### O que existia

```text
tools/lunar-campaign  →  core/navigation/lunar_transfer.hpp     ← medido
Godot, tecla J        →  godot/.../mission_planner.cpp          ← executado
```

As duas implementações não compartilhavam nada. E já discordavam em algo
mensurável: o caminho do jogo **não tinha a triagem analítica da cônica de
partida**, que é o mecanismo por trás de 46 das 59 falhas do Milestone 6. O jogo
continuava com o defeito que o Milestone 6.1 havia corrigido no core.

### O que existe agora

```text
UI (main.gd)
  → SpaceflightSimulation::plan_transfer      simulation_node.cpp
  → spaceflight_godot::plan_transfer          mission_planner.cpp  (81 linhas)
  → sf::navigation::plan_lunar_transfer       core/navigation/mission_planner.hpp
  → sf::navigation::plan_and_fly              core/navigation/lunar_transfer.hpp

tools/lunar-campaign
  → sf::navigation::plan_lunar_transfer       ← a mesma porta
```

`mission_planner.cpp` do GDExtension: **532 → 81 linhas**. As 469 linhas de
astrodinâmica foram **apagadas**, não movidas — já existia uma implementação
melhor delas. A única aritmética que sobrou na ponte é subtrair a posição do
corpo central do estado absoluto da simulação.

Detalhes da arquitetura, da API e do que o Godot deixou de poder escolher:
[navigation-integration.md](../architecture/navigation-integration.md).

---

## 2. O teste que impede a divergência (§6)

`tests/scientific/test_planner_equivalence.cpp`, três verificações, todas
passando.

**A pergunta.** A requisição que a ponte constrói, campo a campo, contra a que a
campanha constrói — para **os três modelos de execução**, porque um desvio de
tradução num modelo que ninguém voou hoje continuaria invisível até o dia em que
alguém o voasse. 40 campos, incluindo os dez valores da grade de tempo de voo.

**A resposta.** O mesmo estado, época, nave e destino, voados ponta a ponta pelos
dois chamadores, com **tolerância zero**:

```text
departure time · time of flight · Lambert branch · injection Δv
B-plane target · capture Δv · predicted periapsis · predicted final orbit
+ as duas manobras (ignição, duração, throttle, guiagem, direção)
+ a lista de alternativas e as inclinações previstas
```

Zero, e não um épsilon, porque não são dois métodos que deveriam concordar: é uma
função chamada duas vezes com argumentos que devem ser iguais. Um épsilon seria
onde um desvio real se esconderia.

**A forma.** O fonte da ponte é lido e nove símbolos são proibidos nele —
`solve_lambert`, `correct_departure`, `b_plane_from_state`, `aim_for_periapsis`,
`plan_insertion`, `maneuver_for_delta_v`, `run_mission`, `propagate`,
`Trajectory` — mais um limite de 200 linhas. A primeira verificação pega um
desvio na resposta; esta pega um desvio na **forma**, que é o que acontece
primeiro.

A ponte compila dentro do binário de teste porque o seu cabeçalho não tem Godot
dentro. Isso foi uma escolha deliberada e é o que torna a afirmação verificável:
o teste exercita o código que a engine chama, não uma cópia dele.

### Uma tolerância, e só uma

A cena guarda a nave no referencial baricêntrico, a |r| = 1,4637·10¹¹ m. Uma
órbita de estacionamento de 400 km está a 6,8·10⁶ m da Terra — cinco ordens de
grandeza menor —, então `terra + parking − terra` é cancelamento catastrófico:
2,2·10⁻¹⁶ de 1,46·10¹¹ m são 3,2·10⁻⁵ m. **Medido: 4,5·10⁻⁶ m.**

O que tem de ser exato, e é, é o estado **absoluto** que o core reconstrói ao
somar a origem de volta — bit a bit. A órbita de estacionamento é uma
representação intermediária; o estado absoluto é o número que o integrador usa.
As velocidades fazem a viagem de ida e volta exatamente, porque a Terra a
3·10⁴ m/s e a órbita a 7,7·10³ m/s estão a um fator de quatro uma da outra e não
há nada para cancelar.

---

## 3. A campanha pelo caminho de produção (§7)

`scripts/lunar_campaign.sh 365 6`, através de `plan_lunar_transfer`.

```text
épocas              365
sucessos            365   (100,0 %)
primeiras 100       100   (100,0 %)
```

E a comparação com a campanha anterior, coluna a coluna
(`tools/validation/compare_campaigns.py`):

```text
outcome (before -> after)
     SUCCESS -> SUCCESS     365

column                          identical   differ      max |diff|
departure_epoch_tdb_s                 365        0             0 s
time_of_flight_s                      365        0             0 s
lambert_branch                        365        0
departure_delta_v_ms                  365        0           0 m/s
correction_magnitude_ms               365        0           0 m/s
bplane_target_t_m                     365        0             0 m
bplane_target_r_m                     365        0             0 m
required_capture_dv_ms                365        0           0 m/s
actual_periapsis_m                    365        0             0 m
post_burn_ecc                         365        0
post_burn_periapsis_alt_m             365        0             0 m
post_burn_apoapsis_alt_m              365        0             0 m
post_burn_inclination_deg             365        0           0 deg
propellant_used_kg                    365        0            0 kg
```

**Bit a bit idêntica.** Não há divergência para explicar — o que é o resultado
certo, e não uma coincidência: a campanha passou a chamar o mesmo código por uma
fachada, e uma fachada que mudasse um número seria um defeito.

Distribuições sobre as 365 capturas:

| grandeza | mín | mediana | máx |
|---|---:|---:|---:|
| periapsis | 94,47 km | 96,59 km | 97,87 km |
| apoapsis | 101,13 km | 103,03 km | 104,32 km |
| excentricidade | 0,0015 | 0,0017 | 0,0022 |
| Δv total | 3 900,5 m/s | 4 086,7 m/s | 4 824,4 m/s |
| **inclinação** | **0,12°** | **19,72°** | **30,21°** |
| RAAN | 0,22° | 268,59° | 359,14° |

---

## 4. Inclinação: reportada, não corrigida (§13)

A faixa de 0,12° a 30,21° é exatamente a que o brief observou, e **não é um
defeito**. A inclinação não faz parte da especificação: o sobrevoo é mirado por
magnitude de |B| com o ângulo do plano B fixo em zero, e a inclinação resultante
é consequência da geometria de chegada daquela época.

O que seria um defeito é o planejador saber o número e não dizê-lo. Portanto:

* `OrbitTarget` aceita `inclination` e `raan` opcionais (§12) — vocabulário antes
  da capacidade, para que o dia em que ela chegar não mude todos os chamadores;
* `MissionMetrics` reporta `predicted_inclination` e `predicted_raan`;
* **cada alternativa** que a busca voou reporta as suas, e a cena as imprime;
* `TransferCost` ganhou um peso `inclination_error`, **zero por omissão** (§14).
  Ligá-lo muda qual trajetória é escolhida, o que é uma campanha nova.

Exemplo real, da cena rodando sem teclado:

```text
[mission]   alt prograde/5.00d/coast=1.467h  INVALID_BPLANE  ...
[mission]   alt prograde/5.50d/coast=1.467h  ok   5.50 d  6327 m/s → 97 x 103 km, e 0.0017, i 20.3°
[mission]   alt prograde/4.75d/coast=1.467h  ok   4.75 d  6556 m/s → 97 x 103 km, e 0.0017, i 19.5°
[mission]   alt prograde/5.50d/coast=1.333h  DEPARTURE_CORRECTOR_STAGNATED  ...
[mission]   alt prograde/4.50d/coast=1.467h  ok   4.50 d  6661 m/s → 97 x 103 km, e 0.0017, i 19.5°
```

Note que a escolhida (4,50 d, 6 661 m/s) **não** é a mais barata (5,50 d,
6 327 m/s). A função de custo precifica a autoridade que o corretor precisou
gastar, não só o Δv — e essa regra é do Milestone 6.1, medida lá.

---

## 5. Planejamento visível e estados de missão (§§15, 16)

Tudo o que a §15 pede está no cockpit, e cada número vem de `MissionMetrics`:

```text
mission        COMPLETE to Moon   (K: abandon)
departure      past   prograde branch, 4.50 d of flight
arrival        past
injection      5831.6 m/s over 9.7 min
midcourse      2729.4 m/s   (folded into the injection, not a separate burn)
capture        829.7 m/s over 82.9 s
total dv       6661.3 m/s of 26942938 available
predicted      96.9 x 103.2 km, e 0.0017, i 19.49 deg, RAAN 307.3 deg
propellant     14.81 kg required, 18985.19 kg left after
autopilot      lag 0.00 deg mean / 0.00 deg peak, RCS 0.0 g, duty 0.000
```

Os estados da §16 estão em `core/navigation/mission_execution.hpp` e são
**calculados no core**, porque "a nave está em aproximação" é uma afirmação sobre
a trajetória — depende de onde está a esfera de Hill do destino, de onde estão as
queimas, de se a atitude assentou. A UI imprime um rótulo.

Duas coisas ditas em voz alta em vez de escondidas:

* **`MIDCOURSE_CORRECTION` nunca é atingido** com o formato de plano atual. O
  corretor diferencial dobra a sua autoridade dentro da injeção — é o sentido de
  corrigir a velocidade de partida em vez de acrescentar uma queima depois. O
  estado é entrado quando uma manobra chamada `midcourse` está ativa; no dia em
  que o planejador emitir uma, o cockpit já sabe como chamá-la.
* **A fase comanda a atitude.** Sem isso `ORIENTING` seria um rótulo que ninguém
  obedece: o cockpit diria que a nave está se preparando enquanto o nariz ficasse
  onde o piloto o deixou. O comando vem do core (que sabe qual é a próxima
  queima e para onde ela aponta) e a cena o aplica. Uma missão armada pilota; uma
  abandonada devolve a atitude — e `set_pointing_mode` **recusa** enquanto o
  computador está pilotando, em vez de aceitar e desfazer no quadro seguinte.

---

## 6. Predito × realizado (§17)

Gravado no instante em que a órbita vira órbita — uma revolução após o corte da
queima de captura, que é como o planejador também a lê.

**No cenário dourado, através do arnês de teste:**

```text
                     predicted        actual    difference
  periapsis altitude    96.387        96.379        -0.008  km
  apoapsis altitude    102.643       102.636        -0.006  km
  eccentricity           0.002         0.002         0.000
  inclination           18.771        18.772         0.002  deg
  propellant used        9.028         9.028         0.000  kg
  capture delta-v      829.250       829.250        -0.000  m/s
  arrival                                          -150.862  s
```

**Na cena, ao vivo, a 100 000× de warp:**

```text
about Moon      CAPTURED   1840.4 km at 1630.8 m/s
  orbit        96.8 x 103.2 km altitude, e 0.0017, i 19.44 deg, 117.8 min

                    predicted         actual     difference
  periapsis km        96.8998        96.8388        -0.0610
  apoapsis km        103.1652       103.1584        -0.0069
  eccentricity         0.0017         0.0017         0.0000
  inclination         19.4933        19.4950         0.0017
  propellant kg        14.8076        14.8076         0.0000
  capture dv         829.7210       829.7211         0.0001
```

**61 metros** de erro de previsão de periapsis sobre uma transferência de quatro
dias e meio voada no jogo.

Duas escolhas de medição que custaram uma correção cada:

* **A massa antes da queima** é lida no último instante **antes** da ignição, não
  no primeiro dentro da queima. A queima de captura dura ~83 s; um chamador que
  amostre o voo mais grosso do que isso — o teste de regressão anda 5 dias em
  4 000 passos, 119 s de espaçamento — leria a massa já na metade da queima e
  reportaria 21 m/s a menos do que o motor entregou. Massa é constante em deriva,
  então qualquer amostra anterior é exata.
* **A chegada realizada** é o mínimo corrente da distância ao destino, não "o
  instante em que esta medição foi tomada". Contra o segundo, a tabela reportava
  duas horas de discrepância que eram inteiramente um artefato de quando se olhou.

Um Δv de captura que não teve os dois extremos observados é reportado como
`not recorded`, e não com o número do plano disfarçado de medida.

---

## 7. Endurecimento do autopiloto (§§8–11)

Detalhes e todas as medições em
[autopilot-hardening.md](autopilot-hardening.md). O resumo:

**O mecanismo tem forma fechada.** O atraso é o erro de regime de um PD tipo 0
seguindo uma rampa, `θ = 2ζω/ω_n`. Na periapse de uma órbita lunar de 100 km o
retrógrado gira a 8,886·10⁻⁴ rad/s, o que a 0,05 rad/s de banda dá 2,036° — contra
**2,064° medidos, 1,4 %**. A varredura confirma a lei `1/ω_n` com **0,4 % de erro
sobre um fator de seis em ganho**.

**A escolha.** ω_n = **0,20 rad/s**, o menor ganho varrido que satisfaz
`pico < 1°` **e** `média < 0,5°` (0,15 erra a média por 10 %). A ω_n = 0,05
nenhuma das três épocas captura — o ponto de partida do brief não era só
impreciso, era insuficiente.

**Sem oscilação**, e verificado sem inspecionar séries temporais: `pico/média`
fica em `1,2425 ± 0,004` ao longo de todo o varrimento. A forma de onda não muda,
só a escala; um controlador subamortecido teria essa razão crescendo com o ganho.

**Saturação: zero**, em todas as corridas — e agora medida em vez de suposta,
depois que `RcsSystem::max_torque()` foi corrigido de `4aF` para os `2aF` que a
nave de fato entrega.

### A excentricidade não é monotônica no ganho

O resultado que eu não esperava. Ajustando os seis ganhos varridos:

```text
e = | 0,00735 · lag[°] − 0,001710 |     resíduos ±3,6·10⁻⁵ sobre 0,00035 .. 0,0105
```

**O intercepto é a excentricidade da queima finita** (0,001710 contra 0,001732).
O atraso contribui um vetor de excentricidade; a guiagem retrógrada-instantânea
ideal contribui outro de sinal oposto; `|e|` é a diferença. Logo `|e|` tem mínimo
onde eles se cancelam e **volta a subir** acima disso.

O ajuste usou só ω_n ≤ 0,30. Dois ganhos foram medidos **fora** dele:

| ω_n | `e` previsto | `e` medido |
|---:|---:|---:|
| 0,40 | 0,000197 | 0,000292 |
| 0,60 | 0,000704 | **0,000744** |

`e` subiu, como exigido. Isso dá à §10 o seu argumento mais forte: escolher a
banda **minimizando excentricidade** pousaria em ω_n ≈ 0,355 por uma razão que
não tem nada a ver com qualidade de controle — é cancelamento entre dois erros
opostos, e o ponto de cancelamento se move quando a duração da queima muda. As
duas linhas de apontamento são monotônicas em ω_n por construção; foi nelas que a
escolha se apoiou.

### Uma consequência de custo

O orçamento de `2·10⁶` passos, calibrado para `FINITE_BURN`, não serve para o
autopiloto. A **mesma** busca, mesma época:

| modelo | passos | propagações |
|---|---:|---:|
| `FINITE_BURN` | 138 050 | 435 |
| `AUTOPILOT` | 42 393 965 | 773 |
| razão | **307×** | 1,8× |

Só 1,8 vezes os voos e 307 vezes os passos: **é o tamanho do passo que colapsa**.
Daí um orçamento por modelo (`2·10⁶` / `150·10⁶`) e não um único maior, que
deixaria uma corrida `FINITE_BURN` genuinamente errada moer setenta e cinco vezes
mais antes de avisar.

---

## 8. Regressão de missão completa (§18)

`tests/scientific/test_full_mission.cpp`. O teste é dividido em duas metades, e a
divisão é o ponto:

1. **BUSCA**, sob guiagem ideal, na grade de produção completa. Existe uma
   trajetória, a busca a encontra, e a órbita prevista é a pedida? Falha aqui é
   falha de **planejador** — não há erro de controle nesse modelo.
2. **EXECUÇÃO**, exatamente naquela geometria, fixada, voada pelo controlador de
   atitude com doze propulsores respondendo. Falha aqui é falha de **execução**,
   porque a etapa 1 já provou a trajetória.

A leitura literal da §18 — uma busca AUTOPILOT sobre a grade inteira — custa
dezenas de milhões de passos e quase uma hora. A decomposição é mais barata **e
diz mais**: é a mesma da §19, e um teste que responde às duas perguntas
separadamente consegue dizer qual delas quebrou.

Fixar não pula o planejador: o corretor, a mira no plano B e a captura rodam
todos de novo, no modelo do autopiloto. O que se pula é rebuscar uma grade cuja
resposta a etapa 1 já produziu.

Um segundo teste verifica a **contabilidade** da §17 — que o monitor visita as
fases que o plano contém e consegue dizer o que previu contra o que aconteceu.
`ORIENTING` e `CAPTURE_ORIENTING` não estão entre elas ali, e o teste diz por
quê: ele roda em `FINITE_BURN`, onde a lei de guiagem *é* a direção do empuxo e o
erro de apontamento é zero por construção. Fingir que passaram seria pior do que
não visitá-las.

---

## 9. Campanha de execução (§§19–21)

[mission-execution-campaign.csv](mission-execution-campaign.csv), **114 casos**,
cada um planejado sob IMPULSIVE e depois voado sob AUTOPILOT na geometria fixada.

| eixo | casos | planning | execution | final orbit |
|---|---:|---:|---:|---:|
| §19 órbita do cenário (407 km / 28,3°) | 42 | 42/42 | 40/42 | 40/42 |
| §20 matriz 200 / 400 / 800 km | 27 | 27/27 | 27/27 | 27/27 |
| §20 matriz 1000 km | 9 | 9/9 | **6/9** | 6/9 |
| §21 tanques | 36 | 22/36 | 22/36 | 22/36 |
| **total** | **114** | 100/114 | 95/114 | 95/114 |

As três perguntas são contadas separadamente porque colapsá-las é o que deixou o
Milestone 6 incapaz de dizer se o culpado era o planejador ou o motor.

**§20, altitude.** 200, 400 e 800 km entregam 9/9. A **1000 km** o planejamento
continua 9/9 e a execução cai para 6/9 — uma época em três falha com
`DEPARTURE_CORRECTOR_STAGNATED`, em todas as três inclinações. Como o
planejamento impulsivo passou, é falha de **execução** por definição: a
trajetória existe e o corretor no modelo do autopiloto não a inverte.

**§20, inclinação.** 0°, 28,5° e 51,6° dão resultados **idênticos** em todas as
altitudes. Não é surpresa depois de dito: a nave carrega 26 942 km/s, então uma
mudança de plano é de graça. O eixo da inclinação não é uma restrição *para esta
nave* — e isso é o achado, não a ausência dele.

**§21, tanques.** Ver a §10 abaixo.

### Predito × realizado, sobre as 95 missões completas

| grandeza | mediana | pior |
|---|---:|---:|
| periapsis | −2,48 km | −5,58 km |
| apoapsis | +2,56 km | +7,19 km |
| excentricidade | +0,00134 | +0,00306 |
| inclinação | −0,018° | +4,08° |
| Δv de captura | +0,27 m/s | −33,9 m/s |

**Cuidado ao ler esta tabela.** Aqui o *predito* vem da perna **impulsiva** e o
*realizado* da perna **autopiloto** — modelos diferentes, de propósito, porque é
disso que a §19 trata. Os 2,5 km são o custo físico de voar uma queima finita em
vez de um impulso, **não** um erro de previsão.

A comparação predito × realizado *dentro do mesmo modelo* está na §6 acima:
**61 metros** de periapsis, na cena, ao vivo.

### O autopiloto, sobre as 95 completas

| | mín | mediana | máx | especificação |
|---|---:|---:|---:|---|
| apontamento médio | 0,386° | 0,413° | 0,422° | < 0,5° ✅ |
| apontamento de pico | 0,498° | 0,512° | 0,522° | < 1,0° ✅ |
| saturação de torque | 0,000 | 0,000 | **0,000** | sem saturação ✅ |
| RCS por missão | 0,134 g | 0,167 g | 0,207 g | desprezível ✅ |
| excentricidade | 0,0010 | 0,0013 | 0,0031 | ≤ 0,01 ✅ |

A especificação escolhida na §7 se sustenta em **todas as 95**, não só nas três
épocas com que foi escolhida.

---

## 10. Combustível insuficiente (§21)

O requisito é que o planejador **recuse** quando `required Δv > available Δv`, e
que a recusa se chame `INSUFFICIENT_PROPELLANT`. Esse rótulo era **código morto**
antes deste milestone: existia na taxonomia e nada o atribuía.

Calibrar o cenário levou três tentativas, e as duas primeiras erradas são a parte
útil.

**Primeira, 1,02× = 4 182 m/s.** Justificada com "103 das 365 épocas custam mais
que isso". Coluna errada: aqueles 103 vêm do `departure_delta_v` da campanha
FINITE, que já tem a autoridade do corretor embutida; a perna de **planejamento**
é impulsiva e suas missões custam 3 907 a 4 132 m/s. Um orçamento comparado
contra a coluna errada não é um orçamento. Resultado: nenhuma recusa.

**Segunda, espalhar as épocas pelo ano** em vez de cinco dias seguidos.
Resultado: ainda nenhuma recusa — e o motivo é o achado. **O planejador não falha
quando o tanque aperta; ele re-otimiza.** `TransferSession::screen` recusa
candidatos que a nave não pode pagar, então um orçamento apertado **estreita a
busca em vez de derrotá-la**. Sobre doze épocas espalhadas, o tanque apertado
mudou a missão escolhida uma vez (2026-03-31, tempo de voo 5,00 → 3,75 d).

Logo um cenário só exercita a recusa se o orçamento estiver abaixo do
**transfer mais barato alcançável**, não abaixo do escolhido livremente.

**Terceira**, e os quatro cenários finais:

| cenário | orçamento | planejou | o que demonstra |
|---|---:|:---:|---|
| ABUNDANT | 26 942 938 m/s | 12/12 | 6 600×; nunca restringe |
| LIMITED | 5 535 m/s | 12/12 | um tanque meramente confortável ainda não é restrição |
| **MARGINAL** | **4 100 m/s** | **10/12** | a resposta depende da data de partida |
| INSUFFICIENT | 3 895 m/s | 0/12 | abaixo do mais barato alcançável: recusa sempre |

E as duas recusas do MARGINAL **não são a mesma falha**:

* **2026-11-26 → `INSUFFICIENT_DEPARTURE_DV`.** O crivo analítico recusou todos
  os candidatos: não existe partida que a nave possa pagar. Pego antes de uma
  única propagação.
* **2026-07-29 → `INSUFFICIENT_CAPTURE_DV`.** Uma partida *era* pagável, a
  transferência foi voada, e a nave não pôde pagar a queima de captura ao chegar.

A diferença entre as duas expõe onde o crivo é aproximado: ele testa
`departure + insertion_estimate > budget` com uma estimativa **de dois corpos**
da inserção, e naquela época a captura voada custou mais do que a estimativa. O
planejador escolheu ali uma partida *mais cara* (4 128 m/s, 5,50 d) do que sob
ABUNDANT e só descobriu o problema na chegada.

---

## 11. Headless contra GPU (§§22, 23)

Antes deste milestone `ctest` rodava 34 testes, todos verdes, numa árvore cujo
starfield estava invisível. Nada mentia — simplesmente **nenhum daqueles testes
rasteriza um pixel**, e nada dizia isso ao leitor.

```bash
cmake --build build --target ctest-headless     # aritmética; roda em qualquer lugar
cmake --build build --target gpu-validation     # precisa de framebuffer real
```

`gpu.starfield` e `gpu.relativistic_visual` entraram na suíte com rótulo `gpu` e
**pulam** (saída 77 → CTest reporta *Skipped*) quando falta Godot, falta a
GDExtension, ou não há display. Uma máquina que não pode rodá-los não os
reprovou; ela não os rodou.

O arnês do starfield foi **preservado como está** (§22): nenhuma mudança
estrutural era necessária e nenhuma foi feita.

E a §23 virou um documento: [graphics-compatibility.md](graphics-compatibility.md)
diz que tudo o que existe de evidência gráfica neste repositório foi medido em
**Apple M5 Pro / Metal / Godot 4.5 Forward+ / macOS**, e lista o que não foi
testado — Vulkan, D3D12, OpenGL, Mobile, Compatibility, Windows, Linux, AMD,
NVIDIA, Intel. Isso não bloqueia o milestone; é o alcance da evidência, dito.

---

## 12. O que NÃO foi feito, e porquê

**O controlador não foi redesenhado** (§8, explícito). O atraso de rastreamento é
removível — por *feed-forward* da taxa do alvo, ou por um termo integral — e
nenhum dos dois está implementado. Ambos estão nomeados em
[attitude.md](../physics/attitude.md) §7 desde o Milestone 3. Subir o ganho
compra a especificação com **autoridade**; o próximo milestone deveria comprá-la
com **estrutura**, e aí o ganho pode voltar a 0,05 com o mesmo apontamento e um
quarto do consumo de RCS.

**A inconsistência da inércia não foi corrigida.** O casco que o controlador gira
tem 1000 kg; a nave que o integrador translada tem 20 000 kg. São o mesmo objeto
com duas massas. Está registrado em
[autopilot-hardening.md](autopilot-hardening.md) §6 em vez de silenciosamente
consertado, porque consertá-lo multiplica a inércia por ~20 e **muda todos os
números daquela página** — seria uma campanha nova apresentada como um ajuste.
Os dois lados do simulador usam consistentemente `solid_box(1000, {8,3,3})`, de
modo que ele é internamente coerente; o que não é coerente é com a massa da nave.

**A otimização de inclinação não foi implementada** (§14, explícito). O
vocabulário existe (`OrbitTarget::inclination`, `TransferCost::inclination_error`)
e o peso é zero. Ligá-lo é uma campanha nova.

**`orbit-cli intercept` continua com o seu próprio corretor.** É um comando de
diagnóstico de UMA transferência — sem busca de partida, sem grade de tempo de
voo, sem função de custo — e é o instrumento que produziu
[b-plane.md](../physics/b-plane.md) §9. Não está no caminho de produção e a §5 do
brief é sobre o planejador do jogo. É o próximo candidato óbvio a consolidação, e
fica dito aqui em vez de ficar por dizer.

**Tempo retardado e objetos extensos** continuam fora do caminho crítico (§24).
Os estudos existentes foram preservados e nada de novo foi implementado.

---

---

## 13. Critérios de saída (§25)

| critério | veredito | evidência |
|---|:---:|---|
| Godot usa o mesmo planner do core | **PASS** | `plan_lunar_transfer` é a única porta; ponte de 81 linhas |
| não existe duplicação de algoritmo orbital no frontend | **PASS** | 469 linhas apagadas; nove símbolos proibidos, verificados lendo o fonte |
| 365/365 continuam válidos pelo caminho público | **PASS** | 365/365, **bit a bit idênticos** em 14 colunas |
| autopilot atende a especificação definida | **PASS** | ω_n = 0,20; pico ≤ 0,522°, média ≤ 0,422° em **95/95** missões |
| missão completa funciona com queima finita | **PASS** | `test_full_mission`, busca + execução separadas |
| diferenças predicted × actual são registradas | **PASS** | `MissionOutcome`; 61 m na cena ao vivo |
| regression de missão completa existe | **PASS** | `tests/scientific/test_full_mission.cpp` |
| campanha de execução real foi realizada | **PASS** | 114 casos, três eixos |
| combustível insuficiente é corretamente detectado | **PASS** | `INSUFFICIENT_DEPARTURE_DV` ×12, `INSUFFICIENT_CAPTURE_DV` ×1 |

### O que não está verde, e não deveria estar

Cinco casos em 114 falham a execução depois de o planejamento passar, e nenhum
deles é uma surpresa escondida:

* **3 a 1000 km de altitude** (uma época em três, em todas as inclinações),
  `DEPARTURE_CORRECTOR_STAGNATED`;
* **2 na órbita do cenário**, o mesmo mecanismo.

São falhas de **execução**, classificadas como tal, com o planejamento provado
independente. É exatamente a discriminação que a §19 pede — e a taxa, 95/114,
está reportada em vez de escondida atrás de uma média.

---

## 14. Entrega (§26)

| arquivo | conteúdo |
|---|---|
| [`docs/architecture/navigation-integration.md`](../architecture/navigation-integration.md) | arquitetura, API, regra, o teste que impede a divergência |
| [`docs/validation/autopilot-hardening.md`](autopilot-hardening.md) | mecanismo, varredura, especificação, saturação, orçamento de passos |
| [`docs/validation/full-mission-regression.md`](full-mission-regression.md) | cenário dourado e critérios |
| [`docs/validation/mission-execution-campaign.csv`](mission-execution-campaign.csv) | 114 casos, 38 colunas |
| [`docs/validation/milestone-6-2-report.md`](milestone-6-2-report.md) | este documento |

Adicionais, porque o milestone os produziu:

| arquivo | conteúdo |
|---|---|
| [`docs/validation/graphics-compatibility.md`](graphics-compatibility.md) | §23: o alcance da evidência gráfica, dito |
| [`tools/validation/compare_campaigns.py`](../../tools/validation/compare_campaigns.py) | a comparação coluna a coluna que provou os 365 idênticos |
| [`scripts/execution_campaign.sh`](../../scripts/execution_campaign.sh) | a campanha da §19, fatiada por processo |
| [`scripts/gpu_validation.sh`](../../scripts/gpu_validation.sh) | a suíte que precisa de framebuffer |

---

## 15. Três erros meus, e o que cada um custou

Registrados porque são o tipo de erro que este milestone existe para tornar
visível, e porque os três foram achados pelos próprios instrumentos.

**A campanha reconstruía a órbita de estacionamento.** Sem pedir matriz nenhuma,
`run_execution_campaign` refazia o estado a partir de elementos com
`raio_médio + 400 km` = 6 771 008 m, contra os 6 778 000 m do cenário — cujos
"400 km" são acima do raio **equatorial**. Toda execução padrão voou uma órbita
**7 km abaixo** da que nomeava. A §19 foi refeita.

**O crivo de combustível foi calibrado contra a coluna errada**, e depois com uma
amostragem que não podia disparar. As duas versões produziam números defensáveis
sobre a coisa errada. A terceira encontrou o mecanismo real — o planejador
re-otimiza — e o transformou em quatro cenários que medem coisas diferentes.

**A instrumentação do autopiloto rodava em cada voo de sonda.** Centenas por
época, cada um pagando duas chamadas de efeméride por passo aceito, para medir
trajetórias que ninguém voa. Multiplicava o custo de uma época por uma ordem de
grandeza. Guardada para o voo que tem queima de captura, que é o único onde
qualquer daquelas grandezas existe.
