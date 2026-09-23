# Compatibilidade gráfica: o que foi qualificado, e onde

**Milestone 6.2, seções 22 e 23.** Atualizado para a pilha do
[ADR-0009](../adr/0009-render-stack.md) (SDL3 + SDL_GPU).

Este documento existe para dizer uma coisa que não estava dita em lugar nenhum:
**a renderização deste simulador foi verificada em exatamente duas configurações
com o renderizador atual** — Metal num Mac e Vulkan por software num Linux — (e
em duas com o Godot, que ele substituiu), e nenhum
resultado gráfico deste repositório afirma mais do que isso.

---

## 1. As configurações qualificadas

### 1.1 A de referência

```text
Hardware   Apple M5 Pro
API        Metal
Renderer   SDL_GPU (SDL 3.4), spaceflight (ADR-0009)
SO         macOS (Darwin 25.5.0)
Resolução  1024 x 640 (o arnês), 1440 x 900 (janela padrão)
```

Tudo em [starfield-debug.md](starfield-debug.md) e em
[relativistic-rendering-visual.md](relativistic-rendering-visual.md) foi medido
aí, e as imagens versionadas em `docs/validation/starfield/` e
`docs/validation/scene/` são dela. Com o renderizador atual, `gpu.starfield`
passou nos dez estágios e no 3b, e os snapshots do estágio 10 batem com as
referências semeadas na época do Godot a no máximo 0,00003 (orçamento 0,020).
A mesma máquina já tinha sido a de referência com o Godot 4.5, Forward+.

### 1.2 Linux arm64, Vulkan por software — o renderizador atual

```text
Hardware   Apple M5 Pro, num contêiner Docker (Linux aarch64)
API        Vulkan (Mesa llvmpipe, LLVM 20.1.2 — rasterização em CPU)
Renderer   SDL_GPU (SDL 3.4), spaceflight (ADR-0009), fora da tela
SO         Ubuntu 24.04, GCC 13.3, CMake 3.28
Resolução  1024 x 640 (o arnês)
```

Qualificada em 2026-09-23. É a prova de que o caminho **Linux + Vulkan** do
executável atual compila e desenha: o build inteiro sai sem nenhum warning em
`app/`, `gpu.starfield` passa nos dez estágios e no 3b com os mesmos orçamentos
do Mac (log em
[linux/starfield-validation-sdlgpu-llvmpipe.log](linux/starfield-validation-sdlgpu-llvmpipe.log)),
os snapshots do estágio 10 diferem das referências em no máximo 0,00003, e
`gpu.relativistic_visual` produz a escada inteira. As suítes `unit`,
`integration`, `presentation` e `assets` passam.

O que ela **não** prova: um driver de GPU de verdade no Linux. O llvmpipe é o
rasterizador de referência do Mesa, e o que falha num driver de hardware (como
o `hasvk` da §1.3) não falha nele. E `regression.reference_states` falha aí por
2,8 mm numa propagação LEO de 1,5·10¹¹ m: o teste fixa os dígitos "nesta
máquina" (clang/macOS) e o GCC no aarch64 reordena a aritmética. Não é da
apresentação — o core não mudou —, mas fica registrado aqui porque foi aqui que
apareceu.

### 1.3 Linux, Vulkan, Intel — qualificada com o Godot, a refazer

> ⚠️ **Esta qualificação é da pilha antiga.** Ela foi feita com o Godot 4.5; o
> renderizador SDL_GPU ainda **não** rodou em Linux nem em Vulkan. O registro
> abaixo fica como está porque é o que foi medido, mas não vale para o
> executável atual: `gpu.starfield` e `gpu.relativistic_visual` têm de ser
> rodados de novo nesta máquina.

```text
Hardware   Intel Core i5-3210M, Intel HD Graphics 4000 (Ivy Bridge GT2)
API        Vulkan 1.2 (Mesa, driver hasvk)
Renderer   Godot 4.5, Forward+
SO         Ubuntu 24.04, Linux 6.17, sessão Wayland
Resolução  1024 x 640 (o arnês)
```

Qualificada em 2026-09-23, com o Godot. `gpu.starfield` passou nos dez estágios, cada um
dentro do mesmo orçamento do Mac; `gpu.relativistic_visual` produziu a escada
inteira de quadros. O log fica em
[linux/starfield-validation.log](linux/starfield-validation.log). As imagens não
foram versionadas: as do Mac continuam sendo a referência, e as do Linux diferem
delas em no máximo 0,00031 no estágio 10 (orçamento 0,020).

É o extremo fraco da matriz, e isso é deliberado: uma GPU integrada de 2012,
num driver que ao abrir avisa `Ivy Bridge Vulkan support is incomplete`. Os dois
defeitos da §2 dependiam de renderer e de profundidade, e nenhum reapareceu aqui.
O que difere do Mac está dentro do ruído de rasterização: 1 591 blobs no
baseline do estágio 6 contra 1 714, a mesma proporção entre os efeitos.

Isto **não bloqueia** o milestone (§23). É um registro do alcance da evidência.

### 1.4 Windows, Direct3D 12 — previsto, não construído

O SDL_GPU tem backend Direct3D 12 e `cmake/shaders.cmake` tem o caminho HLSL, mas
nada disso foi compilado nem rodado num Windows. Nenhuma afirmação gráfica deste
repositório vale para lá.

---

## 2. Por que isso importa mais aqui do que num jogo comum

Os dois defeitos que mantiveram o starfield invisível durante todo o
Milestone 6 eram, os dois, **específicos de renderer** (do Godot, que era o
renderizador na época):

1. `render_mode unshaded` faz o Forward+ do Godot 4 descartar `EMISSION`. Em
   Mobile e em Compatibility o comportamento é outro.
2. A esfera do céu ficava abaixo de um passo de quantização de um buffer de
   profundidade de 24 bits. A precisão e o formato do depth buffer variam com a
   API e com o driver.

Nenhum dos dois é reproduzível por aritmética, e nenhum dos dois é portável de
graça. O renderizador atual elimina as duas causas por construção — não há modos
de material, e a profundidade do mundo é `D32_FLOAT` com Z reverso, que o
estágio 3b confere até `near = 10⁻⁴` —, mas troca o Godot por três backends do
SDL_GPU e três traduções de shader (MSL, SPIR-V, HLSL), e cada um é uma
combinação nova. Um terceiro defeito da mesma família pode estar sentado em qualquer
combinação não testada, e a única coisa honesta a fazer é dizer quais são.

---

## 3. A separação headless / GPU

Antes deste milestone, `ctest` rodava 34 testes e todos passavam, numa árvore
cujo starfield estava quebrado. Nada mentia — simplesmente **nenhum daqueles
testes rasteriza um pixel**, e nada dizia isso ao leitor.

Agora há dois alvos, e os rótulos são o ponto:

```bash
cmake --build build --target ctest-headless     # aritmética; roda em qualquer lugar
cmake --build build --target gpu-validation     # precisa de um driver de GPU
```

* `ctest-headless` = `ctest --label-regex "unit|integration|scientific|regression|presentation"`.
  Sem display, sem driver, sem janela (`presentation.*` desenha num `Canvas` que
  grava chamadas, não pixels). Um job de CI que roda isto e reporta
  "all tests passed" **não** está fazendo uma afirmação sobre os gráficos.
* `gpu-validation` = `ctest --label-regex "gpu"`. Dois casos:

  | teste | arnês | evidência |
  |---|---|---|
  | `gpu.starfield` | `spaceflight_starfield_validation` (`tests/gpu/starfield_validation.cpp`) | 10 estágios + 3b, PNGs + log |
  | `gpu.relativistic_visual` | `spaceflight --capture` | um quadro por degrau |

  Os dois passam por `scripts/gpu_validation.sh` e desenham **fora da tela**,
  numa textura lida de volta da GPU: precisam de um driver (Metal, Vulkan ou
  Direct3D 12), não de display.

Os testes de GPU **pulam** (saída 77 → CTest reporta *Skipped*) quando o binário
não foi compilado ou não há dispositivo GPU. Uma máquina que não pode
rodá-los não os reprovou; ela não os rodou, e esses são fatos diferentes. É a
mesma política dos testes que dependem de kernels SPICE.

O `spaceflight --headless` **não** substitui nenhum dos dois: ele não cria
dispositivo GPU nem desenha nada, só imprime a leitura técnica. Na época do
Godot a mesma armadilha era o `--headless` do engine, com um rasterizador dummy
— e um arnês que "passasse" sob ele estaria provando coisa nenhuma, que é
exatamente como o starfield sobreviveu quebrado a um milestone inteiro.

---

## 4. O que ainda não foi testado

| eixo | qualificado | não testado |
|---|---|---|
| backend SDL_GPU | Metal | Vulkan (só com o Godot, a refazer), Direct3D 12 (não construído) |
| SO | macOS | Linux (a refazer), Windows |
| GPU | Apple Silicon (M5 Pro) | Intel HD 4000 (só com o Godot), AMD, NVIDIA, Intel recente |
| precisão de profundidade | `D32_FLOAT`, Z reverso | — (é fixa no renderizador, não depende do driver) |

A suíte já está preparada para essa matriz: `scripts/gpu_validation.sh` não tem
nada de específico de plataforma, e o arnês do starfield lê pixels de volta por
`Gpu::download()` (`app/gfx/gpu.hpp`), sobre o SDL_GPU, que é o mesmo nos três
backends. O que falta é **rodar**: no Linux, a máquina da §1.3 com a pilha nova;
no Windows, primeiro compilar.

### O primeiro teste a rodar noutra máquina

`gpu.starfield`. Os dez estágios dele isolam camada por camada — catálogo, malha,
instâncias, shader, profundidade, exposição — e o estágio que falhar nomeia a
camada. É o instrumento certo para descobrir se um driver novo trouxe um terceiro
defeito da família dos dois primeiros.

---

## 5. O que o Milestone 6.2 mudou aqui

Nada de estrutural (§22). O arnês de validação do starfield foi **preservado
como está**. O que mudou:

* o teste visual entrou explicitamente na suíte de regressão, com rótulo `gpu`;
* `ctest` deixou de dar a impressão de cobrir gráficos;
* os dois scripts passaram a sair 77 em vez de 1 quando faltam pré-requisitos;
* esta página passou a existir.

Com o ADR-0009 o arnês foi portado para C++ sobre o renderizador do jogo, com os
mesmos estágios e as mesmas tolerâncias, e os testes de GPU deixaram de precisar
de display.
