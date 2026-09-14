# Primeiro voo

## Antes de abrir

O simulador precisa de três conjuntos de dados que não estão no repositório
porque não são nossos: as efemérides da NASA, o catálogo de estrelas e o motor
gráfico. Cada um tem um script que o busca.

```bash
./scripts/fetch_cspice.sh          # o toolkit SPICE da NASA
./scripts/fetch_kernels.sh         # efemérides DE440, ~120 MB
./scripts/fetch_star_catalog.sh    # Yale BSC5, 9110 estrelas
./scripts/fetch_godot.sh           # o motor, 152 MB
./scripts/fetch_godot_cpp.sh       # as bindings

cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build-godot --target spaceflight_gdextension -j
```

Depois disso, para voar:

```bash
./external/godot/Godot.app/Contents/MacOS/Godot --path godot/project
```

Se faltar alguma coisa, o simulador **não** falha em silêncio: sem os kernels ele
diz qual script rodar; sem o catálogo de estrelas o céu fica vazio e o mostrador
técnico explica porquê. Nada disso muda a dinâmica.

## O que aparece

Em poucos segundos você está sentado no cockpit, em órbita da Terra a 400 km,
com inclinação de 51,6 graus — a órbita da estação espacial. Não há menu, não há
tela de carregamento, e não há tutorial: a nave está operacional, o tanque está
cheio, o motor está desligado e a Lua já é o alvo.

<figure class="callouts" style="--w:1920;--h:1080">
<img src="../validation/m7/cockpit-earth-orbit.png" alt="O cockpit em órbita da Terra">
<b style="--x:6;--y:5">1</b>
<b style="--x:94;--y:5">2</b>
<b style="--x:50;--y:36">3</b>
<b style="--x:45;--y:57">4</b>
<b style="--x:26;--y:70">5</b>
<b style="--x:49;--y:70">6</b>
<b style="--x:71;--y:70">7</b>
<b style="--x:50;--y:90">8</b>
<b style="--x:15;--y:95">9</b>
<b style="--x:9;--y:64">10</b>
<figcaption>O primeiro quadro de um voo novo.</figcaption>
</figure>

1. **Time warp e corpo de referência.** Quantas vezes o tempo da simulação corre
   mais depressa que o seu relógio, e em torno de que corpo os números de órbita
   são medidos.
2. **Alvo.** Distância e velocidade relativa.
3. **As janelas.** A Terra está ali porque a nave parte apontada 25 graus abaixo
   do prógrado: de 400 km, o limbo fica 19,7 graus abaixo da horizontal local, e
   um nariz exatamente no prógrado poria o planeta inteiro debaixo do peitoril.
4. **Lâmpadas de estado.** `MSTR`, `RCS`, `ENG`, `AP`.
5. **`FLIGHT`** — para onde o nariz aponta e onde estão as direções que importam.
6. **`NAVIGATION`** — a órbita atual.
7. **`TARGET`** — o alvo, a distância e o fechamento.
8. **`SYSTEMS`** — motor, empuxo, propelente, massa, RCS, relatividade.
9. **Os sete botões**, clicáveis com o mouse.
10. **Mensagens da nave.** Aparecem, ficam alguns segundos, e apagam.

## As três teclas para começar

| | |
|---|---|
| `{{key:help}}` | a ajuda dos controles, dentro do jogo |
| `{{key:camera_cycle}}` | troca a câmera: cockpit, externa, perseguição, velocidade, alvo |
| `{{key:nav_panel}}` | o computador de missão |

## O primeiro minuto

Arraste com o **botão direito do mouse** para olhar em volta. Olhe para baixo:
os sete botões estão na borda do painel, onde um cockpit de verdade os põe.
Olhe para os lados: há janelas laterais. Olhe para cima: há o painel superior,
não o espaço.

Depois carregue em `{{key:camera_cycle}}` para sair e ver a nave.

![A nave vista de fora, do lado iluminado](../validation/m7/external-spacecraft.png)
