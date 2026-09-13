# Resposta ao PROMPT-0

O que foi construído, item por item do enunciado, o que foi além dele, e o que
continua por fazer.

Estado em 2026-09-13: **Milestones 0 a 5 completos; auditoria do Milestone 6 executada**, 32 suítes de teste, 100 %
passando (18 delas na categoria `scientific`). O núcleo compila e é testado **sem nenhum engine instalado** — que é
como se verifica que a separação do ADR-0002 continua real.

```
32 suítes     18 "scientific" (a categoria que tests/CMakeLists.txt define como
              "does the answer match nature / an external reference?")
              10 unit · 3 integration · 1 regression
25 documentos 11 física · 5 arquitetura · 8 ADRs · 1 tolerâncias
```

---

## Parte 1 — Os 40 itens do enunciado

### Fundações (§1–§9)

| § | Pedido | Estado | Onde |
|---|---|---|---|
| 1 | Stack C++20 + Godot | ✅ | `ADR-0001`, `ADR-0002` |
| 2 | Build CMake | ✅ | `CMakeLists.txt`; CSPICE compilado a partir do fonte |
| 3 | Engine gráfico como consumidor | ✅ | `ADR-0002`; alvo **desligado por padrão** — é o teste da separação |
| 4 | Biblioteca principal independente | ✅ | `core/`, sem Godot em lado nenhum |
| 5 | Arquitetura geral | ✅ | `docs/architecture/system-architecture.md` |
| 6 | Efemérides | ✅ | CSPICE + DE440 (`de440s.bsp`), `ADR-0003` |
| 7 | Fonte de verdade astronômica | ✅ | JPL; `GM` dos kernels, **nunca duplicados** em `constants.hpp` |
| 8 | Referencial principal SSB/J2000 | ✅ | `ADR-0004`, `docs/architecture/coordinate-system.md` |
| 9 | Nave como *test particle* | ✅ | a nave não perturba nada |

### Gravidade e propagação (§10–§12)

| § | Pedido | Estado | Onde |
|---|---|---|---|
| 10 | Campo gravitacional de N corpos | ✅ | `core/gravity/`, superposição verificada bit a bit |
| 11 | **Não usar SOI como física** | ✅ | não existe SOI no modelo; a gravidade é sempre a soma |
| 12 | Propagador com controle de erro | ✅ | Dormand–Prince 5(4), `ADR-0005` |

§10 ganhou de brinde o **J₂** (`docs/physics/geopotential.md`), com a regressão
nodal medida em −5,0149 °/dia contra −5,0027 previstos pela teoria secular.

### Relatividade (§13–§15)

| § | Pedido | Estado | Onde |
|---|---|---|---|
| 13 | `\|v\| < c` **sem clamp** | ✅ | estado em **momento relativístico**; `v = u/√(1+u²/c²)` é estruturalmente < c |
| 14 | Relatividade especial completa | ✅ | `docs/physics/relativistic-propulsion.md`, `core/relativity/` |
| 15 | Gravidade relativística | ✅ | métrica de campo fraco 1PN, `docs/physics/relativistic-gravity.md` |

A regra 13 reaparece em cada camada nova, e todas as vezes a resposta foi a mesma
forma: **um limite estrutural, não um corte**.

| Onde | O limite |
|---|---|
| propagador | `u` (momento/massa) é livre em `(−∞, ∞)`; `v` sai < c por construção |
| brilho do céu | curva de Naka–Rushton `L/(L+L½)`, bijeção `[0,∞) → [0,1)` |
| índice da LUT de Planck | `u = T/(T+T₀)`, bijeção também |
| tempo retardado por vértice | discriminante estruturalmente positivo porque `\|v\| < c` |

### Propulsão (§16–§18)

| § | Pedido | Estado |
|---|---|---|
| 16 | Motor fictício | ✅ Fusion Torch Mk III, `config/engines/torch-mk3.json` |
| 17 | Modelo energético | ✅ `docs/physics/propulsion-model.md` |
| 18 | Configuração experimental | ✅ dois modos de uma usina: IMPULSO (0,03 c, 200 kN) e CRUZEIRO (0,5 c, 11,2 kN) |

### Atitude, nave, relógio (§19–§21)

| § | Pedido | Estado | Onde |
|---|---|---|---|
| 19 | Atitude de corpo rígido | ✅ | quaternion escalar-primeiro (`ADR-0008`), RCS, controlador PD |
| 20 | Nave | ✅ | `core/spacecraft/` |
| 21 | Quatro relógios | ✅ | `SimulationClock`; a taxa de quadros **nunca** alcança o integrador |

### Renderização (§22–§25)

| § | Pedido | Estado | Onde |
|---|---|---|---|
| 22 | Godot recebe *snapshots* | ✅ | `SimulationSnapshot` é um **valor**, não uma janela |
| 23 | **Nunca** modificar a posição física para origem flutuante | ✅ | `RenderTransform` só lê; testado bit a bit |
| 24 | Conversão Core → Renderer | ✅ | `double → float` acontece **uma vez**, depois da subtração |
| 25 | Instrumentação | ✅ | cockpit completo |

### Navegação (§26–§28)

| § | Pedido | Estado |
|---|---|---|
| 26 | Computador de navegação | ✅ `core/navigation/` |
| 27 | Lambert Solver | ✅ `docs/physics/lambert.md`, com o caso degenerado a 180° documentado e recusado |
| 28 | Piloto automático | ✅ modos de apontamento; nenhum caminho de uma tecla para a orientação |

### Validação (§29–§32)

| § | Pedido | Estado |
|---|---|---|
| 29 | REBOUND / REBOUNDx | ✅ `tools/validation/reboundx_cross_check.py` — a geodésica vista por outro código |
| 30 | Validação | ✅ |
| 31 | Testes científicos obrigatórios | ✅ **todos os oito** |
| 32 | Tolerâncias justificadas | ✅ **restrição do compilador** |

§32 não é convenção aqui: o harness **não tem sobrecarga sem a string de
justificativa**. Um número mágico não compila. A justificativa é impressa junto
com o resultado, de modo que a saída do teste é a documentação da tolerância.
Inventário completo em `docs/validation/tolerances.md`, 19 seções.

Os oito testes de §31, e onde:

| Pedido | Onde |
|---|---|
| SPICE, posições conhecidas | `test_spice_positions.cpp` — contra Horizons, pior caso 2,42 m |
| órbita circular | `test_two_body.cpp` |
| órbita elíptica | `test_two_body.cpp`, `test_orbital_elements.cpp` |
| Hohmann vs Δv analítico | `test_propulsion.cpp` |
| conservação sem motor | `test_two_body.cpp` — energia 10⁻¹¹, momento angular 1,2·10⁻¹¹ |
| SR a 0,01–0,999 c | `test_relativistic_propulsion.cpp` |
| tempo próprio vs analítico | `test_relativistic_propulsion.cpp` |
| limite newtoniano | oito arquivos o verificam |

### Milestones (§33–§38)

| § | Milestone | Estado |
|---|---|---|
| 33 | 0 — base científica | ✅ `fa7260c` |
| 34 | 1 — propulsão, manobras, Lambert | ✅ `5d9a2c6` |
| 35 | 2 — Godot, origem flutuante, warp | ✅ `e28453a` |
| 36 | 3 — cockpit | ✅ `f50e607` |
| 37 | 4 — propulsão relativística | ✅ `ebb4f1d` |
| 38 | 5 — renderização relativística | ✅ `86af99d` (física) + `e438920` (visual) |

§38 proíbe uma coisa: *"não representar contração de Lorentz simplesmente
escalando meshes"*. A resposta está em `docs/physics/relativistic-rendering.md`
§11.5 e **contradiz a leitura ingênua da proibição** — ver a Parte 3.

### §39 — A regra fundamental

```
modelo matemático → documentação → teste analítico → implementação
                  → teste numérico → visualização
```

Seguida em todas as features. O documento vem **antes**: `b-plane.md` foi escrito
com os números já medidos em protótipo, antes de uma linha de C++. Nunca o
contrário — nenhum efeito visual foi inventado primeiro para depois se procurar
física que combinasse.

---

## Parte 2 — O que foi além do enunciado

### 2.1 Renderização relativística de banda limitada

§38 pede Doppler e *beaming*. O enunciado — e a literatura de jogos — param em
`I' = D⁴I`. Isso é **bolométrico**, e um olho tem banda. Como a magnitude de um
catálogo já é um fluxo **na banda**, o que se observa é

```
F'_V = F_V · D⁴ · η(D·T)/η(T)
```

e a diferença não é cosmética:

| `β = 0,9048` | bolométrico (§38) | banda visível |
|---|---|---|
| à frente | 400× | **51,5×** |
| à ré | 2,5·10⁻³× | **3,3·10⁻⁷×** |

O contraste frente/ré sai de 1,6·10⁵ para **1,6·10⁸**: levar a banda em conta
torna o efeito **mil vezes mais extremo**, não mais suave. E o expoente efetivo
cai de 4 para exatamente **1** no limite de Rayleigh–Jeans, onde `η ∝ T⁻³` e
`D⁴·D⁻³ = D`.

### 2.2 Colorimetria de verdade

Planck → CMF CIE 1931 (ajuste analítico de Wyman/Sloan/Shirley 2013) → XYZ →
sRGB linear, conferido contra o **lugar planckiano publicado**: melhor que
1,1·10⁻³ em `x,y` acima de 3 000 K. A integral é avaliada em **espaço
logarítmico**, porque a ré a `β = 0,99` pede 142 K, onde o integrando vale
`e⁻²²³⁶` e a cromaticidade viraria `0/0`.

### 2.3 Céu real

Yale BSC5, 9 110 registros → **8 786 estrelas**, com as outras 324 contadas e
reportadas em vez de sumirem. E um teste de **tolerância zero**: o cone
`arccos β` contém exatamente o mesmo conjunto de estrelas que o hemisfério em
repouso, para todo `β`. Um erro de sinal ou de convenção `n̂`/`ŝ` quebra na hora.

### 2.4 Plano B e a viagem à Lua

Não pedido em lugar nenhum do enunciado. O `intercept` mirava a **posição** do
centro da Lua — e acertar o centro de um corpo é colidir com ele.

`docs/physics/b-plane.md` + `core/navigation/b_plane.hpp` + 9 testes contra uma
hipérbole **construída**. Resultado, ponta a ponta:

```
stage 1 (posição)  : 363 286,93 km → 1,88 km
stage 2 (plano B)  : r_p 1837,36683 km, pedido 1837,4 km   ← 33 metros
insertion          : 825,35 m/s retrógrado, queima de 82,5 s
orbit about Moon   : 96,9 × 103,1 km, e = 0,0017, período 117,79 min
CAPTURED
```

e a mesma viagem **dentro da cena**, com uma tecla (`J`).

### 2.5 Câmera, HUD e outras coisas que não são física

Câmera orbital com dois referenciais (casco / velocidade), *free-look*, zoom; HUD
que cabe por construção, em duas colunas; leitura do fator Doppler na direção em
que se olha — que transforma virar a cabeça numa **medição**.

### 2.6 Transporte de spin

`docs/physics/spin-transport.md`: precessão de Thomas e geodética. Não pedido.

---

## Parte 3 — Onde a medição contradisse a expectativa

Este é o padrão mais persistente do projeto, e vale mais que qualquer feature.
**Sete vezes** uma hipótese razoável se revelou errada ao ser medida, e nas sete
o código estava certo e a expectativa não — ou vice-versa, e só a medição disse
qual.

| # | Esperado | Medido |
|---|---|---|
| 1 | momento angular conservado exatamente (força central) | RK explícito **não** preserva invariantes quadráticos; 1,2·10⁻¹¹ |
| 2 | GR e Newton divergem em LEO por `U/c² ≈ 1,3·10⁻⁹` | **1,23·10⁻⁸**, nove vezes mais: depois de uma revolução o que separa os dois é a precessão, com seu `6π` |
| 3 | escalar a mesh por `1/γ` é o erro a evitar | **omitir** a contração dá silhueta 160 % fora de circular; ela é um **passo**, e a proibição é usá-la como **resposta** |
| 4 | o guard protege o `look_at` da degenerescência | **o guard era o salto**: 88° de roll num quadro ao cruzar o limiar |
| 5 | a câmera não chegava atrás da nave por gimbal lock | posição perfeitamente contínua; o referencial é que era o da **velocidade** e não o do casco |
| 6 | mirar um ponto a 100 km de altitude | focagem gravitacional ×2,92: daria `r_p` **1 497 km abaixo da superfície** |
| 7 | a partida mais barata é a melhor | a mais barata é a mais **longa** e a mais sensível; o corretor empaca. Escolher voando é o critério certo |

E três erros de leitura — todos os números certos, e o mostrador mentindo:

* ângulo medido a partir do prógrado (relativo à Terra, 7,7 km/s) e `D` a partir
  de `β` (baricêntrico, 30,7 km/s), a 30° um do outro;
* rótulo dizendo `prograde` depois de a câmera ter orbitado para longe;
* HUD abrindo em `free` porque os valores iniciais divergiam do preset.

---

## Parte 4 — O que **não** está feito

Registado aqui pelo mesmo motivo que `rendering.md` §6 registava as dívidas da
imagem: para não ser descoberto como bug.

**Física**

* **Arrasto de referencial** (`g₀ᵢ ≠ 0`). A 0,9 c o termo que a métrica atual joga
  fora vale 3,6·10⁻⁴, **quatro ordens acima** dos termos 1PN estáticos que ela
  mantém. O modo `WeakFieldStaticMetric` é honesto para trajetórias lentas perto de
  corpos girando e para trajetórias rápidas longe deles — não para as duas ao
  mesmo tempo.
* Sem lente gravitacional nem atraso de Shapiro.
* A Lua é um ponto: `J₂ = 2,03·10⁻⁴` e `C₂₂` comparável não entram, e um periapse
  lunar baixo sente.

**Imagem**

* **Aberração rígida por corpo**: o disco de um planeta não se distorce ao
  atravessar o mapa; só o centro se move.
* Estrelas como corpos negros, sem linhas espectrais.
* Sem extinção interestelar, atmosfera ou eclipses.

**Navegação**

* O planejamento de missão na cena **não é determinístico**: se o corretor fecha
  depende da geometria de partida. Tentam-se três candidatos e recusa-se com o
  motivo se nenhum fechar. Três execuções seguidas fecharam — amostra pequena,
  não prova.
* `B·T`/`B·R` são mirados, mas o **ângulo** do plano B é um parâmetro e não uma
  otimização: não há escolha automática de plano de órbita lunar.

**Verificação**

* ⚠️ **Nada da metade visual foi visto numa tela por quem a escreveu.** Toda a
  verificação é *headless*: os números são reais e conferidos, mas enquadramento,
  sensibilidade do mouse e aparência são desconhecidos deste lado.

---

## Parte 5 — Como verificar

```bash
./scripts/fetch_kernels.sh          # 33 MB, SPICE DE440
./scripts/fetch_star_catalog.sh     # 560 kB, Yale BSC5
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure        # 32/32, sem engine nenhum
```

A viagem à Lua, pela linha de comando:

```bash
./build/bin/orbit-cli intercept tests/scenarios/lunar-intercept.json \
    --to Moon --tof 4.5 --flyby-altitude-km 100 --tolerance-km 0.2 --insert
```

A cena, sem tela:

```bash
./scripts/run_godot_headless.sh                   # ótica: cone, Doppler, beaming
SPACEFLIGHT_HEADLESS_MISSION=1 ./external/godot/Godot.app/Contents/MacOS/Godot \
    --headless --path godot/project --quit-after 3200     # a viagem inteira
```

Com tela: `J` planeia, `.` cinco vezes sobe o warp, e seis dias e três quartos
depois o HUD diz `about Moon  CAPTURED`.
