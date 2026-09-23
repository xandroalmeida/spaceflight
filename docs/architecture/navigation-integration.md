# Integração da navegação: um único planejador

**Milestone 6.2, seções 1 a 7, 12 a 16.**

Este documento descreve a arquitetura depois da integração, o que foi apagado, e
a regra que impede a divergência de voltar.

> Nota (2026-09-23, ADR-0009): a apresentação deixou de ser Godot. A ponte que
> este documento chama de `godot/gdextension/src/mission_planner.cpp` é hoje
> `app/session/transfer_bridge.cpp` (`sf::app::plan_transfer`), chamada por
> `FlightSession` em `app/session/flight_session.cpp`. O §1 descreve o estado de
> antes do Milestone 6.2 e fica como estava; o resto usa os nomes atuais.

---

## 1. O problema

O Milestone 6.1 fechou a navegação Terra–Lua com 365 épocas capturadas em 365. A
campanha media isto:

```text
tools/lunar-campaign
        ↓
core/navigation/lunar_transfer.hpp
```

O jogo, quando o piloto apertava **J**, executava isto:

```text
Godot (tecla J)
        ↓
SpaceflightSimulation::plan_transfer
        ↓
godot/gdextension/src/mission_planner.cpp      532 linhas
```

E `mission_planner.cpp` não chamava `lunar_transfer.hpp`. Ele tinha o seu próprio
tudo:

| mecanismo | no core | no GDExtension |
|---|---|---|
| varredura de partida | `TransferSession::build_candidates` | laço sobre `coast.sample(48)` |
| grade de tempo de voo | `time_of_flight_days`, 10 valores | `{0.7, 0.85, 1.0, 1.15, 1.3, 1.5}` × o palpite |
| banda degenerada de Lambert | ±2° em torno de π | `> 3.09 || < 0.09` rad |
| triagem da cônica de partida | analítica, com piso de 120 km | **não existia** |
| corretor de posição | `departure_targeting`, tol. 1e7 m | `position_tolerance_m`, tol. 1e4 m |
| corretor de plano B | 4 passadas, tolerância derivada de dr_p/db | 3 passadas, mesma derivação, outro laço |
| função de custo | pesos explícitos, §8 do 6.1 | ordenar por Δv, depois voar e ordenar pela distância |
| classificação de falha | 20 razões nomeadas | uma `std::string message` |

A consequência não é estética. **A campanha certificava um programa que o jogador
nunca executava.** Os 365/365 descreviam `lunar_transfer.hpp`; a tecla J
executava outra coisa. Um relatório de validação sobre software que ninguém roda
não é um relatório de validação.

E as duas implementações *já* discordavam em algo mensurável: o GDExtension não
tinha a triagem da cônica de partida, que é o mecanismo por trás de 46 das 59
falhas do Milestone 6. O caminho do jogo continuava com o defeito que o caminho
científico tinha corrigido.

---

## 2. A arquitetura depois

```text
     UI (app/presentation, Shift+J)
            │  destino, órbita alvo, janela de busca
            ▼
     FlightSession::plan_transfer                  app/session/flight_session.cpp
            │  estado da sessão  →  struct do core   (numa thread)
            ▼
     sf::app::plan_transfer                        app/session/transfer_bridge.cpp (a ponte)
            │  state_for() + request_for()
            │  SimulationState + LunarTransferRequest
            ▼
     sf::navigation::plan_lunar_transfer           core/navigation/mission_planner.hpp
            │
            ▼
     sf::navigation::plan_and_fly                  core/navigation/lunar_transfer.hpp
            │
     Lambert · corretor de posição · plano B · queima finita · classificação
```

E, exatamente do mesmo ponto:

```text
     tools/lunar-campaign
            │  SimulationState + LunarTransferRequest
            ▼
     sf::navigation::plan_lunar_transfer
```

Os dois entram pela mesma porta, com o mesmo tipo de requisição, e recebem o
mesmo tipo de resposta.

### O que foi apagado

`godot/gdextension/src/mission_planner.cpp` passou de **532 linhas** para **81**
(hoje `app/session/transfer_bridge.cpp`, 102 linhas contando comentários).
O que sobrou é a tradução: converter o estado absoluto da simulação em uma órbita
de estacionamento relativa ao corpo central, preencher a requisição, chamar o
core, devolver o resultado. Nenhuma aritmética decide nada sobre trajetória.

O que **não** foi feito: mover o código para outro arquivo. As 469 linhas de
astrodinâmica foram deletadas, porque já existia uma implementação melhor delas.

---

## 3. A API pública

`core/navigation/mission_planner.hpp`.

```cpp
MissionPlanResult plan_lunar_transfer(const SimulationState& state,
                                      const LunarTransferRequest& request);
```

A requisição é declarativa — o que a missão quer, não como consegui-lo:

```cpp
struct LunarTransferRequest {
    celestial::BodyId       origin, destination;
    OrbitTarget             target_orbit;       // §12
    TimeWindow              departure_window;
    DurationRange           time_of_flight;
    OptimizationObjective   objective;          // §14
    SpacecraftCapabilities  spacecraft;
    SearchEffort            effort;
};
```

E o resultado é executável sem interpretação:

```cpp
struct MissionPlanResult {
    MissionPlanStatus              status;
    ManeuverPlan                   maneuvers;     // pronto para armar
    TrajectoryPrediction           trajectory;
    MissionMetrics                 metrics;       // §15
    std::optional<FailureReason>   failure;
    std::vector<MissionAlternative> alternatives; // §13
    TransferRecord                 diagnostics;   // só para a campanha
};
```

### O que a apresentação não recebe

Sem solução de Lambert, sem jacobiano, sem resíduo do corretor, sem lista de
candidatos, sem vetor de velocidade de partida. Isso é *como* a resposta foi
obtida, e um cliente que lê essas coisas passa a depender delas — o próximo
planejador teria de reproduzir as entranhas do anterior para não quebrar a cena.

`diagnostics` é a única exceção deliberada, e o nome diz para quem ela é: a
campanha grava quarenta grandezas por época (§3 do 6.1) e o CSV delas é a
evidência em que o milestone se apoia. A ponte não a toca — e
`tests/scientific/test_planner_equivalence.cpp` verifica isso lendo o fonte.

### `maneuvers` é o que voou, não uma reconstrução

`TransferRecord::flight_plan` guarda o `ManeuverPlan` que o voo vencedor
**executou**, verbatim. Antes disso, a única forma de a cena voar a trajetória
validada seria re-derivar as queimas a partir do registro — e uma re-derivação é
exatamente uma segunda implementação, do tipo que este milestone apagou.

Sob `ExecutionModel::Impulsive` o plano vem vazio, e isso é a resposta correta:
não existe motor naquele modelo, então não existe manobra para armar um navio.

---

## 4. A regra arquitetural

> Astrodinâmica pertence ao core. A apresentação mostra, controla e solicita.
> A apresentação não resolve órbitas.

(No brief do Milestone 6.2 a regra dizia "Godot"; vale igual para o executável próprio do ADR-0009.)

O que a cena **pode** fazer:

* fornecer parâmetros (destino, periapsis/apoapsis alvo, janela de busca);
* iniciar o planejamento;
* apresentar o resultado e as alternativas;
* solicitar a execução;
* abortar.

O que a cena **não** escolhe: época de partida, tempo de voo, ramo de Lambert,
ponto de mira no plano B, duração de queima, orientação durante a queima.

### O tempo de voo não é um botão

O `plan_transfer` antigo recebia `time_of_flight_days` da GDScript (a UI de então). O novo não
recebe, e a ausência é o ponto.

Com o ponto de partida fixo em onde a órbita estivesse e o tempo de voo fixo em
4,5 dias, o ângulo de transferência é o que o calendário der — e acima de ~215°
a solução de Lambert exige uma velocidade de partida 4,6 a 13 km/s fora da
órbita, numa direção cuja cônica reentra na Terra. 82,5 % das geometrias assim
obtidas são invoáveis a partir de 400 km. Foi isso que fez o Milestone 6 falhar
82 épocas em 100.

Oferecer o tempo de voo como um mostrador do cockpit seria recolocar o defeito
pela interface.

---

## 5. O teste que impede a divergência

`tests/scientific/test_planner_equivalence.cpp`, três verificações.

**A pergunta.** A requisição que a ponte constrói, campo a campo, contra a que a
campanha constrói — e para **os três modelos de execução**, porque a tradução é
por campo e um desvio num modelo que ninguém voou hoje ficaria invisível até o dia
em que alguém o voasse. Quarenta campos, incluindo os dez valores da grade de
tempo de voo.

É por isso que `state_for` e `request_for` são funções públicas separadas: um
teste que só consegue comparar duas trajetórias voadas diz que elas concordam
*hoje*; um que compara as duas **requisições** diz que a ponte continua fazendo a
mesma pergunta, que é a invariante que de fato tem de valer.

**A resposta.** O mesmo estado, época, nave e destino, por dois caminhos:

* o caminho da campanha — `navigation::plan_lunar_transfer`;
* o caminho da cena — `sf::app::plan_transfer`, **o fonte real da ponte
  (`app/session/transfer_bridge.cpp`), compilado dentro do binário de teste**.

Isso é possível porque a ponte não tem SDL nem GPU dentro — uma escolha
deliberada (já era assim quando a ponte vivia na GDExtension sem Godot no
cabeçalho), e é ela que torna a afirmação verificável: o teste exercita o código
que o jogo chama, não uma cópia dele que por acaso concorda.

As oito grandezas da §6 são comparadas com **tolerância zero**:

```text
departure time · time of flight · Lambert branch · injection Δv
B-plane target · capture Δv · predicted periapsis · predicted final orbit
```

Zero porque não são dois métodos que deveriam concordar — é uma função chamada
duas vezes com argumentos que devem ser iguais, e o planejador é determinístico.
Uma tolerância aqui seria onde uma divergência real se esconderia: se a ponte
começar a ajustar "ligeiramente" um peso ou uma grade, um épsilon absorveria isso
e o teste continuaria verde enquanto os dois caminhos se separavam de novo.

E também as **manobras**: nome, ignição, duração, aceleração, modo de guiagem e
direção inercial. Concordar nos números e discordar nas queimas seria a pior das
duas falhas — o mostrador certo e o navio voando outra coisa.

**A forma.** A segunda verificação lê
`app/session/transfer_bridge.cpp` e exige que estes símbolos **não
apareçam**:

```text
solve_lambert · correct_departure · b_plane_from_state · aim_for_periapsis
plan_insertion · maneuver_for_delta_v · run_mission · propagate
```

A primeira verificação pega uma divergência na *resposta*. Esta pega uma
divergência na *forma*: uma ponte que ganha uma chamada de Lambert está a caminho
de voltar a ser um segundo planejador, e pode muito bem concordar com o core por
um tempo antes de parar.

---

## 6. Estados da missão

`core/navigation/mission_execution.hpp` (§16).

```text
IDLE → PLANNED → WAITING_FOR_DEPARTURE → ORIENTING → INJECTION_BURN
     → COAST → [MIDCOURSE_CORRECTION] → APPROACH → CAPTURE_ORIENTING
     → CAPTURE_BURN → ORBIT_INSERTION → COMPLETE | FAILED
                                                   ABORTED
```

A classificação está no core e não no cockpit porque "a nave está em
aproximação" é uma afirmação sobre a trajetória: depende de onde está a esfera de
Hill do destino, de onde estão as queimas, de se a atitude assentou. Física na
apresentação seria um terceiro lugar onde trajetórias são raciocinadas — depois do
core e depois do planejador que este milestone apagou.

`MissionExecution` é um **observador**. `update` recebe um estado e devolve uma
classificação; nada nele escreve em um `PropagationState`. É a mesma regra que o
controlador de atitude segue.

### `MIDCOURSE_CORRECTION` nunca é atingido, e isso está declarado

Com o formato de plano que este milestone entrega, o corretor diferencial dobra
a sua autoridade **dentro da injeção** — é exatamente o sentido de corrigir a
velocidade de partida em vez de acrescentar uma queima depois. Um plano voado tem
duas manobras e nenhuma é uma correção de meio-curso.

O estado existe e é entrado quando uma manobra chamada `midcourse` está ativa.
No dia em que o planejador emitir uma, o cockpit já sabe como chamá-la. Não é
código morto; é vocabulário declarado antes da capacidade, do mesmo modo que
`OrbitTarget::inclination`.

### `APPROACH` usa a esfera de Hill calculada

```text
r_H = d · (m / 3M)^(1/3)
```

A partir dos dois GMs e da separação que vale naquele instante, não de uma
constante. A da Lua dá 61 500 km; escrever esse número tornaria a classe
específica da Lua. Com o cálculo, a excentricidade da órbita do próprio destino
aparece honestamente — como um raio de Hill que respira alguns por cento ao longo
de um mês, em vez de uma constante que não respira.

---

## 7. Predito × realizado

`MissionOutcome` (§17), gravado no instante em que a órbita vira órbita — uma
revolução após o corte da queima de captura, que é como o planejador também a lê.

| grandeza | predito | realizado | diferença |
|---|---|---|---|
| periapsis | ✓ | ✓ | ✓ |
| apoapsis | ✓ | ✓ | ✓ |
| excentricidade | ✓ | ✓ | ✓ |
| inclinação | ✓ | ✓ | ✓ |
| combustível | ✓ | ✓ | ✓ |
| tempo de chegada | ✓ | ✓ | ✓ |
| Δv de captura | ✓ | por Tsiolkovsky sobre a massa consumida | ✓ |

O Δv de captura realizado só é gravado quando **os dois extremos da queima foram
observados**. Uma missão armada depois da queima, ou cujo estado não foi
atualizado durante ela, não tem resposta honesta — e reporta `not recorded` em
vez de devolver o número do plano disfarçado de medida.

---

## 8. Inclinação: reportada, nunca corrigida em silêncio

§13. As órbitas finais da campanha saem entre ~0,1° e ~30,2° de inclinação
lunar. **Isso não é um defeito.** A inclinação não faz parte da especificação: o
planejador aponta o sobrevoo por magnitude de |B| com o ângulo do plano B fixo em
zero, e a inclinação resultante é consequência da geometria de chegada.

O que seria um defeito é o planejador saber o número e não dizê-lo. Portanto:

* `OrbitTarget` **aceita** `inclination` e `raan` opcionais (§12) — o vocabulário
  existe antes da capacidade, para que o dia em que ela chegar não mude todos os
  chamadores;
* `MissionMetrics` **reporta** `predicted_inclination` e `predicted_raan`;
* cada `MissionAlternative` reporta os seus, para que a inclinação escolhida seja
  uma consequência visível de uma escolha;
* `TransferCost` ganhou um peso `inclination_error`, **zero por omissão** (§14).
  Ligá-lo muda qual trajetória o planejador escolhe, o que é uma campanha nova e
  não um ajuste.

---

## 9. Onde cada seção do brief foi parar

| § | assunto | onde |
|---|---|---|
| 1–3 | uma implementação autoritativa | `core/navigation/mission_planner.hpp`; ponte de 81 linhas (hoje `app/session/transfer_bridge.cpp`) |
| 4 | API do planejador | `LunarTransferRequest` / `MissionPlanResult` |
| 5 | remover duplicação | 469 linhas apagadas do GDExtension |
| 6 | teste de equivalência | `tests/scientific/test_planner_equivalence.cpp` |
| 7 | campanha pelo caminho público | `tools/lunar-campaign` → `plan_lunar_transfer` |
| 8–11 | endurecimento do autopiloto | [autopilot-hardening.md](../validation/autopilot-hardening.md) |
| 12 | `OrbitTarget` | `core/navigation/mission_planner.hpp` |
| 13 | inclinação reportada | `MissionMetrics`, `MissionAlternative` |
| 14 | custo extensível | `TransferCostTerms`, pesos novos em zero |
| 15 | planejamento visível | `MissionMetrics` → `FlightSession::plan()` → painel de missão |
| 16 | estados da missão | `core/navigation/mission_execution.hpp` |
| 17 | predito × realizado | `MissionOutcome` |
| 18 | regressão de missão completa | `tests/scientific/test_full_mission.cpp` |
| 19–21 | campanha de execução | [full-mission-regression.md](../validation/full-mission-regression.md) |
| 22–23 | separação headless/GPU | `tests/CMakeLists.txt`, `scripts/gpu_validation.sh` |
