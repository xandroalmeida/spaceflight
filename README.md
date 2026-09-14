# spaceflight

Simulador científico de voo espacial no Sistema Solar.

O núcleo é uma biblioteca C++20 independente do engine gráfico: física orbital,
efemérides JPL, propagação com controle de erro e uma CLI de verificação. O Godot
entra depois, como consumidor de snapshots (ADR-0002).

**Estado: Milestone 7 concluído — primeiro vertical slice jogável.**

Os Milestones 0–6 construíram e qualificaram o núcleo científico: efemérides JPL,
gravidade de N corpos com J₂, propagação com dense output e controle de erro,
propulsão, manobras, Lambert com targeting diferencial, atitude de corpo rígido
com RCS e apontamento, propulsão relativística em espaço plano, gravidade como
geometria, a renderização relativística inteira — tempo de trânsito da luz,
aberração, Doppler e *beaming* limitado à banda visível, rotação de Terrell por
vértice, um céu de 8 786 estrelas reais do Yale BSC5 — e um planejador Terra–Lua
qualificado contra 365 épocas.

O **Milestone 7** transformou isso num simulador que se pilota: um cockpit
tridimensional, uma nave com geometria, instrumentos por função, controle manual
de atitude e de motor, cinco modos de câmera, mapa orbital, computador de bordo
com `EXECUTE`/`CANCEL`, e a missão Terra–Lua voável do princípio ao fim. O
relatório é [`docs/validation/milestone-7-report.md`](docs/validation/milestone-7-report.md);
as imagens da demonstração estão em `docs/validation/m7/`.

**38 suítes de teste**, das quais 21 na categoria `scientific` — a que
`tests/CMakeLists.txt` define como *"does the answer match nature / an external
reference?"* — comparando contra o JPL Horizons/SPICE, o REBOUNDx, o lugar
planckiano da CIE, o Yale BSC5 ou soluções analíticas fechadas; mais 137
verificações da camada de apresentação, que não repetem uma única conta da
física.

```bash
cmake --build build --target ctest-headless   # a física, sem tela
cmake --build build --target godot-tests      # a apresentação, sem tela
cmake --build build --target gpu-validation   # os pixels, precisa de tela
```

---

## Começar

```bash
./scripts/fetch_kernels.sh          # ~33 MB de kernels SPICE (DE440)
./scripts/fetch_star_catalog.sh     # 560 kB: Yale BSC5, o céu a olho nu
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
│   ├── fetch_kernels.sh        baixa LSK + PCK + DE440
│   ├── lunar_campaign.sh       a campanha Terra-Lua, em processos paralelos
│   └── starfield_validation.sh o arnês de dez estágios do campo de estrelas
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
│   ├── adr/                           0001 linguagem .. 0008 representação de atitude
│   ├── manual/
│   │   ├── *.md                       o manual do piloto, um capítulo por arquivo
│   │   ├── manual.css                 a folha de estilo da impressão
│   │   └── manual.pdf                 GERADO por scripts/build_manual.sh
│   ├── gameplay/
│   │   ├── controls.md                GERADO do Input Map; não editar à mão
│   │   └── cockpit.md                 o que cada mostrador diz e de onde vem
│   ├── assets/
│   │   ├── manifest.md                estado de cada asset; o que falta gerar
│   │   ├── import-settings.md         como importar cada tipo, e porquê
│   │   └── planets|spacecraft|cockpit prompts prontos para o Codex
│   └── validation/
│       ├── tolerances.md              toda tolerância, medida e justificada
│       ├── milestone-7-report.md      o que é jogável, e o que ficou por fazer
│       ├── m7-playtest.md             o checklist manual da Definition of Done
│       └── m7/                        a demonstração do M7, fotografada
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
    └── project/                projeto Godot 4.5: cena, câmera, HUD
        └── shaders/            cor e brilho a partir de D; Terrell por vértice
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

A extensão é **desligada por padrão**, e isso é o teste: o core e as 32 suítes
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
| `WeakFieldStaticMetric` | `u = dx/dτ` | métrica estática fraca (`WeakFieldMetric`) |

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

## Milestone 6.1: os dois bloqueadores do Milestone 6

O Milestone 6 ficou aberto por duas coisas, e as duas eram defeitos que o próprio
instrumento de medida escondia.

**A campanha Terra–Lua** reportava 18 sucessos em 100 épocas e todas as falhas
diziam que o solver havia convergido. Ele havia — sobre trajetórias que
atravessam a Terra: em 52 das 100 épocas a propagação parava com
`trajectory entered Earth` seis minutos depois da ignição, **depois** de um
estágio de plano B reportar `converged`. A causa não era o corretor; era que o
pipeline não tinha busca nenhuma, e 82,5 % das geometrias que o calendário
entregava são invoáveis a partir de uma órbita de 400 km. Hoje: **100/100** nas
mesmas 100 épocas e **365/365** ao longo de um ano, com um critério de sucesso
bem mais estrito (a órbita pedida, não `ε < 0`) e uma taxonomia de 20 razões em
que nada falha como "solver failed".

```bash
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --map
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --epochs 100
scripts/lunar_campaign.sh 365 8
```

**O starfield** carregava 8 786 estrelas e não mostrava nenhuma — e "invisível" é
o sintoma que todo defeito possível compartilha. Um arnês de dez estágios separou
as hipóteses e achou **dois** defeitos, o primeiro escondendo o segundo:
`render_mode unshaded` faz o Forward+ do Godot 4 descartar `EMISSION` (as
estrelas eram desenhadas em preto sobre preto), e a esfera do céu ficava abaixo
de um passo de quantização do buffer de profundidade de 24 bits.

```bash
./scripts/starfield_validation.sh     # precisa de tela; --headless não desenha nada
```

Relatório: [`docs/validation/milestone-6-1-report.md`](docs/validation/milestone-6-1-report.md).

## Milestone 6.2 — um planejador, não dois

Os 365/365 acima descreviam `core/navigation/lunar_transfer.hpp`. O jogo, quando
o piloto apertava **J**, executava outra coisa: 532 linhas de astrodinâmica
dentro do GDExtension, com a sua própria busca de partida, a sua própria grade de
tempo de voo e o seu próprio corretor de plano B — e **sem** a triagem da cônica
de partida, que é o mecanismo por trás de 46 das 59 falhas do Milestone 6. A
campanha certificava software que ninguém executava.

As 469 linhas foram **apagadas**. Campanha e jogo entram pela mesma porta,
`sf::navigation::plan_lunar_transfer`, e a campanha refeita por esse caminho dá
**365/365 bit a bit idênticos** aos anteriores.

```bash
scripts/lunar_campaign.sh 365 8                     # o caminho público
scripts/execution_campaign.sh 30 8                  # planner + autopiloto + queima finita
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --autopilot-sweep
ctest --test-dir build -R planner_equivalence       # a ponte não pode divergir de novo
```

**O autopiloto** entregava `e ≈ 0,0105` contra `0,0017` das queimas finitas. Não
era ruído: é o erro de regime de um PD seguindo uma rampa, `θ = 2ζω/ω_n`, que
prevê o pico medido com 1,4 % — na periapse de uma órbita lunar de 100 km o
retrógrado gira a 8,886·10⁻⁴ rad/s e a 0,05 rad/s de banda isso são 2,036°
permanentes, contra 2,064° medidos. A
varredura confirma a lei `1/ω_n` a 0,4 % sobre um fator de quatro em ganho, e
`ω_n` passou para 0,20 rad/s — o menor que satisfaz `pico < 1°` e `média < 0,5°`.

E os testes deixaram de fingir que cobrem gráficos:

```bash
cmake --build build --target ctest-headless    # aritmética, roda em qualquer lugar
cmake --build build --target gpu-validation    # precisa de framebuffer real
```

Relatório: [`docs/validation/milestone-6-2-report.md`](docs/validation/milestone-6-2-report.md).
Arquitetura: [`docs/architecture/navigation-integration.md`](docs/architecture/navigation-integration.md).

## Milestone 7 — o primeiro cockpit jogável

O que o M7 acrescentou é **imagem e interação**, não física. Nenhum modelo foi
redesenhado; o que mudou é que agora dá para sentar na nave.

```
antes                                     agora
─────────────────────────────────────     ──────────────────────────────────────
a nave: uma caixa amarela de 20 km        22 m de veículo com cockpit, habitat,
                                          tanques, radiadores, motor, antenas
não havia cockpit                         interior 3D com quatro mostradores,
                                          sete botões clicáveis, quatro lâmpadas
uma câmera orbital                        cinco modos, e DUAS câmeras por modo
52 números monoespaçados                  instrumentos por função; os 52 números
                                          continuam, atrás de F3
a Terra: uma esfera azul                  continentes, nuvens, luzes noturnas,
                                          limbo, e rodando pelos kernels
sem som                                   som estrutural dentro da nave, e
                                          silêncio na câmera externa
```

Três coisas que este milestone obrigou a construir e que valem ser ditas:

**As texturas encontraram três defeitos que a esfera lisa escondia.** A rotação
dos corpos estava transposta — as linhas da matriz do SPICE entregues a um
construtor de `Basis` que recebe colunas, ou seja, a Terra girava ao contrário. O
polo da malha (`+y` de `SphereMesh`) não é o polo do corpo (`+z` fixo ao corpo),
e sem conversão as calotas iam para o equador. E a costura de UV desenhava uma
linha de polo a polo, porque em `u = 0 ≡ 1` a derivada salta e a GPU escolhe o
mipmap mais grosseiro. Nenhum dos três é detectável por aritmética; os três
foram fechados contra um facto externo: às 00:00 UTC o ponto subsolar tem de
estar perto de 180° E, e agora ele cai no Pacífico e o antissolar no Saara.

E um normal map da Lua foi **recusado por medição**: sobre os mares, que são as
superfícies mais lisas que existem lá, ele lê `(50, 54, 247)` em vez de
`(128, 128, 255)`, e os canais R e G correlacionam-se com o albedo em vez da
inclinação. É uma imagem de relevo tingida, não um normal map.

**Duas escalas, uma câmera.** A cena tem 1 unidade = 10⁶ m e o plano próximo tem
de ser 50 km, ou as estrelas somem por quantização de profundidade. A nave tem
22 m — duas mil vezes dentro desse plano. São duas câmeras com a mesma orientação
e o mesmo campo de visão, uma em unidades de cena e outra em metros, e a
conversão entre elas é uma multiplicação num lugar só. Elas não são duas câmeras
que se seguem: são a mesma câmera em duas unidades, e é por isso que a paralaxe
entre a nave e o planeta atrás dela sai certa em vez de ser ajustada.

**O desenho lê o atuador.** `RcsForce::throttles()` é a mesma função que o modelo
de forças voa e que o renderizador consome — não há arranjo de código em que o
jato desenhado e o propelente queimado discordem. Um torque puro abre dois bicos;
um diagonal abre quatro, a frações diferentes; e o teste fixa os quatro casos,
incluindo o de "sem comando, nada acende", que é o único que uma implementação
guiada pela tecla acertaria por acidente.

**Os marcadores vêm do piloto automático.** `PointingController::direction_for` é
a rotina pela qual o autopilot guia, e é dela que saem as retículas do mostrador
de voo. Alinhar o nariz com o marcador leva a nave ao mesmo sítio por construção.

E uma dívida encontrada e **não** paga: o tensor de inércia do core modela uma
caixa de 8 × 3 × 3 m, que é o casco pressurizado — mas não os treze metros de
tanques, potência e motor que ficam atrás dele. A regra 57 do M7 põe "perfect
spacecraft mass distribution" fora deste milestone, e corrigir o tensor
invalidaria as campanhas de apontamento do M6.2. Está em
[`docs/assets/spacecraft/dimensions.md`](docs/assets/spacecraft/dimensions.md)
com a forma de pagar.

**Manual do piloto:** [`docs/manual/manual.pdf`](docs/manual/manual.pdf) — trinta
e uma páginas com as capturas da demonstração, do primeiro voo à órbita lunar.
A fonte é [`docs/manual/`](docs/manual/), um capítulo por arquivo, e o PDF sai de
`./scripts/build_manual.sh`. Nenhuma tecla é escrita à mão lá dentro: elas vêm do
Input Map, e uma ação renomeada **reprova a compilação** em vez de imprimir a
tecla errada.

Relatório: [`docs/validation/milestone-7-report.md`](docs/validation/milestone-7-report.md) ·
controles: [`docs/gameplay/controls.md`](docs/gameplay/controls.md) ·
cockpit: [`docs/gameplay/cockpit.md`](docs/gameplay/cockpit.md) ·
checklist manual: [`docs/validation/m7-playtest.md`](docs/validation/m7-playtest.md)

## Próximo

O arrasto de referencial (`g₀ᵢ ≠ 0`): a 0,9 c o termo que a métrica atual joga
fora vale 3,6·10⁻⁴, quatro ordens **acima** dos termos 1PN estáticos que ela
mantém. Enquanto isso não existir, o modo `WeakFieldStaticMetric` explicita que
trajetórias lentas perto de corpos girando e para trajetórias rápidas longe
deles — não para as duas ao mesmo tempo.

Na imagem, o que ficou de fora está listado em
[`docs/architecture/rendering.md`](docs/architecture/rendering.md) §6: a
aberração ainda é rígida por corpo (o disco de um planeta não se distorce ao
atravessar o mapa), a luz anda em linha reta mesmo perto do Sol, e as estrelas
são corpos negros sem linhas espectrais.

No controle, o atraso de rastreio do apontamento foi **comprado com autoridade**,
não removido: `ω_n` subiu de 0,05 para 0,20 rad/s. O *feed-forward* da taxa do
alvo — cuja derivada a lei de guiagem já conhece — levaria o mesmo atraso a zero
sem gastar banda nenhuma, e com ele o ganho poderia voltar a 0,05 com um quarto
do consumo de RCS. Está nomeado em
[`docs/physics/attitude.md`](docs/physics/attitude.md) §7 desde o Milestone 3.

E a inércia do casco (1000 kg) ainda não é a massa da nave (20 000 kg): o mesmo
objeto com duas massas. Está registrado em
[`docs/validation/autopilot-hardening.md`](docs/validation/autopilot-hardening.md)
§6 em vez de silenciosamente consertado, porque consertá-lo multiplica a inércia
por vinte e refaz todos os números daquela página.

O Milestone 7 acrescentou a essa mesma linha uma segunda discrepância, do mesmo
tipo e pela mesma razão: a caixa de 8 × 3 × 3 m que a inércia modela é o casco
pressurizado, e a nave desenhada tem 22 m. As duas pagam-se juntas — derivando o
tensor da geometria — e as duas exigem re-qualificar a campanha em vez de trocar
uma constante.
