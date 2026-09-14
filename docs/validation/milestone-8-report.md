# Milestone 8 — relatório

> Sistema Solar e planejamento geral de missões.

---

## O QUE SE JOGA

Abre-se o jogo em órbita terrestre. `Tab`, `◀ ▶` até `MARS`, `SEARCH`. O jogo
continua a andar enquanto a busca corre — cerca de um minuto, com as contagens a
subir no painel. Voltam duas trajetórias com tempos de voo e Δv diferentes.
`Shift+M` mostra a transferência desenhada entre a Terra e Marte, sobre as
órbitas planetárias amostradas da efeméride. `Enter` arma. `.` sobe o warp.
Duzentos e quatro dias depois, três queimas, e a nave está numa órbita de **497,8
× 503,2 km** em torno de Marte.

Terra→Lua continua a funcionar, pelo mesmo código, com os mesmos números de
antes.

---

## SISTEMA SOLAR

`core/celestial/solar_system.hpp` é a tabela única de identidade e hierarquia: 21
corpos, do Sol a Caronte, cada um com o pai, o tipo e até onde o planejador foi
qualificado para ele. O core, o planejador, a GDExtension e a interface leem dela;
nenhum deles mantém uma lista própria.

Ela é separada do `BodyCatalog` porque as duas respondem a perguntas diferentes:
uma diz **de quem é a massa no modelo de forças** — e para isso Marte é o
baricentro 4 — e a outra diz **o que existe e para onde dá para ir**, onde Marte
é o planeta 499 e Fobos existe.

Desenhados: Sol, Mercúrio, Vênus, Terra, Lua, Marte, Fobos, Deimos, Júpiter,
Saturno, Urano, Netuno, Plutão. Posição do SPICE, orientação do PCK, superfície
de textura onde ela existe e de cor média onde não.

### Honestidade do catálogo

Os gigantes gasosos são desenhados na posição do **baricentro** do sistema deles,
porque os SPK dos próprios planetas têm centenas de megabytes e não são
distribuídos. Isso basta para desenhar — o baricentro de um sistema planetário
fica dentro do planeta; medido, onde os dois existem: Marte 499 e o baricentro 4
estão a **10 cm**. E não basta para ser o centro de uma órbita de captura.

Então eles não são oferecidos como destino. O catálogo classifica cada corpo em
`qualified` (a Lua, uma campanha de 365 épocas), `experimental` (Marte, Vênus,
Mercúrio, Fobos, Deimos) ou `observation` (todo o resto), e a interface mostra
isso em vez de oferecer um botão que falha.

Marte só existe porque o `mar099s.bsp` entrou no conjunto padrão de kernels. Sem
ele o corpo 499 não existe para o SPICE. Verificado que ele não perturba o DE440:
a posição baricêntrica da Terra sai **idêntica ao último dígito** com e sem ele.

---

## PLANEJADOR GERAL

O planejador já era genérico na aritmética — o próprio cabeçalho dizia *"nothing
in this file is lunar"*. O que faltava era o gerador de candidatos.

`TransferGeometry` é a única coisa que difere entre os dois tipos de missão, e é
lida da hierarquia:

```text
LOCAL            o destino orbita a origem      Terra → Lua
INTERPLANETARY   ambos orbitam um primário      Terra → Marte
```

Não existe um `if (destination == Mars)` no core. Acrescentar Titã não acrescenta
um ramo.

Tudo depois da geração de candidatos é compartilhado: o mesmo corretor
diferencial, o mesmo plano-B, a mesma queima de captura, a mesma classificação.

Vocabulário renomeado: `plan_lunar_transfer` → `plan_mission`,
`LunarTransferRequest` → `MissionRequest`, `lunar_transfer.hpp` →
`transfer_planner.hpp`, `LUNAR_IMPACT` → `TARGET_IMPACT`.

---

## TERRA → MARTE

```text
departure      2026-01-01T00:52:10
arrival        2026-07-24
flight time    204,41 dias

injection      16 864 m/s em 1685 s
capture         7 516 m/s em  735 s
trim                          15 s
total          24 380 m/s de 26 942 938 disponíveis
propellant     54,14 kg

arrival orbit  497,8 × 503,2 km, e = 0,0007, i = 20,68°
requested      500 × 500 km
```

### Três coisas que a física impôs

Cada uma foi descoberta a errar, e cada uma está no código com a medição que a
justifica.

**A mira do sobrevoo precisa antecipar a queda do periapsis.** A queima de
captura dura 735 s a 19 km/s de velocidade no periapsis: o motor fica aceso ao
longo de quatro raios marcianos. Mirada em 500 km, e dimensionada para trazer o
apoapsis a 500, a elipse resultante tinha periapsis de **−523 km** — através do
planeta. A mira virou quantidade resolvida: voa-se a captura impulsiva uma vez,
mede-se quanto o periapsis caiu, mira-se essa distância mais alto. Com *warm
start* entre passes; sem ele cada passe refaz uma correção de duzentos dias e a
busca leva dez minutos em vez de cinco segundos.

**Uma queima não circulariza.** Varrendo o deslocamento da queima relativamente
ao periapsis:

| deslocamento | órbita | e |
|---|---|---|
| −300 s | −957 × 4954 km | 0,549 |
| **0 s** | **192 × 993 km** | **0,101** |
| +366 s | −1493 × 7131 km | 0,695 |

Zero é ótimo local, então um corretor em (Δv, deslocamento) tem coluna nula ali e
para — foi o que fez, após 57 voos. O botão não existe. São duas queimas: a mira
resolve o periapsis, um trim de **15 s** no periapsis baixa o apoapsis. Quinze
segundos realmente são quase o impulso que a aritmética de dois corpos supõe; 735
não são.

Uma captura lunar dura 83 s contra 1,6 km/s e continua com uma queima só. O
primeiro voo da queima impulsiva já cai dentro da órbita pedida, e tudo abaixo
desse teste fica desligado — que é a razão de a campanha lunar não ter mexido um
dígito.

**Tolerâncias lunares não escalam.** Três constantes eram lunares sem dizer:

| | LOCAL | INTERPLANETARY | porquê |
|---|---|---|---|
| tolerância da mira | 2 km | 50 km | o passo de diferenças finitas (1e-3 m/s) move a chegada 1 km numa transferência lunar e **34 km** numa marciana |
| conic abaixo da superfície | multa | recusa | voar assim mesmo custa **226 000 passos** por voo contra 5 003, porque a trajetória atravessa a Terra |
| orçamento de passos | 2e6 | 50e6 | quatro dias contra duzentos |

---

## PREVISTO CONTRA VOADO

Depois de 204 dias e das três queimas, medido na cena:

| | previsto | voado | diferença |
|---|---|---|---|
| periapsis | 497 830,09 m | 497 838,90 m | **8,8 m** |
| apoapsis | 503 210,43 m | 503 226,69 m | **16,3 m** |
| excentricidade | 0,00069155 | 0,00069251 | 9,6e-7 |
| inclinação | 20,67974° | 20,68010° | 0,00036° |
| propelente | 54,1515 kg | 54,1519 kg | **0,37 g** |
| Δv de captura | 7516,580 m/s | 7516,684 m/s | 0,10 m/s |

Oito metros e oitenta, depois de duzentos e quatro dias. Isso é o que significa
inverter o modelo em vez de prever com ele.

A única linha discrepante é a **época de chegada**, que difere em 3,8 dias: o
monitor guarda a aproximação mais próxima sobre as amostras que o quadro lhe dá,
e a 1e7× cada quadro são 0,87 dia. É resolução de medição sob warp alto, não erro
de trajetória. Registrado no backlog.

---

## MAPA DO SISTEMA

`Shift+M` alterna `LOCAL` / `SYSTEM`. São dois **referenciais** e não duas
escalas: o mapa local ancora o arco no corpo de origem, e num cruzeiro de
duzentos dias a Terra anda 500 milhões de quilômetros.

O mapa do sistema é heliocêntrico, em **metros**, e carrega o nome do frame como
um campo do payload — `"Sun-centred J2000, metres"` — que ele imprime no canto.
As órbitas planetárias são amostradas da efeméride, recortadas contra a cobertura
dos kernels (o ano de Netuno tem 165 anos; o de440s acaba em 2150) e o campo
`clipped` diz quando foram. Zoom logarítmico, de 1e6 a 1e12 m. Anéis em UA.
Rótulos com antiaglomeração.

---

## COMPUTADOR DE BORDO

```text
TARGET        ◀ MARS ▶
TARGET ORBIT  ◀ 500 km circular ▶        [ SEARCH ]

TRAJECTORIES FOUND
             FASTER          CHEAPER
FLIGHT       204d 10h        204d 10h
DEPART ΔV    18442 m/s       16864 m/s
...

REFUSED (4)
  181.12d          PERIAPSIS_TOO_LOW
```

Os rótulos `FAST` / `BALANCED` / `LOW ΔV` só aparecem quando há três opções para
nomear; com duas são `FASTER` e `CHEAPER`. Chamar uma trajetória de "equilibrada"
quando não há nada com que a equilibrar seria inventar uma comparação.

A órbita-alvo passa a 500 km ao escolher Marte e a 100 km ao voltar para a Lua.

---

## DESEMPENHO

| | |
|---|---|
| busca completa Terra→Marte | **64 s** (768 geometrias, 24 triadas, 6 voadas, 313 propagações) |
| uma candidata isolada | **5,6 s** |
| busca Terra→Lua | 3,0 s |
| corrida completa sem tela, Terra→Marte | 4 min 15 s |

A busca corre numa **thread**. Uma, porque o CSPICE tem mutex global e N delas só
pagariam contenção. O worker nunca escreve no plano; `collect_plan()` instala no
quadro.

Ela pode ser **cancelada**, e para entre candidatas — medido: 0,05 s.

⚠️ O quadro cede a efeméride enquanto a busca corre: `_update_tracks` é a maior
consumidora da cena (256 chamadas para o caminho do alvo, mais uma por amostra do
arco) e cada uma é uma que o worker espera. Sem essa cedência, numa corrida sem
tela — onde o quadro não tem limite de taxa — a busca não terminava de ordenar as
candidatas depois de milhares de quadros.

64 s excede o orçamento da regra 119 ("primeiras candidatas úteis em poucos
segundos"). É por isso que a regra 48 existe, e é por isso que há progresso e
cancelamento.

---

## BUGS CONHECIDOS

Nenhum bloqueante.

**Só duas alternativas de seis são viáveis** nesta época. As quatro recusadas são
todas `PERIAPSIS_TOO_LOW`: o laço de mira esgotou os três passes. Partir do desvio
medido no primeiro passe, em vez de zero, é o caminho evidente. `IMPROVEMENT`.

**A época de chegada é resolvida à granularidade do warp.** Ver acima.
`IMPROVEMENT`.

**A captura interplanetária não está disponível sob `AUTOPILOT`.** Os voos curtos
que tornam o solver de captura viável não montam a pilha de atitude, e uma queima
guiada pelo casco sem ela aponta para lugar nenhum — foi assim que o caso lunar
qualificado começou a falhar, e a correção foi restringir o caminho novo aos
modelos que ele representa. O que falta é construir a pilha de atitude uma vez e
partilhá-la, em vez de a montar dentro de uma função de voo. `PHYSICS_DEBT`.

---

## BACKLOG

```text
GRAVITY_ASSISTS                      regra 84, fora do M8
LOW_THRUST_CONTINUOUS_OPTIMIZATION   regra 85, fora do M8
MULTI_LEG_MISSIONS                   regra 128
AUTOPILOT_INTERPLANETARY_CAPTURE     acima
CAMPANHA_TERRA_MARTE                 365 épocas, como a lunar; enquanto não
                                     existir, Marte é `experimental`
APPROACH_EPOCH_SOB_WARP_ALTO         seção áurea sobre o interpolante denso
AIM_LOOP_PARTINDO_DO_DESVIO_MEDIDO   mais alternativas viáveis por busca
SPK_DOS_GIGANTES                     jup347/sat441: centenas de MB por um
                                     planeta que o M8 só desenha
ANÉIS_DE_SATURNO                     regra 70
ECLIPSES                             herdado do M7
MOLA_NORMAL_MAP                      calculado, não pintado (regra 25)
CLIQUE_NO_MAPA_PARA_SELECIONAR       regra 75, explicitamente opcional
```

---

## ASSETS A GERAR

```text
1. Mars albedo
   Prompt:      docs/assets/planets/mars-albedo-codex-prompt.md
   Destino:     godot/project/assets/textures/mars/mars_albedo.png
   Enquanto não existir: substituto procedural em PlanetTextures.mars_albedo().
                Acerta a cor, o contraste e as calotas. Não é um mapa de Marte.
```

**Não pedir** um normal map de Marte a um gerador de imagens. A topografia do
MOLA existe e é medida; se o relevo for desejado ele é **calculado**, como o da
Lua (regra 25). O manifesto marca esse asset como `REFUSED` e diz porquê.

Mercúrio, Vênus, Júpiter, Saturno, Urano, Netuno e Plutão usam cor média do
disco. Reconhecíveis a distância e nada mais, que é o que a regra 69 pede.

---

## CRITÉRIOS DE SAÍDA

```text
[PASS] Solar System bodies are represented from SPICE
[PASS] Mars can be selected as destination
[PASS] mission planner API is no longer Moon-specific
[PASS] Earth→Moon still works                     96,901 × 103,184 km, idêntico
[PASS] Earth→Mars trajectory search works         497,8 × 503,2 km
[PASS] multiple trajectory alternatives displayed
[PASS] system map displays Earth, Mars, ship and trajectory
[PASS] departure burn executes physically         16 864 m/s em 1685 s
[PASS] interplanetary coast executes physically   204,41 dias
[PASS] Mars approach is rendered
[PASS] capture burn executes physically           7516 m/s + trim de 15 s
[PASS] spacecraft reaches Mars orbit
[PASS] complete vertical slice can be reproduced  scripts/run_godot_headless.sh
[    ] screenshots exist for the complete sequence  scripts/m8_screenshots.sh,
                                                    precisa de tela
[    ] manual playtest checklist passes             docs/validation/m8-playtest.md,
                                                    precisa de uma pessoa
```

As duas últimas linhas precisam de uma tela e de alguém a olhar para ela. O
roteiro fotográfico e a lista existem e estão escritos; correr o primeiro e
seguir a segunda é o passo que não se automatiza (regra 60).
