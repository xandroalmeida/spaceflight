# spaceflight

Simulador científico de voo espacial no Sistema Solar.

O núcleo é uma biblioteca C++20 independente do engine gráfico: física orbital,
efemérides JPL, propagação com controle de erro e uma CLI de verificação. O Godot
entra depois, como consumidor de snapshots (ADR-0002).

**Estado: Milestone 0 concluído.** Sem gráficos, sem gameplay, sem relatividade.
O que existe é uma base verificável: 11 suítes de teste, das quais 4 comparam
resultados contra o JPL Horizons ou contra soluções analíticas fechadas.

---

## Começar

```bash
./scripts/fetch_kernels.sh          # ~33 MB de kernels SPICE (DE440)
cmake -S . -B build                 # baixa e compila o CSPICE na primeira vez
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Sem Godot instalado. Sem rede depois do primeiro `cmake`.

```bash
./build/bin/orbit-cli body Earth --date 2026-01-01
./build/bin/orbit-cli propagate tests/scenarios/leo-circular.json
```

## Estrutura

```text
spaceflight/
├── CMakeLists.txt              raiz; alvos, opções, política de warnings
├── cmake/
│   ├── cspice.cmake            compila o CSPICE (2229 fontes C) como alvo próprio
│   └── warnings.cmake          -Wall -Wextra -Wpedantic -Wconversion ... para o nosso código
├── scripts/
│   ├── fetch_cspice.sh         baixa o toolkit NAIF para o SO/arquitetura corrente
│   └── fetch_kernels.sh        baixa LSK + PCK + DE440
│
├── docs/
│   ├── architecture/
│   │   ├── system-architecture.md     camadas, regra de dependência, fluxo de dados
│   │   └── coordinate-system.md       SSB/J2000, TDB, orçamento de erro do double
│   ├── physics/
│   │   ├── gravity-model.md           N corpos pontuais, domínio de validade, o que falta
│   │   ├── relativity-roadmap.md      formulação alvo: u = gamma*v, geodésica exata
│   │   └── propulsion-model.md        foguete relativístico derivado de conservação
│   ├── adr/                           0001 linguagem .. 0005 propagação
│   └── validation/
│       └── tolerances.md              toda tolerância, medida e justificada
│
├── external/                   CSPICE (fora do Git)
├── kernels/spice/              LSK, PCK, SPK (fora do Git; MANIFEST.md + SHA256SUMS)
│
├── core/                       libspaceflight_core.a  -- sem Godot, sem main()
│   ├── math/                   Vec3, Mat3
│   ├── units/                  constantes SI com proveniência, conversões
│   ├── time/                   CoordinateTime (duas partes), Duration, escalas
│   ├── coordinates/            ReferenceFrame (origem + eixos), StateVector
│   ├── celestial/              BodyId (NAIF), CelestialBody, BodyCatalog
│   ├── ephemeris/              EphemerisProvider, SpiceEphemerisProvider, kernels, erros
│   ├── gravity/                ForceModel, PointMassGravity, CompositeForceModel
│   ├── propagation/            SpacecraftPropagator, Dormand-Prince 5(4), estatísticas
│   ├── trajectory/             elementos orbitais osculadores (diagnóstico)
│   ├── spacecraft/             SpacecraftState
│   ├── simulation/             SimulationClock (wall / coordinate / proper / render)
│   ├── relativity/             Milestone 4  (vazio: precisa do documento antes)
│   ├── propulsion/             Milestone 1
│   ├── attitude/               Milestone 3
│   ├── navigation/ autopilot/  Milestone 1+
│
├── tools/
│   ├── orbit-cli/              kernels, bodies, time, body, elements, gravity, propagate
│   └── validation/             comparações em lote (Milestone 1+)
│
├── tests/
│   ├── support/                harness que EXIGE justificativa de tolerância
│   ├── unit/                   aritmética: Vec3, tempo, elementos, relógio
│   ├── integration/            SPICE carrega? frames compõem? escalas de tempo?
│   ├── scientific/             Horizons, dois corpos, convergência, gravidade real
│   ├── regression/             valores fixados desta build
│   └── scenarios/              cenários JSON para orbit-cli propagate
│
└── godot/                      Milestone 2 (vazio)
```

## Decisões que não se rediscutem sem motivo técnico

Registradas em `docs/adr/`:

| ADR | Decisão |
|---|---|
| 0001 | C++20 no núcleo; C só na fronteira com bibliotecas nativas |
| 0002 | Godot 4.x via GDExtension, apenas renderização; nunca física |
| 0003 | CSPICE + DE440 como fonte autoritativa; nave é partícula-teste |
| 0004 | Estado em SSB / J2000 (ICRF), SI, TDB em representação de duas partes |
| 0005 | Dormand–Prince 5(4) adaptativo, desacoplado de frame e de time warp |

E três regras que valem para tudo o que vier:

1. **Nenhuma Sphere of Influence na física.** Todas as fontes gravitacionais
   contribuem em todo instante (§11 do enunciado).
2. **Nenhum clamp.** `if (v > c) v = c` é proibido; o limite tem que sair da
   formulação (`docs/physics/relativity-roadmap.md` §3).
3. **Documento → teste analítico → implementação → teste numérico.** Nunca
   efeito visual primeiro e física inventada depois (§39).

## O que o Milestone 0 verifica

```
$ ctest --test-dir build
100% tests passed, 0 tests failed out of 11
```

Entre outras coisas:

* posições de Sol, Terra e Lua batem com o JPL Horizons dentro de **2,4 m**
  (a diferença real entre DE440 e DE441 — nós ficamos abaixo dela);
* a definição do baricentro Terra–Lua é recuperada das massas do kernel a
  10⁻¹² relativo;
* uma órbita circular fecha após um período com erro de ~10⁻⁴ m e conserva
  energia a 10⁻¹¹;
* a propagação bate com a solução fechada de Kepler a 6·10⁻¹⁰ relativo ao longo
  de cinco órbitas;
* a aceleração de maré da Lua em LEO reproduz `2GMr/d³` dentro do truncamento
  esperado de 5 %;
* uma época fora da cobertura do kernel produz **erro**, nunca extrapolação.

Toda tolerância acima tem origem declarada em `docs/validation/tolerances.md`.

## Próximo

Milestone 1: propulsão newtoniana, queimas pela CLI, alteração de apoastro,
escape da Terra, interceptação da região lunar.
