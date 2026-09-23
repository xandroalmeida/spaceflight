# spaceflight

Simulador científico de voo espacial no Sistema Solar.

Você pilota uma nave a partir de um cockpit 3D, planeja e executa missões —
órbita terrestre, Terra→Lua, Terra→Marte — e tudo o que acontece na tela sai de
física verificável: efemérides do JPL, gravidade de N corpos, propagação com
controle de erro e, em alta velocidade, relatividade especial e geral de campo
fraco, inclusive na imagem (aberração, Doppler, rotação de Terrell).

![Órbita marciana vista de fora](docs/validation/m8/12-mars-orbit-external.png)

## Objetivos

Construir uma simulação interativa **fisicamente consistente** do voo de uma
nave pelo Sistema Solar, incluindo regimes relativísticos, com separação estrita
entre física, astronomia, visualização e gameplay.

Na prática:

* **O núcleo é uma biblioteca C++20 sem engine gráfico.** Compila, roda e é
  testado sem o Godot instalado. O Godot só desenha o que o núcleo entrega
  (ADR-0002); nada na pasta `godot/` calcula física.
* **O critério de sucesso é numérico, não visual.** Toda grandeza produzida é
  comparada contra uma referência externa (JPL Horizons/SPICE, REBOUNDx, solução
  analítica, medida experimental) com tolerância documentada.
* **Nada de atalhos que um jogo faria:** sem esferas de influência, sem `clamp`
  de velocidade, sem efeito visual que não venha de um modelo físico.

## Estado

**Milestone 8 concluído:** Sistema Solar inteiro desenhado a partir do SPICE,
planejador geral de missões e a viagem Terra→Marte voável do princípio ao fim
(204 dias, três queimas, órbita marciana de ~500 km).

O histórico de cada milestone, com o que foi medido e o que ficou por fazer,
está em [`docs/validation/`](docs/validation/) — o mais recente é
[`milestone-8-report.md`](docs/validation/milestone-8-report.md), que também traz o
backlog. Para aprender a pilotar, leia o
[manual do piloto](docs/manual/manual.pdf).

## Requisitos

| | |
|---|---|
| SO | macOS (arm64, Metal) ou Linux x86_64 (Vulkan) — os dois qualificados |
| Compilador | C++20: Apple clang ou GCC ≥ 13 |
| CMake | ≥ 3.24 |
| Ferramentas | `bash`, `curl`, `python3` (só stdlib), `git` |
| Disco | ~250 MB de downloads (CSPICE, kernels DE440, Godot) |
| Opcional | Chrome/Chromium, para gerar o PDF do manual |

O Godot 4.5 e as bindings `godot-cpp` são baixados pelos scripts do projeto;
não é preciso instalá-los. Todos os dados de terceiros (kernels, catálogo
estelar, toolkit NAIF) também são baixados, com checksum, e ficam fora do Git.

## Como rodar

### O núcleo e a CLI

```bash
./scripts/fetch_kernels.sh          # ~33 MB de kernels SPICE (DE440)
./scripts/fetch_star_catalog.sh     # 560 kB: Yale BSC5
cmake -S . -B build                 # baixa e compila o CSPICE na primeira vez
cmake --build build -j
```

```bash
./build/bin/orbit-cli body Earth --date 2026-01-01
./build/bin/orbit-cli propagate tests/scenarios/leo-raise-apoapsis.json
./build/bin/orbit-cli intercept tests/scenarios/lunar-intercept.json --to Moon --tof 4.5
```

Outros cenários em [`tests/scenarios/`](tests/scenarios/README.md).

### O jogo

```bash
./scripts/fetch_godot_cpp.sh        # bindings C++ (godot-4.5-stable)
./scripts/fetch_godot.sh            # o editor 4.5-stable, em external/
cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build-godot --target spaceflight_gdextension -j
./scripts/run_godot_headless.sh     # registra a extensão e verifica a cena sem tela
```

Depois, para abrir o jogo:

```bash
# macOS
./external/godot/Godot.app/Contents/MacOS/Godot --path godot/project
# Linux
./external/godot/Godot_v4.5-stable_linux.x86_64 --path godot/project
```

Controles: [`docs/gameplay/controls.md`](docs/gameplay/controls.md). Detalhes do
lado Godot, versões e armadilhas conhecidas: [`godot/README.md`](godot/README.md).

### Testes

```bash
cmake --build build --target ctest-headless   # a física, sem tela
cmake --build build --target godot-tests      # a camada de apresentação, sem tela
cmake --build build --target gpu-validation   # os pixels; precisa de tela real
```

Testes que dependem de algo ausente (kernels, Godot, tela) saem com código 77 e
aparecem como `Skipped`, nunca como aprovados. `scientific.full_mission` pode levar
horas numa máquina modesta.

## Contribuir

### Antes de começar

1. Leia a [arquitetura do sistema](docs/architecture/system-architecture.md): a
   regra de dependência, as camadas e o mapa do repositório.
2. Leia os [ADRs](docs/adr/). As decisões registradas ali não se rediscutem sem
   uma razão técnica forte e demonstrável:

   | ADR | Decisão |
   |---|---|
   | 0001 | C++20 no núcleo; C só na fronteira com bibliotecas nativas |
   | 0002 | Godot 4.x via GDExtension, apenas renderização; nunca física |
   | 0003 | CSPICE + DE440 como fonte autoritativa; a nave é partícula-teste |
   | 0004 | Estado em SSB / J2000 (ICRF), SI, TDB em representação de duas partes |
   | 0005 | Dormand–Prince 5(4) adaptativo, desacoplado de frame e de time warp |
   | 0006 | Dense output de 4ª ordem: estado em qualquer instante |
   | 0007 | Configuração em JSON com comentários, parser no core |
   | 0008 | Quaternions: escalar primeiro, Hamilton, corpo→inercial |

### Regras que valem para toda mudança

1. **Nenhuma esfera de influência.** Todas as fontes gravitacionais contribuem em
   todo instante.
2. **Nenhum clamp.** `if (v > c) v = c` é proibido; o limite tem de sair da
   formulação ([`relativity-roadmap.md`](docs/physics/relativity-roadmap.md) §3).
3. **Documento → teste analítico → implementação → teste numérico.** Nunca efeito
   visual primeiro e física inventada depois. Cada modelo tem sua derivação em
   [`docs/physics/`](docs/physics/).
4. **Toda tolerância é justificada.** O harness de testes exige uma justificativa
   em cada comparação aproximada, e o número vai para
   [`docs/validation/tolerances.md`](docs/validation/tolerances.md).
5. **O core não conhece o Godot.** Nenhum include, link ou conta de física em
   `godot/`; o shader recebe grandezas prontas do core.
6. **Uma única implementação.** Jogo, CLI e campanhas de validação entram pelas
   mesmas funções públicas do core; não se duplica astrodinâmica na ponte.
7. **Dívida conhecida é registrada, não escondida.** Se algo fica de fora, vai
   para o documento correspondente com o motivo e o caminho para pagar.

### Antes de abrir um PR

* `ctest-headless` passa (e `godot-tests`, se a mudança toca `godot/`);
* build sem warnings novos — `-DSPACEFLIGHT_WERROR=ON` ajuda a garantir;
* o documento de física/arquitetura afetado está atualizado;
* se uma tecla mudou, `docs/gameplay/controls.md` foi regerado
  (`scripts/dump_controls.sh`) e o manual recompilado (`scripts/build_manual.sh`).

### Onde está cada coisa

| Pasta | Conteúdo |
|---|---|
| [`docs/architecture/`](docs/architecture/) | camadas, coordenadas, navegação, renderização |
| [`docs/physics/`](docs/physics/) | derivação, domínio de validade e dívidas de cada modelo |
| [`docs/validation/`](docs/validation/) | relatórios de milestone, campanhas, tolerâncias |
| [`docs/gameplay/`](docs/gameplay/) | controles, cockpit, mapa, missões |
| [`docs/assets/`](docs/assets/) | estado e especificação de texturas, modelos e áudio |
| [`docs/manual/`](docs/manual/) | fonte do manual do piloto |
| [`docs/PROMPT-0.md`](docs/PROMPT-0.md) | o enunciado original do projeto |
