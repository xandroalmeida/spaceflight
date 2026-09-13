# Godot (Milestones 2 e 5)

O Godot entra aqui **apenas como consumidor** do `spaceflight_core` (ADR-0002).
Nada nesta pasta calcula física — nem o GDScript, nem os shaders.

```
godot/
├── gdextension/     ponte C++: expõe SimulationSnapshot e o céu ao engine
│                    SpaceflightSimulation  estado, snapshots, posições aparentes
│                    SpaceflightSky         catálogo estelar + tabela de Planck
└── project/         projeto Godot 4.5: cena, câmera, HUD
    └── shaders/     star_field, relativistic_body
```

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
elapsed        82.767170 s   warp 10x
altitude       399.957 km
speed          7672.579 m/s
...
beta           0.0001025289154362769
render res.    0.8071619902045453 m per float ulp at Earth

stars          8786  (apparent)
forward cone   89.994 deg holds 4405 stars (50.14 %)
doppler        0.999897 astern .. 1.000103 ahead
5800 K star    1.000463768920553 x ahead, 0.9995364461543639 x astern  (visible band)
exposure       0.1585 half-saturation flux
light time     Moon 1.1937 s
```

Cada número é conferível: `v = √(GM/r)` a 6771 km dá 7672,6 m/s; o período
kepleriano dá 5544,87 s; a resolução de renderização é `6771 km · ε_float` =
0,807 m (`docs/architecture/rendering.md` §2); o tempo de luz até a Lua, 1,19 s,
é a distância dela dividida por `c`. A `β = 10⁻⁴` o Doppler é `1 ± 10⁻⁴` e o
brilho muda 0,05 % — o efeito **existe** e é desprezível, que é o que tem de ser
nessa velocidade.

### A cadência do print é em quadros, não em segundos

`--quit-after N` conta **quadros**. A versão do Milestone 2 imprimia a cada
segundo de relógio de parede, o que fazia a verificação depender da velocidade da
máquina: a 143 fps, `run_godot_headless.sh 200` imprimia uma vez — ou, numa
máquina mais rápida, nenhuma, e a saída ficava vazia sem nenhum erro. Agora é a
cada 150 quadros, e o mesmo comando dá a mesma saída em qualquer lugar.

### Voar de verdade até `β` relativístico

A ótica só fica visível acima de `β ≈ 0,1`, e o único jeito honesto de chegar lá é
queimar por oito anos. Com tela, a tecla `C` faz isso: CRUZEIRO, prógrado,
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

| Tecla | Ação |
|---|---|
| `,` `.` | desce/sobe o time warp (1× … 10⁸×) |
| `F` | alterna o foco entre a nave e cada corpo |
| `B` | escala dos corpos (1× … 1000×) |
| `R` | reinicia a órbita |
| `1`–`6`, `0` | modo de apontamento; `0` = manter |
| setas, `PgUp`/`PgDn` | torque manual do RCS |
| `Z` `X` `-` `=` | acelerador: cheio, corte, trim |
| `M` | modo do motor, IMPULSO ↔ CRUZEIRO |
| `E` `Q` | exposição do céu (§10.4) |
| `L` | tempo de luz + aberração: liga/desliga |
| `C` | queima de cruzeiro: CRUZEIRO + prógrado + acelerador cheio + warp 10⁸ |
| `W` `A` `S` `D`, ou botão direito do mouse | olhar em volta |
| `V` | trava da câmera: perseguição → prógrado → retrógrado |
| `H` | recentra o olhar |

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

A órbita é parametrizada num referencial construído a partir da **velocidade**, e
não dos eixos do mundo — é isso que faz o azimute significar alguma coisa aqui:

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

### Olhar em volta é uma medição

O `V` alterna entre as duas vistas em que a ótica mais difere, e o HUD diz o que
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

O rótulo (`chase`, `prograde`, `retrograde`) vira `free` assim que a câmera sai do
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
