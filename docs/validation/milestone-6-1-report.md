# Relatório do Milestone 6.1

**Data:** 13 de setembro de 2026
**Escopo:** confiabilidade da navegação Terra–Lua e validação óptica relativística
**Resultado global:** os dois bloqueadores P0 do Milestone 6 estão fechados

```text
NAVIGATION:                 PASS
STARFIELD:                  PASS
RELATIVISTIC VISUALIZATION: PASS
```

---

## Resumo executivo

O Milestone 6 ficou aberto por dois bloqueadores. Ambos acabaram sendo defeitos
que o próprio instrumento de medida escondia.

**Navegação.** A campanha reportava 18 sucessos em 100 épocas e todas as falhas
diziam que o solver havia convergido. Ele havia: havia convergido sobre
trajetórias que atravessam a Terra. Em 52 das 100 épocas a propagação final
parava com `trajectory entered Earth` seis minutos depois da ignição, **depois**
de um estágio de plano B reportar `converged`. A causa não era o corretor: era
que o pipeline não tinha busca nenhuma — o ponto de partida era onde a órbita
estivesse e o tempo de voo era uma constante — e 82,5 % das geometrias assim
obtidas são invoáveis a partir de uma órbita de 400 km.

Hoje: **100/100** nas mesmas 100 épocas e **365/365** ao longo de um ano, com um
critério de sucesso muito mais estrito, e toda falha classificada.

**Starfield.** 8 786 estrelas carregadas, nenhuma visível — e "invisível" é o
sintoma que todo defeito possível compartilha. Um arnês de dez estágios separou
as hipóteses e encontrou **dois** defeitos independentes, o primeiro escondendo o
segundo: `render_mode unshaded` faz o Forward+ do Godot 4 descartar `EMISSION`, e
a esfera do céu ficava abaixo de um passo de quantização do buffer de
profundidade de 24 bits.

Hoje o céu aparece, e a aberração, o Doppler e o beaming são verificados
numericamente contra o `core/` — não contra uma captura de tela.

| Área | Milestone 6 | Milestone 6.1 | Evidência |
|---|---|---|---|
| Missão Terra–Lua | FAIL (18 %) | **PASS** (100 %, 365/365) | [hardening](lunar-navigation-hardening.md) |
| Starfield | FAIL (invisível) | **PASS** (10 estágios) | [starfield-debug](starfield-debug.md) |
| Render relativístico | BLOCKED | **PASS** | [visual](relativistic-rendering-visual.md) |
| Núcleo relativístico | PASS | PASS (inalterado) | [milestone 6](relatorio-milestone-6.md) |
| Propagação e invariantes | PASS | PASS (inalterado) | [convergência](numerical-convergence.md) |
| Efemérides e referenciais | PASS | PASS (inalterado) | [Horizons](ephemeris-horizons.md) |

---

## NAVIGATION: PASS

Detalhes completos em
[lunar-navigation-hardening.md](lunar-navigation-hardening.md).

### O diagnóstico

A primeira ação foi explicitamente **não** mexer em tolerância, iteração ou
timeout (seção 2 do brief), e sim reexecutar a campanha antiga guardando a saída
inteira. A correlação que saiu dali decide tudo:

| | entrou na Terra | não entrou |
|---|---:|---:|
| capturada | **0** | 41 |
| não capturada | 52 | 7 |

Três mecanismos, todos físicos:

1. **46 casos** — a cônica pós-injeção passa abaixo da superfície da Terra. Com
   ponto de partida e tempo de voo fixos, o ângulo de transferência é o que o
   calendário der; acima de ~215° a solução de Lambert exige uma velocidade de
   partida 4,6 a 13 km/s fora da órbita, numa direção que reentra.
2. **6 casos** — o corretor de partida para a 3,4·10⁵ km, com Δv de Lambert entre
   8 e 11 km/s: a mesma geometria impossível, vista do outro lado.
3. **6 timeouts** — o estágio 1 mirava o **centro** da Lua, então as sondas do
   estágio 2 passavam a 600 m dele, onde a aceleração de ponto material é
   10¹¹ m/s² e o passo do integrador colapsa. Um caso levou 160 s.

### As correções

| # | mudança | efeito medido |
|---|---|---|
| 1 | busca em `partida × tempo de voo × ramo` (320 geometrias por época) | 15 % → 100 % |
| 2 | perigeu da cônica de partida como **preço**, não recusa; "sem colisão" verificado no arco voado | 15 % → 50 % na configuração fixa; sem efeito com a busca completa |
| 3 | estágio 1 mira o **sobrevoo**, não o centro | elimina o mecanismo de timeout na origem |
| 4 | tolerância do estágio 1 dimensionada ao seu trabalho | 30 s → 0,5–1 s por época |
| 5 | função de custo explícita com a distância de voo convertida em Δv de correção | Δv de partida mediano 10 952 → 3 159 m/s no modelo impulsivo |
| 6 | sucesso = a órbita pedida, não `ε < 0` | 41 "capturas" → 100 sucessos sob um critério mais estrito |

### Ablação

As mesmas 100 épocas, uma configuração por linha:

| configuração | sucesso |
|---|---:|
| reprodução do Milestone 6 (partida fixa, tof 4,5 d, cônica recusada) | 15 % |
| a mesma, com a cônica de partida precificada em vez de recusada | 50 % |
| + grade de tempo de voo | 52 % |
| + janela de partida | **100 %** |
| busca completa | **100 %** |

A reprodução do Milestone 6 dá 15 % contra os 18 relatados: o pipeline novo
reproduz o antigo quando se lhe tira a busca, que é a melhor evidência de que o
diagnóstico está certo.

A segunda linha é a que não foi prevista. A peneira analítica no perigeu da
cônica de partida — que parecia a correção central — **recusava transferências
voáveis**: ela lê a cônica não corrigida, e o corretor depois move a velocidade de
partida em 152 a 3 910 m/s, o que rotineiramente levanta um perigeu que começou
abaixo da superfície. Hoje o déficit é um **preço** na função de custo, e a
restrição rígida "sem colisão" vive onde uma colisão é um fato: no arco voado.
Com a busca completa isso não muda nada — as duas leituras escolhem a mesma
trajetória em 100 de 100 épocas — e sem ela vale 35 pontos percentuais.

### Planejamento contra execução

Os três modelos voam a **mesma** geometria, fixada pela busca:

| modelo | `e` | órbita | queima | atraso de apontamento |
|---|---:|---|---:|---:|
| IMPULSIVE | 0,000003 | 99,87 × 99,88 km | — | — |
| FINITE_BURN | 0,001732 | 96,41 × 102,78 km | 83,5 s | — |
| AUTOPILOT | 0,010468 | 79,89 × 118,34 km | 83,5 s | 1,65° médio, 2,06° de pico |

**Zero falhas de planejador, uma de execução por época.** O planejador entrega
uma órbita circular de 100 km com `e = 3·10⁻⁶`; o que falta na órbita final é o custo de
executar. E o custo tem fórmula: o atraso do autopilot vai exatamente como
`1/ω_n` (produto `atraso × ω_n` constante a 0,5 % ao longo de 16× em largura de
banda), e a partir de `ω_n = 0,1 rad/s` a captura entra na especificação.

### Sensibilidade de Oberth

A janela da queima de captura é estreita e simétrica: ±30 s leva a
excentricidade de 0,0017 a 0,021; ±120 s põe a órbita dentro da Lua.

### Critério de saída

A seção 15 pede ≥ 95 %, idealmente 100 % das geometrias factíveis,
deterministicamente.

```text
100 épocas originais           100 %
365 épocas, uma por dia        100 %
100 épocas, modelo impulsivo   100 %
NO_FEASIBLE_TRAJECTORY             0
determinismo                   sem aleatoriedade em nenhum ponto
```

---

## STARFIELD: PASS

Detalhes completos em [starfield-debug.md](starfield-debug.md).

### Defeito 1 — `unshaded` descarta `EMISSION`

Sob `render_mode unshaded` o Forward+ do Godot 4 resolve o fragmento como
`vec4(albedo, alpha)` e nunca lê `EMISSION`. O shader escrevia
`ALBEDO = vec3(0.0)` e `EMISSION = star_colour`: as estrelas eram carregadas,
transformadas e rasterizadas — em preto, sobre preto.

Medido com dez primitivas de ponto, uma variante por quadro:

```text
unshaded  + EMISSION        0 pixels acesos, byte máximo   0
unshaded  + EMISSION x100   0 pixels acesos, byte máximo   0   <- elimina "escuro demais"
unshaded  + ALBEDO        360 pixels acesos, byte máximo 255
iluminado + EMISSION      360 pixels acesos, byte máximo 255
```

### Defeito 2 — a esfera do céu abaixo de um passo de profundidade

Com as estrelas finalmente brancas, um terço delas continuava ausente. O buffer
de profundidade tem 24 bits; com Z reverso a profundidade é `near/distância`, e
`0,01 / 1,9e5 = 5,26e-8` não chega ao passo de quantização `2⁻²⁴ = 5,96e-8`.
Quais estrelas sobreviviam era decidido por arredondamento em `float`.

| `near` | passos de profundidade | no quadro | renderizadas |
|---:|---:|---:|---:|
| 0,01 | 0,88 | 83 | **59** |
| 0,012 | 1,06 | 83 | 83 |
| 0,05 | 4,42 | 83 | 83 |
| 0,5 | 44,15 | 83 | 83 |

`CAMERA_NEAR` passou a 0,05 — quatro vezes o penhasco medido, ainda 50 km — e o
limite de zoom passou a ser escrito na **distância**, onde o plano próximo está.

O estágio que mede isso fica na suíte com `near = 0,01` como controle marcado
como falha esperada: se ele parar de perder estrelas, a medição parou de medir.

### O arnês

Dez estágios, cada um com um número por resposta, e o oráculo sempre no `core/`:
eixos, escala, clip space, profundidade, escada de contagem, magnitude, toggles,
aberração, Doppler, beaming, snapshots. Exige **tela**: o modo `--headless` do
Godot tem um rasterizador falso que não desenha nada, e uma suíte que passasse
nele não provaria nada — que é como o starfield atravessou um milestone inteiro
quebrado.

---

## RELATIVISTIC VISUALIZATION: PASS

Detalhes completos em
[relativistic-rendering-visual.md](relativistic-rendering-visual.md).

Tudo comparado contra o `core/` — `get_apparent_direction()`,
`get_expected_response()`, `get_expected_colour()` — e nunca contra uma
impressão humana.

| verificação | pior erro | orçamento |
|---|---:|---:|
| aberração, ângulo GPU × CPU, `β` = 0; 0,1; 0,5; 0,9; 0,99 | 0,199° | 0,359° |
| cromaticidade Doppler, 3 temperaturas × 2 ângulos | 0,0012 | 0,04 |
| beaming, razão de intensidades | 0,0146 | 0,12 |
| beaming, resposta absoluta | 0,0086 | 0,03 |
| magnitude, monotonicidade e valor | 0,0141 | 0,03 |
| snapshots de regressão a 0c, 0,5c, 0,9c, 0,99c | 0,00000 | 0,02 |

Duas correções de **medição** foram necessárias antes de qualquer desses números
significar alguma coisa, e ambas estão registradas: o framebuffer é codificado em
sRGB enquanto o shader escreve linear (comparar o byte guardado com o que o
`core/` calculou é comparar uma grandeza com a sua própria função de
transferência), e o centroide de um blob é um índice enquanto uma projeção é
contínua.

A imagem `scene_forward_beta_0p9.png` é a que o Milestone 6 não conseguiu
produzir: em `β = 0,9`, olhando para dentro do cone, o céu empilhado num disco
azul centrado na direção do movimento.

---

## Dívidas menores (Parte C do brief)

### Seção 27 — strong types: **feito**

Os três quantitativos que a auditoria de unidades do Milestone 6 deixou em
aberto passaram a carregar o tipo:

| antes | depois |
|---|---|
| `Quaternion::from_axis_angle(axis, double)` | `units::Angle` |
| `Quaternion::to_axis_angle(Vec3&, double&)` | retorna `AxisAngle { Vec3, units::Angle }` |
| `Quaternion::angle_between(...) -> double` | `units::Angle` |
| `EulerZYX { double yaw, pitch, roll }` | três `units::Angle` |
| `OrbitalElements::{inclination, raan, argument_of_periapsis, true_anomaly}` | quatro `units::Angle` |
| `apparent_position(..., double tolerance_seconds, ...)` | `time::Duration` |

62 pontos de chamada convertidos; 33 testes passam. `mean_motion` fica como
`double` [rad/s] com a razão anotada: é uma **taxa**, não um ângulo, e um tipo
`AngularRate` seria a contrapartida honesta — nada ainda tem dois deles para
confundir.

### Seção 28 — modelo lunar: **não implementado, como pedido**

A qualificação progressiva (ponto material → J2 → J2+C22 → grau mais alto)
continua sendo o caminho, e GRAIL grau 420 continua fora do caminho principal. A
campanha desta entrega roda com Sol + Terra + Lua como pontos materiais mais o J2
da Terra, e o erro medido nessa configuração não é ainda um argumento para subir
o grau: o periastro do sobrevoo cai dentro de 1,9 km do pedido em 365 épocas.

### Seção 29 — frame dragging: **não implementado, como pedido**

Continua fora do caminho crítico e o estudo continua correto em recusar um termo
isolado de Lense–Thirring que quebraria a contagem de ordem da expansão.

---

## O que este milestone não fecha

Registrado porque a regra do brief é explicar, não maximizar verde.

- **A inclinação da órbita lunar final não é controlada**: sai entre 0,1° e 30,2°
  ao longo do ano. A especificação da seção 13 não a menciona, e a busca escolhe
  ângulos de transferência perto de 180°, onde o plano é mal condicionado.
  Controlar isso é o problema da
  [otimização do plano B](../physics/b-plane-optimization.md).
- **A restrição de combustível nunca morde**: a nave do cenário tem 26 942 km/s de
  orçamento. As duas razões de falha por Δv existem e são verificadas, e nenhuma
  época chegou perto de acioná-las — a campanha não testa margem de combustível.
- **O autopilot não atinge a especificação** com os ganhos do cenário. O mecanismo
  está medido e o remédio demonstrado (`ω_n ≥ 0,1`); ele não foi aplicado à cena.
- **Uma só órbita de estacionamento** em todas as 365 épocas.
- **Uma só GPU**: Apple M5 Pro, Metal 3.2, Godot 4.5.stable. Os números de passo
  de profundidade dependem de o buffer ter 24 bits; o estágio 3b mede onde o
  penhasco está em vez de assumir.
- **Tempo retardado e objetos extensos** continuam com inspeção diferencial
  pendente, como no Milestone 6. Nenhum dos dois bloqueia a validação óptica.

---

## Reprodução

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure                 # 33 testes

./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --map
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --epochs 100
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --compare --epochs 3
./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --oberth-sweep
scripts/lunar_campaign.sh 365 8

cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build-godot --target spaceflight_gdextension -j
./scripts/starfield_validation.sh                          # precisa de tela
SPACEFLIGHT_CAPTURE="$PWD/docs/validation/scene" \
  external/godot/Godot.app/Contents/MacOS/Godot --path godot/project
```

---

## Evidências

| documento | assunto |
|---|---|
| [lunar-navigation-hardening.md](lunar-navigation-hardening.md) | diagnóstico, correções, ablação, campanhas |
| [lunar-navigation-campaign-v2.csv](lunar-navigation-campaign-v2.csv) | 365 épocas, uma linha cada, ~90 colunas |
| [starfield-debug.md](starfield-debug.md) | os dois defeitos e o arnês de dez estágios |
| [relativistic-rendering-visual.md](relativistic-rendering-visual.md) | aberração, Doppler, beaming, snapshots |
| [relatorio-milestone-6.md](relatorio-milestone-6.md) | o milestone anterior, inalterado |
| `docs/validation/starfield/` | log e capturas do arnês |
| `docs/validation/scene/` | a cena de produção, 12 capturas |
| `docs/validation/starfield-reference/` | snapshots de regressão |
