# spaceflight

Simulador científico de voo espacial no Sistema Solar.

O núcleo é uma biblioteca C++20 independente do engine gráfico: física orbital,
efemérides JPL, propagação com controle de erro e uma CLI de verificação. O Godot
entra depois, como consumidor de snapshots (ADR-0002).

**Estado: Milestone 1 concluído.** Núcleo científico, efemérides JPL, gravidade de
N corpos com J₂, propagação com dense output, propulsão, planejamento e execução
de manobras, Lambert e targeting diferencial. Sem gráficos, sem gameplay, sem
relatividade. 18 suítes de teste, das quais 10 comparam resultados contra o JPL
Horizons ou contra soluções analíticas fechadas.

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
./build/bin/orbit-cli propagate tests/scenarios/leo-raise-apoapsis.json
./build/bin/orbit-cli intercept tests/scenarios/lunar-intercept.json --to Moon --tof 4.5
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
│   │   ├── coordinate-system.md       SSB/J2000, TDB, orçamento de erro do double
│   │   └── navigation.md              planejar × executar, perda gravitacional, descontinuidades
│   ├── physics/
│   │   ├── gravity-model.md           N corpos pontuais, domínio de validade, o que falta
│   │   ├── geopotential.md            J2: forma sem referencial girante, constantes, testes
│   │   ├── propulsion-model.md        F = eta*q*w derivado de conservação; o que eta custa
│   │   ├── lambert.md                 variáveis universais, casos degenerados, targeting
│   │   ├── relativity-roadmap.md      formulação alvo: u = gamma*v, geodésica exata
│   │   └── propulsion-model.md        foguete relativístico derivado de conservação
│   ├── adr/                           0001 linguagem .. 0007 formato de configuração
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
│   ├── gravity/                ForceModel, PointMassGravity, OblatenessGravity (J2), Composite
│   ├── propagation/            SpacecraftPropagator, Dormand-Prince 5(4), dense output, estatísticas
│   ├── trajectory/             elementos osculadores (diagnóstico), Lambert
│   ├── propulsion/             motor: F = eta*q*w, contabilidade de energia
│   ├── navigation/             manobras, executor, missão, planejador, targeting
│   ├── config/                 leitor de JSON com comentários (ADR-0007)
│   ├── spacecraft/             SpacecraftState
│   ├── simulation/             SimulationClock (wall / coordinate / proper / render)
│   ├── relativity/             Milestone 4  (vazio: precisa do documento antes)
│   ├── attitude/               Milestone 3
│   ├── autopilot/              Milestone 3
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
| 0006 | Dense output de 4ª ordem: estado em qualquer instante, sem forçar o passo |
| 0007 | Configuração em JSON com comentários, parser no core |

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
100% tests passed, 0 tests failed out of 18
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
* uma época fora da cobertura do kernel produz **erro**, nunca extrapolação;
* com J₂ ligado, a regressão nodal de uma órbita a 400 km e 51,6° sai em
  **−5,002 °/dia** — o valor da ISS — e `L·n̂` (invariante exato de um campo
  axialmente simétrico) se conserva a 4·10⁻¹² enquanto `|L|` deriva 3·10⁻⁴;
* gravar dense output não altera a trajetória em **um único bit**, e amostrar
  100 000 estados de uma órbita não custa nenhuma avaliação de força;
* uma queima em espaço livre reproduz Tsiolkovsky a 10⁻¹⁰, e o modelo de motor
  entrega o limite do foguete de fótons (`F = P/c`) e o **defeito de massa** de
  uma reação química (4,5·10⁻¹⁰) sem ter sido construído para nenhum dos dois;
* a perda gravitacional escala com `(nΔt)²`: dez vezes o empuxo reduz a perda por
  um fator **99,2**, contra os 100 previstos;
* uma transferência de Hohmann LEO→GEO executada com duas queimas chega em órbita
  circular com `e = 4,9·10⁻⁶`;
* Lambert reconstrói a velocidade de um arco conhecido a 10⁻¹¹, e o targeting
  diferencial leva um intercepto lunar de **267 573 km** de erro para **3,5 km**.

Toda tolerância acima tem origem declarada em `docs/validation/tolerances.md`.

## Milestone 1, item por item

| Enunciado §34 | Como verificar |
|---|---|
| propagar 1 órbita | `orbit-cli propagate tests/scenarios/leo-circular.json` |
| propagar 1 dia | `orbit-cli propagate tests/scenarios/leo-j2-nodal-regression.json` |
| executar burn | `orbit-cli propagate tests/scenarios/leo-raise-apoapsis.json` |
| alterar apoastro | idem — 6 778 km → 19 950 km, com a perda gravitacional reportada |
| escapar da Terra | `orbit-cli propagate tests/scenarios/earth-escape.json` — energia cruza zero |
| interceptar região da Lua | `orbit-cli intercept tests/scenarios/lunar-intercept.json --to Moon --tof 4.5` |

## Próximo

Milestone 2: Godot como consumidor de snapshots — câmera externa, Terra, Lua,
nave, starfield, floating origin, time warp. O core já entrega tudo de que ele
precisa (`Trajectory::state_at` em qualquer instante, sem forçar o passo).
