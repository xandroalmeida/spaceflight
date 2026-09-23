# Nave

A geometria é **procedural** (regras 7, 48): `app/presentation/scene/spacecraft_visual.cpp`
constrói 22 metros de veículo com primitivas. Não há modelo artístico e o M7 não
espera por um.

* [dimensions.md](dimensions.md) — as medidas, e a dívida de inércia que elas
  deixam registrada
* [3d-model-brief.md](3d-model-brief.md) — o contrato para quem quiser fazer um
  modelo que substitua a geometria procedural
* [hull-panels-codex-prompt.md](hull-panels-codex-prompt.md) — a única textura
  externa, e opcional

## O que a silhueta tem de dizer

Na ordem em que importa:

```
cockpit com janelas          onde fica a frente
motor com sino               onde fica a trás
radiadores planos e largos   "isto opera no vácuo"
tanques cilíndricos          "isto carrega propelente"
treliça entre os dois        "não há ar aqui"
antena de alto ganho         escala
manta térmica dourada        a única cor saturada do veículo
```

Sem asas, sem superfícies de controle, sem carenagens aerodinâmicas, sem cauda:
a nave opera exclusivamente no vácuo (regra 5).

## Três coisas que a geometria tem de respeitar

1. **`+x` é o nariz** — a direção ao longo da qual `MainEngineForce` empurra. Não
   é negociável; vem de `core/propulsion/`.
2. **Os seis pontos de RCS** estão em (±2, 0, 0), (0, ±2, 0), (0, 0, ±2) metros,
   porque é ali que `RcsSystem::couples(2.0, ...)` os põe. Os nacelles são
   desenhados NAS posições que a física usa, e não onde ficariam bonitos.
3. **A origem é o centro de massa.**

## A pluma e os jatos

Não são geometria da nave: são `EnginePlume` e `RcsVisual` (no mesmo
`spacecraft_visual.cpp`), e ambos consomem o estado do core.

A pluma segue o **empuxo real** (`thrust_n`), não o acelerador — com o tanque
vazio a tecla continua a funcionar e o empuxo é zero. Os jatos seguem
`RcsForce::throttles()`, que é a mesma função que o modelo de forças voa: não há
arranjo de código em que o jato desenhado e o propelente queimado discordem
(regras 15, 16).
