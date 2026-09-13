# spaceflight

Simulador científico de voo espacial no Sistema Solar.

O núcleo é uma biblioteca C++20 independente do engine gráfico: física orbital,
efemérides JPL, propagação com controle de erro e uma CLI de verificação. O Godot
entra depois, como consumidor de snapshots (ADR-0002).

**Estado: Milestone 4 concluído.** Núcleo científico, efemérides JPL, gravidade de
N corpos com J₂, propagação com dense output, propulsão, manobras, Lambert com
targeting diferencial, e a camada de renderização (snapshot, origem flutuante,
GDExtension para o Godot 4.5), atitude de corpo rígido com RCS e apontamento, e o
cockpit, propulsão relativística em espaço plano, e a óptica do Milestone 5 no
núcleo. Sem gameplay. 24 suítes de teste, das quais 14 comparam resultados contra
o JPL Horizons/SPICE ou contra soluções analíticas fechadas.

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
│   │   ├── navigation.md              planejar × executar, perda gravitacional, descontinuidades
│   │   └── rendering.md               origem flutuante, double→float, o que a imagem ainda mente
│   ├── physics/
│   │   ├── gravity-model.md           N corpos pontuais, domínio de validade, o que falta
│   │   ├── geopotential.md            J2: forma sem referencial girante, constantes, testes
│   │   ├── propulsion-model.md        F = eta*q*w derivado de conservação; o que eta custa
│   │   ├── lambert.md                 variáveis universais, casos degenerados, targeting
│   │   ├── attitude.md                Euler, quaternions, RCS, eixo intermediário
│   │   ├── relativistic-propulsion.md redução em componentes, foguete, limites
│   │   ├── relativistic-rendering.md   tempo de luz, aberração, Doppler, Terrell
│   │   ├── relativistic-gravity.md    métrica de campo fraco, geodésica, Mercúrio/GPS/Shapiro
│   │   ├── spin-transport.md          Fermi-Walker, Thomas, geodética, Gravity Probe B
│   │   ├── relativity-roadmap.md      formulação alvo: u = gamma*v, geodésica exata
│   │   └── propulsion-model.md        foguete relativístico derivado de conservação
│   ├── adr/                           0001 linguagem .. 0007 formato de configuração
│   └── validation/
│       └── tolerances.md              toda tolerância, medida e justificada
│
├── external/                   CSPICE (fora do Git)
├── kernels/spice/              LSK, PCK, SPK (fora do Git; MANIFEST.md + SHA256SUMS)
│
├── tools/
│   ├── orbit-cli/              propagar, interceptar, CSV
│   ├── gr-reference/           emissor de trajetórias para validação cruzada (§29)
│   └── validation/             comparação contra REBOUNDx, gráficos
│
├── core/                       libspaceflight_core.a  -- sem Godot, sem main()
│   ├── math/                   Vec3, Mat3
│   ├── units/                  constantes SI com proveniência, conversões
│   ├── time/                   CoordinateTime (duas partes), Duration, escalas
│   ├── coordinates/            ReferenceFrame (origem + eixos), StateVector
│   ├── celestial/              BodyId (NAIF), CelestialBody, BodyCatalog
│   ├── ephemeris/              EphemerisProvider, SpiceEphemerisProvider, kernels, erros
│   ├── gravity/                ForceModel, PointMassGravity, OblatenessGravity (J2), Composite, WeakFieldMetric
│   ├── propagation/            SpacecraftPropagator, Dormand-Prince 5(4), dense output, estatísticas
│   ├── trajectory/             elementos osculadores (diagnóstico), Lambert
│   ├── propulsion/             motor: F = eta*q*w, contabilidade de energia
│   ├── navigation/             manobras, executor, missão, planejador, targeting
│   ├── config/                 leitor de JSON com comentários (ADR-0007)
│   ├── spacecraft/             SpacecraftState
│   ├── simulation/             SimulationClock, SimulationSnapshot
│   ├── render/                 RenderTransform: absoluto → câmera → float
│   ├── relativity/             cinemática em u = γv, óptica, tempo de luz
│   ├── attitude/               inércia, RCS, controle de apontamento PD
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
└── godot/
    ├── gdextension/            ponte C++ (godot-cpp); desligada por padrão
    └── project/                projeto Godot 4.5: cena, câmera, HUD, starfield
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
| 0008 | Quaternions: escalar primeiro, Hamilton, corpo→inercial |

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
100% tests passed, 0 tests failed out of 24
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
  diferencial leva um intercepto lunar de **267 573 km** de erro para **3,5 km**;
* o solver de tempo de luz concorda com a correção convergida do próprio SPICE
  até o último bit — 0 m para Sol e Marte, 3·10⁻⁵ m para Júpiter, que é o ulp de
  subtrair duas posições baricêntricas;
* um casco de 1 t com 19 t de propelente e exaustão a `0,5 c` chega a
  `β = 0,904762` — exatamente o que a equação do foguete prevê — depois de **oito
  anos de queima**, cobrindo 12,5 anos-luz enquanto o relógio de bordo marca
  12,16 anos contra 19 coordenados;
* com aceleração própria de 1 g durante um ano, `β`, posição e tempo próprio
  batem com o movimento hiperbólico exato até os dígitos impressos, e a equação
  do foguete relativística vale partindo de `β = 0`, `0,5` ou `0,9` — porque é
  escrita em rapidez, que é aditiva;
* o mesmo empuxo a `β = 0,9` acelera `γ = 2,29416` vezes menos de través do que
  ao longo do movimento, exatamente;
* a rotação livre de torque conserva o **vetor** momento angular a 9·10⁻¹², um
  pião simétrico precessa na taxa analítica, e a instabilidade do eixo
  intermediário (efeito Dzhanibekov) bate com `cosh`/`sinh` da solução
  linearizada em **6 dígitos**, sinal incluído;
* sem origem flutuante, 1 km de movimento a 1 UA **desaparece** no `float` (a
  resolução lá é 17,8 km); com a câmera a 100 m da nave, 1 mm sobrevive — e a
  escala de cena não muda nada disso, porque o `float` tem precisão relativa.

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

## Godot (Milestone 2)

```bash
./scripts/fetch_godot_cpp.sh        # bindings C++ (godot-4.5-stable)
./scripts/fetch_godot.sh            # o editor 4.5-stable, em external/
cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build-godot --target spaceflight_gdextension -j
./scripts/run_godot_headless.sh     # roda a cena sem tela e imprime o HUD
```

A extensão é **desligada por padrão**, e isso é o teste: o core e as 26 suítes
compilam e passam sem nenhum engine instalado.

A cena foi verificada **headless**: extensão carregada, kernels lidos, propagação
avançando, HUD com `altitude 400,000 km`, `i = 51,6000°`, `T = 5544,87 s`,
Lua a 358 366 km e resolução de renderização de 0,807 m — que é exatamente
`6771 km · ε_float`. Detalhes, controles e as duas armadilhas do Godot que isso
revelou estão em `godot/README.md`.

## Milestone 5: a física relativística

Três modos de cinemática, escolhidos em `IntegratorConfig::kinematics`:

| Modo | Variável de estado | Gravidade |
|---|---|---|
| `Newtonian` | `v` | soma de acelerações (`ForceModel`) |
| `SpecialRelativistic` | `u = γv` | nenhuma — o propagador recusa |
| `GeneralRelativistic` | `u = dx/dτ` | a geometria (`WeakFieldMetric`) |

O teto de velocidade em nenhum dos dois últimos é uma comparação: é a forma de
`γ = √(1 + |u|²/c²)` e de `u⁰`. Não há linha no código onde `if (v > c) v = c`
pudesse ser escrita, porque `v` nunca é a variável integrada.

Contra números medidos antes de haver teoria para eles:

| | modelo | observado |
|---|---|---|
| precessão do periélio de Mercúrio | **42,985″/século** | 42,98″ |
| GPS menos relógio de solo | **38,505 µs/dia** | 38,51 µs |
| atraso de Shapiro, Terra–Vênus rasante | **116,282 µs** | 116,280 µs |
| deflexão ultrarrelativística / newtoniana | **1,9999999** | 2 |
| precessão geodética do Gravity Probe B | **6 603,88 mas/ano** | 6 601,8 ± 18,3 |
| precessão geodética da Lua (LLR) | **19,188 mas/ano** | 19,2 |

O fator 2 de Eddington não está programado em lugar nenhum: sai das formas de
`A` e `B`. A mesma fórmula dá 1,0000000 para uma partícula lenta.

E a orientação muda **sem que nada gire a nave**: uma volta completa a `β = 0,8`
sob empuxo transversal, com os giroscópios travados e torque zero, deixa o
quaternion girado em `4,188790 rad` — contra `2π(γ−1) = 4,188790`, a rotação de
Wigner acumulada. Derivação em
[`docs/physics/spin-transport.md`](docs/physics/spin-transport.md).

Derivação, orçamento de erro e o que foi desprezado (arrasto de referencial, a
maior dívida): [`docs/physics/relativistic-gravity.md`](docs/physics/relativistic-gravity.md)
e [`docs/physics/spin-transport.md`](docs/physics/spin-transport.md).
Toda tolerância: [`docs/validation/tolerances.md`](docs/validation/tolerances.md) §3.13 e §3.14.

### Validação cruzada contra outro código (§29)

A geodésica foi comparada com o operador `gr` do REBOUNDx — a força 1PN de
Anderson *et al.* usada em efemérides — no mesmo problema, sem nenhuma linha de
código em comum:

```bash
cmake --build build --target gr-reference
./tools/validation/reboundx_cross_check.py
```

Depois de 40 órbitas de Mercúrio as duas trajetórias estão a **2 600 km** uma da
outra, e isso não é um problema: o avanço do periélio por órbita — a quantidade
invariante — concorda em **2,7 partes por milhão** (−0,0001″/século contra 43).
Toda a separação é *quando* a partícula está onde. Uma única constante
(1,46·10⁻⁷ de movimento médio) absorve 99,6 % dela, e o que sobra é **limitado**
em 1,17·10⁴ m = 7,96 × `GM/c²` — a escala em que dois sistemas de coordenadas
1PN podem legitimamente diferir. O controle newtoniano, com relatividade
desligada nos dois códigos, fica em **98 m**.

Leitura completa em [`tools/validation/README.md`](tools/validation/README.md).

## Próximo

A metade visual do Milestone 5: shaders de cor e brilho a partir do fator Doppler,
starfield vindo de um catálogo real em vez de ruído, e o deslocamento por vértice
que produz a rotação de Terrell **sozinha** — não como efeito, mas como
consequência do tempo de trânsito aplicado ponto a ponto.

Depois dela, o arrasto de referencial (`g₀ᵢ ≠ 0`): a 0,9 c o termo que a métrica
atual joga fora vale 3,6·10⁻⁴, quatro ordens **acima** dos termos 1PN estáticos
que ela mantém. Enquanto isso não existir, o modo `GeneralRelativistic` é honesto
para trajetórias lentas perto de corpos girando e para trajetórias rápidas longe
deles — não para as duas ao mesmo tempo.
