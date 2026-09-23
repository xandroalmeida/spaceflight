# ADR-0009 — Apresentação própria: SDL3 + SDL_GPU + Dear ImGui

Status: **aceito** · Data: 2026-09-23 · Substitui: [ADR-0002](0002-render-engine.md)

## Contexto

O ADR-0002 pôs a apresentação no Godot 4.x via GDExtension. Oito milestones
depois, o que o Godot fazia pelo projeto era pouco e o que custava era muito:

* **o jogo não era um binário.** Rodar exigia o editor do Godot, a GDExtension
  compilada com o godot-cpp da mesma versão, e um scan do projeto que crasha ao
  sair no Godot 4.5 (`--import`). Distribuir era distribuir um projeto de engine;
* **a apresentação vivia em GDScript**, onde nada é tipado de ponta a ponta e os
  testes precisavam do Godot instalado (`godot.m7`, `godot.m8`);
* **os defeitos gráficos mais caros eram do engine, não da física**: o
  `render_mode unshaded` descartando `EMISSION` no Forward+ e o buffer de
  profundidade de 24 bits que engolia um terço do céu (Milestone 6) — ambos
  invisíveis ao código do projeto e só descobertos por um harness de pixels;
* **quase nada do engine era usado**: a cena é gerada por código (malhas
  procedurais, nenhuma cena importada), a física do engine é proibida pelo próprio
  ADR-0002, e a UI é texto e linhas.

O enunciado pede Mac e Linux hoje e Windows depois.

## Decisão

A apresentação passa a ser um **executável C++20 próprio**, `spaceflight`, sobre:

| Peça | Escolha | Por quê |
|---|---|---|
| janela, input, áudio | **SDL3** (3.4.16, estático) | portátil, sem runtime externo, mantido |
| GPU | **SDL_GPU** | uma API, três backends: **Metal** (macOS), **Vulkan** (Linux), **Direct3D 12** (Windows) |
| shaders | GLSL 450 → **glslang** → SPIR-V → **SPIRV-Cross** → MSL (e HLSL no Windows) | uma fonte por shader, compilada no build e embutida no binário |
| UI 2-D | **Dear ImGui 1.92** com renderer SDL_GPU próprio | HUD, painéis e as quatro telas do cockpit (desenhadas em texturas) |
| imagens | **stb_image / stb_image_write** | carregar texturas, gravar capturas |
| fonte | DejaVu Sans Mono (em `assets/fonts/`) | monoespaçada, com grego e setas; licença livre |

Todas as dependências entram por `FetchContent`, **fixadas por SHA-256** em
`cmake/third_party.cmake`, sem nada a instalar além do compilador e do CMake.

A divisão em camadas (`app/`):

| Camada | O que é | Depende de |
|---|---|---|
| `app/session` | `FlightSession` — o que o `SimulationNode` da GDExtension era: dono do estado, do relógio, do autopiloto, do planejador (numa thread) | core |
| `app/presentation` | tudo o que `flight.gd` e os scripts faziam: instrumentos, câmeras, cockpit, cena, painéis, roteiros de captura | session — **sem GPU e sem SDL** |
| `app/gfx` | o renderizador SDL_GPU e o renderer do ImGui | presentation, SDL |
| `app/platform`, `app/ui`, `app/main.cpp` | caminhos, áudio e teclado SDL; os painéis ImGui; o laço principal | tudo acima |

O que o ADR-0002 proibia continua proibido, agora com menos lugares para errar:
nenhuma física na apresentação, a posição na tela é uma **projeção** do estado
(`RenderTransform`, camera-relative), e a projeção nunca escreve de volta.

## Decisões de renderização que valem registro

* **Reverse-Z com profundidade `D32_FLOAT`** no mundo: a esfera do céu a
  `near/190000` é um float normal com 23 bits de mantissa abaixo dela. O defeito
  de 24 bits do Milestone 6 deixa de ser possível (a etapa 3b do harness do
  starfield mede isso a cada execução).
* **Estrelas como quads instanciados**, não primitivas de ponto: ponto não tem
  tamanho no Direct3D e tem no máximo um pixel em alguns drivers Vulkan.
* **Duas passadas de cena**: o mundo (corpos e estrelas, em unidades de cena,
  HDR linear, sem tonemap — a fotometria das estrelas é medida) e o campo próximo
  (casco, cockpit, pluma, em metros, 4× MSAA, tonemap fílmico), compostos com
  alfa pré-multiplicado e codificados em sRGB no composite.
* **O quadro é desenhado numa textura** e só depois copiado para a janela: uma
  captura é o próprio quadro, e as capturas não precisam de tela.

## Alternativas consideradas

| Alternativa | Por que não |
|---|---|
| continuar no Godot | os custos do Contexto; nenhum ganho que o projeto use |
| raylib / sokol | OpenGL (depreciado no macOS) ou sem backend D3D12/Metal de primeira classe |
| bgfx | build próprio pesado, shaders numa linguagem própria |
| Vulkan puro + MoltenVK | camada de tradução no Mac e três vezes o código de SDL_GPU |
| WebGPU (Dawn/wgpu) | dependência enorme (Dawn) ou em Rust (wgpu); SDL_GPU cobre os três alvos |
| Qt / GTK para a UI | peso e licença; a UI é texto e linhas, o ImGui basta |

## Consequências

* `cmake -S . -B build && cmake --build build` produz `build/bin/spaceflight`, um
  executável que roda sozinho; `cmake --install` coloca binário, assets, kernels
  e catálogos em `bin/` + `share/spaceflight/`.
* **Tudo o que era testado no Godot passou a ser testado sem ele**:
  `presentation.m7`, `presentation.m8` e `presentation.flight` (sem GPU, label
  `presentation`, parte da suíte headless) e `gpu.starfield` /
  `gpu.relativistic_visual` (off-screen, label `gpu`, sem display).
* Os roteiros de captura (`--shots m7|m8`, `--capture`, `--screenshot`) rodam
  **fora da tela**, com passo fixo de 1/60 s: dão a mesma imagem em qualquer
  máquina com GPU, sem janela.
* **Windows está previsto, não implementado**: o SDL_GPU já tem o backend D3D12 e
  `cmake/shaders.cmake` já tem o caminho HLSL (SPIRV-Cross → `fxc`, SM 5.1), mas
  nada disso foi compilado nem rodado num Windows.
* O Godot, o godot-cpp e todos os scripts ligados a eles saem do repositório.
