# Arquitetura do Sistema

Status: vigente a partir do Milestone 0
Última revisão: 2026-09-23

## 1. Objetivo

Simular, de forma fisicamente consistente e verificável, o voo de uma nave de
massa desprezível através do Sistema Solar, com separação estrita entre física,
astronomia, visualização e gameplay.

O critério de sucesso do núcleo não é visual: é **numérico e reprodutível**.
Toda grandeza produzida pelo core deve ser comparável contra uma referência
externa (JPL, solução analítica, ou integrador independente) com tolerância
documentada.

## 2. Regra de dependência

```
godot/gdextension  ──► spaceflight_core ──► CSPICE
tools/orbit-cli    ──► spaceflight_core ──► CSPICE
tests/*            ──► spaceflight_core ──► CSPICE
```

Invariantes:

* `spaceflight_core` **não** inclui, linka ou conhece Godot;
* `spaceflight_core` **não** possui `main()`, loop de frame, nem estado global mutável;
* CSPICE aparece **apenas** dentro de `core/ephemeris/` (arquivos `.cpp`);
  nenhum header público do core inclui `SpiceUsr.h`;
* o executável gráfico depende do core; o core nunca depende do executável.

A consequência prática exigida pelo enunciado: o projeto inteiro compila,
roda e é testado sem abrir o engine gráfico.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## 3. Camadas

### 3.1 Fundação (sem dependências externas)

| Módulo | Conteúdo | Depende de |
|---|---|---|
| `core/math` | `Vec3`, `Mat3`, utilidades numéricas | — |
| `core/units` | constantes SI, conversões, tipos fortes | `math` |
| `core/time` | `CoordinateTime`, `Duration`, escalas de tempo | `units` |
| `core/coordinates` | `ReferenceFrame`, `Origin`, `Frame`, `StateVector` | `math`, `time` |

Esta camada é puramente aritmética, `constexpr` sempre que possível, e testável
sem kernels.

### 3.2 Astronomia (fronteira com CSPICE)

| Módulo | Conteúdo |
|---|---|
| `core/celestial` | `BodyId` (NAIF), catálogo de corpos, `GravityParameter`, raios |
| `core/ephemeris` | `EphemerisProvider` (interface), `SpiceEphemerisProvider`, `SpiceKernelSet`, `SpiceTimeConverter` |

`EphemerisProvider` é a **única** porta de entrada de dados astronômicos no
resto do sistema. Qualquer outro módulo que precise saber onde um corpo está
recebe um `const EphemerisProvider&` por injeção — nunca um singleton.

Isso permite substituir SPICE por:

* `AnalyticEphemerisProvider` (Kepler de dois corpos) em testes unitários rápidos;
* `FrozenEphemerisProvider` (corpos parados) em testes analíticos de propagação;
* `CachedEphemerisProvider` (decorator) para otimização.

### 3.3 Física

| Módulo | Conteúdo |
|---|---|
| `core/gravity` | `ForceModel` (interface), `PointMassGravity`, `CompositeForceModel` |
| `core/propagation` | `PropagationState`, `SpacecraftPropagator`, `DormandPrince54Propagator`, estatísticas |
| `core/relativity` | (Milestone 4) fator de Lorentz, 4-momento, tempo próprio, correções |
| `core/propulsion` | (Milestone 1+) motor, tanque, modelo energético |
| `core/spacecraft` | `SpacecraftState`, massa seca, tanque, composição da nave |
| `core/attitude` | (Milestone 3+) quaternions, inércia, RCS |

`ForceModel::evaluate` recebe estado e tempo e devolve aceleração; não guarda
estado mutável; é seguro chamar várias vezes por passo (o integrador chama 6–7
vezes por passo) e em qualquer ordem de tempo.

### 3.4 Orquestração

| Módulo | Conteúdo |
|---|---|
| `core/simulation` | `SimulationClock`, `Scenario`, `SimulationSnapshot` |
| `core/navigation`, `core/autopilot`, `core/trajectory` | Milestones 1 e 3+ |

### 3.5 Consumidores

* `tools/orbit-cli` — verificação científica pela linha de comando;
* `tools/validation` — comparações em lote contra JPL/REBOUND, saída CSV;
* `godot/gdextension` — Milestone 2 em diante; consome apenas `SimulationSnapshot`.

## 4. Fluxo de dados de uma avaliação de força

```
t (CoordinateTime, TDB)
      │
      ▼
EphemerisProvider.state(body, t, frame)   ← SPICE / DE440
      │  posição e velocidade dos corpos massivos
      ▼
PointMassGravity.evaluate(spacecraft_state, t)
      │  a = Σ -GM_i (r - r_i)/|r - r_i|³
      ▼
CompositeForceModel  (+ propulsão, + correções relativísticas)
      │
      ▼
DormandPrince54Propagator   passo adaptativo, controle de erro
      │
      ▼
PropagationState(t+Δt)
```

A nave nunca entra no cálculo do movimento dos corpos: ver ADR-0003 e
`docs/physics/gravity-model.md` §2 (aproximação de partícula-teste).

## 5. Modelo de tempo

Três relógios distintos, nunca misturados (ver `docs/architecture/coordinate-system.md` §6):

* **coordinate time** — TDB, segundos desde J2000; é o parâmetro de integração;
* **proper time** — tempo próprio da nave, integrado como variável de estado;
* **wall / render time** — `std::chrono::steady_clock`, só existe fora do core
  científico, em `SimulationClock`.

`SimulationClock` traduz tempo de parede em avanço de tempo coordenado usando o
fator de time warp. O warp altera **quanto** tempo coordenado se pede ao
propagador por frame; nunca altera o passo interno do integrador, que é
escolhido pelo controle de erro.

## 6. Precisão e unidades

* Unidades internas: **SI estrito** (m, m/s, kg, s, N, rad). CSPICE devolve km e
  km/s; a conversão acontece uma única vez, dentro de `SpiceEphemerisProvider`.
* Tipo numérico: `double` em todo o core. Ver `docs/architecture/coordinate-system.md` §7
  para o orçamento de erro de arredondamento.
* `float` só existe na fronteira com o renderizador, depois do
  `RenderTransform` ter subtraído a origem da câmera.

## 7. Erros e diagnóstico

* Erros de programação → `assert` / contratos.
* Erros de dados e de ambiente (kernel ausente, corpo sem cobertura na janela
  temporal, época fora do SPK) → exceções tipadas (`SpiceError`,
  `EphemerisUnavailable`), nunca `SIGABRT` do SPICE.
* O tratamento de erro do CSPICE é colocado em modo `RETURN` na inicialização;
  `SpiceEphemerisProvider` verifica `failed_c()` após cada chamada, extrai a
  mensagem longa e a converte em exceção C++. O toolkit **nunca** aborta o processo.
* O propagador reporta falha numérica (passo mínimo atingido, NaN) como
  resultado explícito, não como exceção silenciosa.

## 8. Concorrência

Milestone 0 é single-thread. As decisões que preservam a opção futura:

* `EphemerisProvider::state()` é `const`; a implementação SPICE é protegida por
  mutex porque o CSPICE tem estado global e **não** é thread-safe;
* nenhum estado global mutável no core;
* `ForceModel` é `const` e reentrante.

## 9. Estratégia de testes

Quatro categorias, refletidas em `tests/`:

| Diretório | Pergunta que responde | Depende de kernels |
|---|---|---|
| `unit` | a aritmética está certa? | não |
| `integration` | as peças conversam? SPICE carrega? | sim |
| `scientific` | o resultado bate com a natureza/referência? | sim (parte) |
| `regression` | o resultado de ontem continua valendo? | sim |

Testes que exigem kernels e não os encontram retornam código 77 e aparecem como
`Skipped` no CTest, nunca como falha silenciosa ou verde falso.

Além dessas, fora do CTest padrão de física:

| Rótulo | Alvo | O que verifica | Precisa de |
|---|---|---|---|
| `godot` | `godot-tests` | a camada de apresentação (cockpit, mapa, missão) | Godot + GDExtension |
| `assets` | `asset-validation` | texturas: proporção, costuras, convenção de normal map | `python3` |
| `gpu` | `gpu-validation` | pixels do céu e da renderização relativística | tela real |

`ctest-headless` roda só as quatro categorias da tabela anterior.

Nenhuma tolerância pode ser um número mágico: o harness de testes
(`tests/support/test_harness.hpp`) **exige** uma string de justificativa em cada
comparação aproximada. Ver `docs/validation/tolerances.md`.

## 10. Fora de escopo no Milestone 0

Registrado para evitar ambiguidade: sem Godot, sem atitude/quaternions, sem
propulsão, sem relatividade, sem SOI, sem gráficos, sem gameplay.
O que existe: tempo, referenciais, efemérides, gravidade de N corpos pontuais,
propagação com controle de erro, CLI e testes.

## 11. Mapa do repositório

```text
spaceflight/
├── CMakeLists.txt          raiz; alvos, opções, política de warnings
├── cmake/                  cspice.cmake (CSPICE como alvo próprio), warnings.cmake
├── config/engines/         motores da nave em JSON com comentários (ADR-0007)
├── catalogs/               Yale BSC5 (fora do Git; MANIFEST.md)
├── kernels/spice/          LSK, PCK, SPK (fora do Git; MANIFEST.md + SHA256SUMS)
├── external/               CSPICE, godot-cpp, editor Godot, dados brutos (fora do Git)
├── scripts/                fetch_*, campanhas, validação de GPU/texturas, manual
│
├── core/                   libspaceflight_core.a -- sem Godot, sem main()
│   ├── math/               Vec3, Mat3
│   ├── units/              constantes SI com proveniência, conversões
│   ├── time/               CoordinateTime (duas partes), Duration, escalas
│   ├── coordinates/        ReferenceFrame (origem + eixos), StateVector
│   ├── celestial/          BodyId (NAIF), BodyCatalog, tabela do Sistema Solar
│   ├── ephemeris/          EphemerisProvider, SpiceEphemerisProvider
│   ├── gravity/            pontual, J2, composto, WeakFieldMetric
│   ├── propagation/        Dormand-Prince 5(4), dense output, estatísticas
│   ├── trajectory/         elementos osculadores (diagnóstico), Lambert
│   ├── propulsion/         motor: F = eta*q*w, contabilidade de energia
│   ├── navigation/         manobras, executor, planejador, targeting
│   ├── attitude/           inércia, RCS, controle de apontamento
│   ├── autopilot/          guiagem de queimas
│   ├── relativity/         cinemática em u = γv, óptica, tempo de luz
│   ├── spacecraft/         SpacecraftState
│   ├── simulation/         SimulationClock, SimulationSnapshot
│   ├── render/             RenderTransform: absoluto → câmera → float
│   └── config/             leitor de JSON com comentários
│
├── tools/
│   ├── orbit-cli/          propagar, interceptar, consultar corpos, CSV
│   ├── lunar-campaign/     campanha Terra-Lua por época
│   ├── gr-reference/       emissor de trajetórias para validação cruzada
│   └── validation/         comparação contra REBOUNDx, gráficos
│
├── tests/
│   ├── support/            harness que exige justificativa de tolerância
│   ├── unit/ integration/ scientific/ regression/
│   └── scenarios/          cenários JSON para orbit-cli
│
├── godot/
│   ├── gdextension/        ponte C++ (godot-cpp); desligada por padrão
│   └── project/            projeto Godot 4.5: cenas, scripts, shaders, testes
│
└── docs/
    ├── adr/                decisões de arquitetura
    ├── architecture/       este documento e os demais de arquitetura
    ├── physics/            derivação e domínio de validade de cada modelo
    ├── validation/         relatórios de milestone, campanhas, tolerances.md
    ├── gameplay/           controles (gerado do Input Map), cockpit, mapa
    ├── assets/             manifesto e especificação de cada asset
    └── manual/             manual do piloto; manual.pdf gerado por scripts/build_manual.sh
```
