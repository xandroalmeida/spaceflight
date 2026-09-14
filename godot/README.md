# Godot (Milestones 2, 5 e 7)

O Godot entra aqui **apenas como consumidor** do `spaceflight_core` (ADR-0002).
Nada nesta pasta calcula física — nem o GDScript, nem os shaders.

```
godot/
├── gdextension/     ponte C++: expõe SimulationSnapshot e o céu ao engine
│                    SpaceflightSimulation  estado, snapshots, posições aparentes
│                    SpaceflightSky         catálogo estelar + tabela de Planck
└── project/         projeto Godot 4.5
    ├── scripts/
    │   ├── flight.gd            o orquestrador: constrói tudo e encaminha a entrada
    │   ├── input_actions.gd     TODO o mapeamento de teclas, numa tabela
    │   ├── flight_controls.gd   o que o piloto comanda
    │   ├── format.gd  palette.gd
    │   ├── world/               nave, pluma, jatos, planetas, estrelas, materiais
    │   ├── camera/              as DUAS câmeras e os cinco modos
    │   ├── cockpit/             interior 3D, controles clicáveis, instrumentos
    │   ├── ui/                  HUD, mapa, missão, menu, ajuda, HUD técnico
    │   ├── audio/               som estrutural e de interface
    │   └── headless/            a verificação sem tela
    ├── tests/                   a suíte de apresentação (regra 62 do M7)
    ├── shaders/                 star_field, relativistic_body
    └── assets/                  áudio-substituto; texturas quando existirem
```

O Milestone 7 partiu o `main.gd` de 1520 linhas nesta árvore. A leitura técnica
completa não foi reduzida: ela está em `ui/debug_hud.gd`, é o que a verificação
sem tela imprime, e passou a viver atrás de `F3` em vez de ser a interface.

Onde cada conta acontece está em `docs/architecture/relativistic-shaders.md`.
Em uma linha: o `D` chega pronto de `core/relativity/optics.hpp` e o shader
aplica o que `D` **significa** (`T' = D·T`, `I' = D⁴I`); ele nunca sabe o que é
`β`. A única fórmula que existe duas vezes é o tempo retardado por vértice, que é
por-vértice por definição, e o teste fixa o **resultado** (`arcsin β`) e não o
texto.

## Construir e rodar

```bash
./scripts/fetch_godot_cpp.sh                 # ~750 kB; as bindings são geradas na build
./scripts/fetch_godot.sh                     # o editor 4.5-stable, em external/ (152 MB)
./scripts/fetch_star_catalog.sh              # Yale BSC5, 9110 estrelas (560 kB)
cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build-godot --target spaceflight_gdextension -j
./scripts/run_godot_headless.sh              # verifica tudo sem precisar de tela
```

A biblioteca sai em `godot/project/bin/spaceflight.dylib` (ou `.so`/`.dll`), que é
onde `spaceflight.gdextension` a procura. Com tela, abra `godot/project/` no editor.

### Versão: um descompasso que existe hoje

O engine está em **4.7.2**, mas o **godot-cpp não publica tag acima de
`godot-4.5-stable`** — verificado, `godot-4.6-stable` e `godot-4.7-stable` dão 404.
Por isso os dois scripts pinam 4.5 dos dois lados: é o par garantidamente
consistente, e é o que está verificado aqui.

GDExtension costuma ser compatível para frente dentro do 4.x (a extensão declara
`compatibility_minimum` e o engine respeita), então `brew install --cask godot`
provavelmente funciona — mas "provavelmente" não é o que se quer enquanto se
verifica se a ponte funciona. Para trocar qualquer um dos lados:

```bash
GODOT_CPP_VERSION=godot-4.4.1-stable ./scripts/fetch_godot_cpp.sh
GODOT_VERSION=4.4.1-stable ./scripts/fetch_godot.sh
```

### Duas armadilhas do Godot que custaram tempo

**1. `.gdextension` só é registrado depois de um *scan* de editor.** Sem
`.godot/extension_list.cfg`, a classe simplesmente não existe e o script falha no
*parse* — sem uma palavra sobre extensão em lugar nenhum da saída. Rodar o jogo
duas vezes não resolve; só um scan resolve.

**2. `--headless --import`, que faz o scan, *crasha* ao sair no Godot 4.5**
(signal 11 dentro da inicialização do shader de névoa, que headless não deveria
estar inicializando). O arquivo é escrito antes do crash, então o script ignora o
status de saída de propósito.

`scripts/run_godot_headless.sh` cuida das duas.

## Verificação headless

```
$ ./scripts/run_godot_headless.sh          # 1200 quadros, ~9 s
```

Primeiro as afirmações do Milestone 5, produzidas pelo **código que a cena usa** e
não copiadas do documento:

```
PROJECTION at beta = 0.0896 (the ship is NOT at this speed; the state is untouched)
  forward cone 84.859 deg holds 4309 stars (49.04 %)
  doppler      0.9141 astern .. 1.0940 ahead
  5800 K star  1.4762 x ahead, 0.6549 x astern  (visible band, not bolometric)

PROJECTION at beta = 0.9048 ...
  forward cone 25.204 deg holds 4309 stars (49.04 %)
  doppler      0.2236 astern .. 4.4731 ahead
  5800 K star  51.483 x ahead, 3.2947e-07 x astern  (visible band, not bolometric)

PROJECTION at beta = 0.9900 ...
  forward cone  8.110 deg holds 4309 stars (49.04 %)
  doppler      0.0709 astern .. 14.1067 ahead
  5800 K star  238.78 x ahead, 2.1991e-23 x astern  (visible band, not bolometric)
```

Três coisas para conferir aí, e todas são conferíveis:

* os cones são `arccos β` — 84,859°, 25,204°, 8,110° — e os fatores Doppler são
  `γ(1 ± β)`, recíprocos exatos;
* **4 309 estrelas nos três**. Não é coincidência nem arredondamento: a aberração
  leva o hemisfério `θ < 90°` sobre o cone `arccos β` **estrela por estrela**, então
  o conjunto é invariante e a contagem também. Um erro de sinal ou uma confusão
  entre `n̂` e `ŝ` quebraria isso imediatamente
  (`docs/physics/relativistic-rendering.md` §12.3);
* o brilho é de **banda visível**, não bolométrico: 51× à frente onde `D⁴` daria
  400×, e 3,3·10⁻⁷ à ré onde `D⁴` daria 2,5·10⁻³. A diferença é a luz que foi para
  o ultravioleta e para o infravermelho (§10).

"PROJECTION" está rotulado em toda linha porque é exatamente isso: o que a ótica
**faria** a essa velocidade, pela mesma rotina, sem que nada no estado mude. A
nave está a `β = 10⁻⁴`.

Depois, o HUD, uma vez a cada 150 quadros:

```
elapsed        10.277051 s   warp 10x
altitude       399.999 km
speed          7672.593 m/s
...
beta           1.005e-4
render res.    0.8072 m per float ulp at Earth

stars          8786  (apparent)
forward cone   89.994 deg holds 4407 stars (50.16 %)
doppler        0.999900 astern .. 1.000100 ahead
5800 K star    1.000454 x ahead, 0.9995458 x astern  (visible band)
exposure       0.1585 half-saturation flux
light time     Moon 1.1952 s
```

Cada número é conferível: `v = √(GM/r)` a 6771 km dá 7672,6 m/s; o período
kepleriano dá 5544,87 s; a resolução de renderização é `6771 km · ε_float` =
0,807 m (`docs/architecture/rendering.md` §2); o tempo de luz até a Lua, 1,19 s,
é a distância dela dividida por `c`. A `β = 10⁻⁴` o Doppler é `1 ± 10⁻⁴` e o
brilho muda 0,05 % — o efeito **existe** e é desprezível, que é o que tem de ser
nessa velocidade.

### Ir à Lua, na cena

`Shift+J` planeja, `Enter` arma, e o warp faz o resto.

⚠️ No Milestone 7 planejar deixou de armar. A regra 26 põe um `EXECUTE` e um
`CANCEL` à frente do piloto, e um plano que já está a voar quando esses botões
aparecem faz do `CANCEL` uma mentira sobre o que acabou de acontecer. Nenhum
número da trajetória mudou com isso — a mesma busca, o mesmo corretor; o que
mudou é quando a lista de manobras chega ao executor.

```
[mission] 2 burns: injection 4548.5 m/s in 94.5 min, insertion 856.4 m/s, flyby 99.8 km
[mission] transfer angle 156.6 deg, tof 6.75 d, lambert 4745.6 m/s
[mission] stage 1 stalled ... (12 iter)   stage 2 converged
```

e seis dias e três quartos depois, no HUD:

```
about Moon      CAPTURED   1838.2 km at 1632.7 m/s
  orbit        96.4 x 103.2 km altitude, e 0.0019, i 19.86 deg, 117.8 min
```

A física é toda do `core/`: Lambert, o corretor diferencial, o plano B
(`docs/physics/b-plane.md`), a inserção. O `.gd` carrega números e mostra-os.

⚠️ **Planejar bloqueia o quadro por cerca de um segundo**, e isso está assim de
propósito: planear é procurar oportunidades de partida e depois inverter o modelo
completo duas vezes, dezenas de propagações. É uma operação de missão, não de
quadro. Threadá-lo compraria um segundo mais suave e custaria poder dizer qual era
o estado da simulação quando o plano foi feito.

Três coisas que esta parte obrigou a medir, e que estão comentadas onde acontecem:

* a correção tem de mirar a **queima finita**, não um impulso. Uma injeção
  translunar perde ~600 m/s para a gravidade enquanto o motor está ligado, e
  corrigir um impulso para depois voar uma queima erra por centenas de milhares de
  quilómetros — medido, fazendo exatamente isso;
* escolher a partida pelo **Δv mais barato é o critério errado**. O mais barato é
  sempre o mais longo (aproxima-se da elipse de energia mínima) e também o mais
  sensível: a busca foi buscar 6,75 dias, poupou 150 m/s que uma nave com
  26 942 km/s não precisa, e deixou o corretor empacado. Agora os candidatos mais
  baratos são **voados** uma vez cada, e escolhe-se o que de facto chega mais
  perto;
* **uma tentativa é cara ou coroa.** Se o corretor fecha depende da geometria de
  partida, e execuções que diferiam só no quadro em que a tecla foi premida davam
  resultados diferentes. Tenta-se até três candidatos e fica-se com o primeiro que
  fecha. Um plano que não atinge o alvo é **recusado com o motivo**, em vez de
  devolvido.

### O HUD cabe por construção, e não por palpite

A fonte era `altura_da_viewport / 38`, limitada a `[15, 28]` px. Um palpite não
sabe quantas linhas existem: a leitura cresceu de 33 para 52 linhas quando a
ótica relativística e a câmera chegaram, e a 24 px isso é 1 500 px de texto numa
janela de 900 — passava da borda de baixo.

Agora não há palpite. `_fit_hud` **pergunta à fonte** a altura de uma linha
(`Font.get_height`) e a largura da linha mais comprida (`Font.get_string_size`),
e desce o tamanho até que as duas caibam. No máximo treze iterações, e só quando
o formato do texto muda.

E como a fonte é monoespaçada — o que já era de propósito, porque a leitura é uma
tabela de colunas alinhadas — o modo completo é empacotado em **duas colunas**,
cortando numa linha em branco para não partir uma seção ao meio. Numa janela de
1440 × 900:

| | linhas | fonte |
|---|---|---|
| uma coluna | 52 | 10 px |
| **duas colunas** | **28** | **18 px** |
| compacto | 13 | 22 px |

No Milestone 7 esta leitura passou para trás de `F3` e deixou de ser a
interface: o que se vê ao voar são os instrumentos do cockpit, e o HUD técnico é
uma sobreposição que se liga quando se quer conferir um número contra uma
tolerância. As duas colunas e o ajuste de fonte pelo conteúdo continuam
exatamente como estão descritos acima.

⚠️ A saída headless imprime **sempre** o conjunto completo, em uma coluna, seja
qual for o modo na tela. A verificação não pode depender de para que lado um
botão da interface está apontando.

Os números também perderam ruído: `String.num_scientific` imprimia tudo o que o
`double` tem (`clock diff 4.014566457044566e-13`), e dezesseis dígitos de uma
grandeza cujo ponto é ser da ordem de `10⁻¹³` não são precisão, são uma linha de
tela gasta à toa. Agora são quatro algarismos significativos — mais fino que
qualquer tolerância de `docs/validation/tolerances.md` — e notação decimal dentro
da faixa que uma pessoa lê sem decodificar (`0.8072 m`, não `8.072e-1 m`).

### A cadência do print é em quadros, não em segundos

`--quit-after N` conta **quadros**. A versão do Milestone 2 imprimia a cada
segundo de relógio de parede, o que fazia a verificação depender da velocidade da
máquina: a 143 fps, `run_godot_headless.sh 200` imprimia uma vez — ou, numa
máquina mais rápida, nenhuma, e a saída ficava vazia sem nenhum erro. Agora é a
cada 150 quadros, e o mesmo comando dá a mesma saída em qualquer lugar.

### Voar de verdade até `β` relativístico

A ótica só fica visível acima de `β ≈ 0,1`, e o único jeito honesto de chegar lá é
queimar por oito anos. Com tela, `Alt+C` faz isso: CRUZEIRO, prógrado,
acelerador cheio, warp 10⁸. Sem tela, uma corrida longa faz o mesmo sozinha:

```
$ ./scripts/run_godot_headless.sh 6000
[headless] commanded PROGRADE
[headless] throttle 100% at 2.999 deg of pointing error
[headless] CRUISE -- exhaust 0.5 c, budget 0.9048 c
```

O orçamento padrão de 1200 quadros é gasto na ótica, que é o que este milestone
acrescentou; a demonstração da queima precisa de mais.

## Controles

A tabela completa está em [`docs/gameplay/controls.md`](../docs/gameplay/controls.md),
**gerada** a partir de `scripts/input_actions.gd` por `scripts/dump_controls.sh` --
uma lista de teclas copiada à mão num documento é uma lista que envelhece em
silêncio. `F1` mostra a mesma coisa dentro do jogo, a partir da mesma fonte.

O essencial:

| Tecla | Ação |
|---|---|
| `W` `A` `S` `D` `Q` `E` | arfagem, guinada, rolagem — **a nave**, e gasta propelente |
| setas | **a câmera**, e não gasta nada |
| `I` `J` `K` `L` `U` `O` | translação por RCS |
| `Shift` / `Ctrl` | abre e fecha o acelerador; `Z` cheio, `X` corte |
| `P` `N` `R` `T` (+`Shift`) | prógrado/retrógrado, normal, radial, alvo |
| `C` | câmera: COCKPIT → EXTERNAL → CHASE → VELOCITY → TARGET |
| `Tab` / `M` | computador de missão / mapa orbital |
| `Shift+J` / `Enter` / `Shift+K` | planejar / executar / abortar |
| `,` `.` | time warp; `Space` pausa; `Esc` menu |
| `` ` `` / `F3` / `F1` | HUD cockpit-mínimo-nenhum / HUD técnico / ajuda |
| `Alt+A` `Alt+D` `Alt+B` `Alt+L` | aberração, Doppler, beaming, tempo de luz |

`L` desliga a **ótica**, não a física. O estado é bit a bit o mesmo dos dois
lados; o que muda é qual pergunta o renderizador faz. É a forma mais rápida de
ver quanto a Lua anda em 1,2 segundos-luz.

### Orbitar, e girar no lugar

São duas coisas diferentes e têm controles diferentes porque respondem a
perguntas diferentes:

* **orbitar** move a câmera *em volta* do alvo, que continua centralizado — é
  como se olha um objeto por todos os lados, e como se troca o céu que está
  atrás dele. É o que `WASD` e o mouse fazem, porque é o que se quer quase
  sempre;
* **girar no lugar** deixa o alvo para trás e aponta para o vazio. Fica sob
  `Shift`, porque é a exceção.

A órbita é parametrizada num referencial, e o modo de câmera escolhe **qual** — porque há
duas perguntas diferentes e elas querem eixos diferentes.

**`ship`: os eixos do próprio casco.** Para olhar *a nave*. Azimute 180° é
exatamente atrás da cauda, 0° é de frente no nariz, e orbitar gira o objeto na
mão. Medido:

| azimute, elevação | ângulo em relação ao nariz | vista |
|---|---|---|
| 0°, 0° | 0,0° | de frente, no nariz |
| **180°, 0°** | **180,0°** | **atrás, na cauda** |
| 90°, 0° | 90,0° | de través |
| 0°, 89° | 89,0° | de cima |
| 180°, 20° | 160,0° | atrás e acima — o padrão |

⚠️ A primeira versão usava o referencial da **velocidade** para os três modos, e
com isso *não dava para chegar atrás da nave*. Com o casco a ~70° do prógrado,
varrer o azimute traçava um círculo que chegava a 138° do nariz e parava: um
três-quartos traseiro, nunca a cauda. Isso foi **medido**, varrendo o azimute e
registrando o ângulo câmera–nariz, não deduzido — e a varredura também mostrou
que a posição era perfeitamente contínua, o que descartou a hipótese óbvia de
que fosse um salto de gimbal.

**`prograde` / `retrograde`: o eixo da velocidade**, com o radial para fora como
"cima". Para olhar *o céu*:

| azimute | onde a câmera fica | para onde se olha |
|---|---|---|
| 180° | atrás da nave | dentro do cone de aberração |
| 90° | de través | perpendicular, `D = γ` (Doppler transverso) |
| 0° | à frente da nave | o céu de ré, o que apagou |

Conferido, com a nave a `β = 1,0075·10⁻⁴`:

```
az 180, el  0   ->    0,0 deg off the aberration axis   D = 1.000101
az   0, el  0   ->  180,0 deg off the aberration axis   D = 0.999899
az  90, el  0   ->   90,0 deg off the aberration axis   D = 1.000000
az 180, el 89   ->   89,0 deg off the aberration axis   D = 1.000002
```

Os dois primeiros multiplicados dão 1,000000. O terceiro é `γ`, que a `β = 10⁻⁴`
vale `1 + 5·10⁻⁹` e arredonda para 1 no mostrador — o Doppler transverso existe e
é pequeno, como tem de ser. O quarto é o polo da órbita, onde o `look_at` do Godot
falharia se o vetor "para cima" não fosse trocado; é por isso que ele está na
lista.

### Continuidade: dois saltos que existiam e onde estavam

A câmera dava saltos, e os dois culpados eram `if`s que trocavam um eixo de
referência de repente. Achados medindo a rotação dos próprios eixos da câmera
entre quadros consecutivos, varrendo elevação e azimute:

```
JUMP  el=  87.90  d_up=  88.53  d_forward=   0.24
JUMP  el= -87.90  d_up=  73.29  d_forward=   0.36
```

O `up` girava até **88 graus num quadro** enquanto a direção de visão mudava
0,2 — um salto puro de *roll*, em `el = ±87,4°`, que é exatamente onde
`acos(0,999)` cai: o limiar do guard que trocava o vetor "para cima" quando ele
chegava perto de paralelo à visão. **O guard era o bug.**

A correção não é ajustar o limiar, é não precisar dele. O vetor "para cima" passa
a ser a **tangente do meridiano**, `∂(posição)/∂(elevação)`, que é unitária e
perpendicular à direção de visão **exatamente**, para todo par (azimute,
elevação): `offset·meridiano = −cos·sen + sen·cos = 0`, algebricamente. Sem ramo,
sem limiar, sem polo.

O segundo era o referencial da velocidade, cujo "para cima" era o radial para
fora — e em órbita baixa `|β̂ · radial| = 0,99`, medido. O produto vetorial que
define o azimute tinha comprimento 0,13 e balançava; ao passar por 1,0 o
referencial inverte. Agora o "para cima" é o **polo J2000**, com
`|β̂ · ẑ| = 0,128`, e isso tem justificativa própria: estes modos existem para
olhar o **céu**, e o céu não gira — uma referência inercial mantém o campo de
estrelas parado enquanto a nave dá a volta.

Verificado depois: **0 saltos** nos três modos, varrendo elevação pelos dois polos
e azimute inteiro.

### Olhar em volta é uma medição

O `C` alterna entre as vistas em que a ótica mais difere, e o HUD diz o que
a câmera está enquadrando:

```
look           prograde   0.0 deg off the aberration axis  INSIDE the forward cone
looking into   D = 1.000101
```

```
look           retrograde   180.0 deg off the aberration axis
looking into   D = 0.999899
```

Os dois `D` multiplicados dão 1,000000 — a reciprocidade exata de
`docs/physics/relativistic-rendering.md` §4, lida do HUD. O ângulo é geometria da
câmera e é medido no `.gd`; o `D` **não** é: vem de `core/relativity/optics.hpp`
pelo `SpaceflightSky.get_doppler_in_direction()`, porque é física.

⚠️ O ângulo é medido a partir da **velocidade baricêntrica**, não do prógrado. Não
é preciosismo: o prógrado do cockpit é relativo ao corpo de referência (7,7 km/s
em torno da Terra) enquanto o céu é aberrado pela velocidade no referencial em
que as estrelas estão paradas (30,7 km/s, dominada pela órbita da própria Terra).
Em LEO os dois apontam a uns 30° de distância. A primeira versão desta linha
media o ângulo de um e o `D` do outro — todos os números certos, e a leitura
mentindo assim mesmo.

Girar a câmera **não é manobra**: a atitude da nave continua onde o RCS a deixou,
e nada no estado muda.

O rótulo (`EXTERNAL`, `CHASE`, `VELOCITY`, `TARGET`) ganha `(free)` assim que a câmera sai do
preset, em vez de continuar afirmando uma posição que ela não tem mais. É o mesmo
cuidado do parágrafo acima: um mostrador cujos números estão todos certos ainda
pode mentir.

## A regra que esta pasta existe para respeitar

O alvo da GDExtension é **desligado por padrão**. Isso não é cautela: é o teste.
`spaceflight_core` e as 30 suítes precisam compilar e passar **sem nenhum engine
instalado** — é assim que se verifica que a separação do ADR-0002 continua real.

Dentro da ponte:

* o Godot recebe `SimulationSnapshot`, nunca ponteiro para o integrador;
* `double → float` acontece uma única vez, depois do `RenderTransform` subtrair a
  origem da câmera (`docs/architecture/rendering.md`);
* nenhuma exceção atravessa a ABI C do GDExtension — toda entrada passa por um
  guard que converte em erro logado e `false`;
* a taxa de quadros decide **quanto** tempo coordenado pedir, nunca **como**
  chegar lá: o passo continua sendo do integrador (§12, §21).

## O que ainda não é verdade na imagem

O Milestone 5 pagou o que estava listado aqui: as posições são aparentes, há
Doppler, *beaming* e rotação de Terrell, e o starfield é o Yale BSC5 e não ruído.
O que resta está em `docs/architecture/rendering.md` §6 — aberração rígida por
corpo, sem lente gravitacional, estrelas como corpos negros sem linhas
espectrais, sem extinção interestelar.

E uma nota de dependência: sem `catalogs/bsc5.dat` a cena **não** falha. O céu
fica vazio, o HUD diz `sky  no catalogue (scripts/fetch_star_catalog.sh)`, e a
dinâmica não muda em nada — a mesma política dos kernels, pelo mesmo motivo.
