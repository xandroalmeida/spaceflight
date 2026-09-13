# Godot (Milestone 2)

O Godot entra aqui **apenas como consumidor** do `spaceflight_core` (ADR-0002).
Nada nesta pasta calcula física.

```
godot/
├── gdextension/     ponte C++: expõe SimulationSnapshot ao engine
└── project/         projeto Godot 4.5: cena, câmera, HUD, starfield
```

## Construir e rodar

```bash
./scripts/fetch_godot_cpp.sh                 # ~750 kB; as bindings são geradas na build
./scripts/fetch_godot.sh                     # o editor 4.5-stable, em external/ (152 MB)
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
$ ./scripts/run_godot_headless.sh

elapsed        1.005643 s   warp 1x
proper time    1.005643 s
clock diff     1.3322676295501878e-15 s

reference      Earth
altitude       400.000 km
speed          7672.594 m/s
acceleration   8.705663 m/s^2

apoapsis       6771.019 km
periapsis      6770.997 km
eccentricity   0.00000164
inclination    51.6000 deg
period         5544.87 s

target         Moon at 358366 km, 7369.2 m/s
beta           0.00010018823279613427
gamma - 1      5.018841033189345e-09
render res.    0.8071670961862926 m per float ulp at Earth
```

Cada número aí é conferível: `v = √(GM/r)` a 6771 km dá 7672,6 m/s; o período
kepleriano dá 5544,87 s; a resolução de renderização é `6771 km · ε_float` =
0,807 m, que é a linha da tabela em `docs/architecture/rendering.md` §2. Em
execuções mais longas a excentricidade osculadora sobe de 1,6·10⁻⁶ para
9,8·10⁻⁶ — a oscilação de curto período do J₂ prevista em
`docs/physics/geopotential.md` §8.

O HUD é espelhado em `stdout` uma vez por segundo quando não há tela, para que
`--quit-after N` seja verificação de verdade e não um no-op silencioso.

## Controles

| Tecla | Ação |
|---|---|
| `,` `.` | desce/sobe o time warp (1× … 100 000×) |
| `F` | alterna o foco entre a nave e cada corpo |
| `R` | reinicia a órbita |

## A regra que esta pasta existe para respeitar

O alvo da GDExtension é **desligado por padrão**. Isso não é cautela: é o teste.
`spaceflight_core` e as 20 suítes precisam compilar e passar **sem nenhum engine
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

Registrado para não ser descoberto como bug no Milestone 5: as posições são
geométricas, não aparentes — sem tempo de trânsito da luz, sem aberração, sem
Doppler, sem contração de Lorentz. O starfield é aleatório, não um catálogo. Ver
`docs/architecture/rendering.md` §6.
