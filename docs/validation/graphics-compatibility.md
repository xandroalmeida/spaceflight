# Compatibilidade gráfica: o que foi qualificado, e onde

**Milestone 6.2, seções 22 e 23.**

Este documento existe para dizer uma coisa que não estava dita em lugar nenhum:
**a renderização deste simulador foi verificada em exatamente uma configuração**,
e nenhum resultado gráfico deste repositório afirma mais do que isso.

---

## 1. A configuração qualificada

```text
Hardware   Apple M5 Pro
API        Metal
Renderer   Godot 4.5, Forward+
SO         macOS (Darwin 25.5.0)
Resolução  1024 x 640 (o arnês), 1920 x 1080 (uso normal)
```

Tudo em [starfield-debug.md](starfield-debug.md) e em
[relativistic-rendering-visual.md](relativistic-rendering-visual.md) foi medido
aí. Nada foi medido em mais lugar nenhum.

Isto **não bloqueia** o milestone (§23). É um registro do alcance da evidência.

---

## 2. Por que isso importa mais aqui do que num jogo comum

Os dois defeitos que mantiveram o starfield invisível durante todo o
Milestone 6 eram, os dois, **específicos de renderer**:

1. `render_mode unshaded` faz o Forward+ do Godot 4 descartar `EMISSION`. Em
   Mobile e em Compatibility o comportamento é outro.
2. A esfera do céu ficava abaixo de um passo de quantização de um buffer de
   profundidade de 24 bits. A precisão e o formato do depth buffer variam com a
   API e com o driver.

Nenhum dos dois é reproduzível por aritmética, e nenhum dos dois é portável de
graça. Um terceiro defeito da mesma família pode estar sentado em qualquer
combinação não testada, e a única coisa honesta a fazer é dizer quais são.

---

## 3. A separação headless / GPU

Antes deste milestone, `ctest` rodava 34 testes e todos passavam, numa árvore
cujo starfield estava quebrado. Nada mentia — simplesmente **nenhum daqueles
testes rasteriza um pixel**, e nada dizia isso ao leitor.

Agora há dois alvos, e os rótulos são o ponto:

```bash
cmake --build build --target ctest-headless     # aritmética; roda em qualquer lugar
cmake --build build --target gpu-validation     # precisa de framebuffer real
```

* `ctest-headless` = `ctest --label-regex "unit|integration|scientific|regression"`.
  Sem display, sem driver, sem janela. Um job de CI que roda isto e reporta
  "all tests passed" **não** está fazendo uma afirmação sobre os gráficos.
* `gpu-validation` = `ctest --label-regex "gpu"`. Dois casos:

  | teste | arnês | evidência |
  |---|---|---|
  | `gpu.starfield` | `scripts/starfield_validation.sh` | 10 estágios, PNGs + log |
  | `gpu.relativistic_visual` | ladder de captura via `SPACEFLIGHT_CAPTURE` | um quadro por degrau |

Os testes de GPU **pulam** (saída 77 → CTest reporta *Skipped*) quando falta o
Godot, falta a GDExtension compilada, ou não há display. Uma máquina que não pode
rodá-los não os reprovou; ela não os rodou, e esses são fatos diferentes. É a
mesma política dos testes que dependem de kernels SPICE.

O `--headless` do Godot **não** serve: o rasterizador dummy não desenha nada, e
um arnês que "passasse" sob ele estaria provando coisa nenhuma — que é
exatamente como o starfield sobreviveu quebrado a um milestone inteiro.

---

## 4. O que ainda não foi testado

| eixo | qualificado | não testado |
|---|---|---|
| API gráfica | Metal | Vulkan, Direct3D 12, OpenGL |
| renderer Godot | Forward+ | Mobile, Compatibility |
| SO | macOS | Windows, Linux |
| GPU | Apple Silicon (M5 Pro) | AMD, NVIDIA, Intel |
| precisão de profundidade | 24 bits, invertida (padrão do Forward+) | tudo o mais |

A suíte já está preparada para essa matriz: `scripts/gpu_validation.sh` não tem
nada de específico de plataforma além da detecção de display, e o arnês do
starfield lê pixels de volta por `Viewport.get_texture()`, que é portável. O que
falta é **hardware**, não código.

### O primeiro teste a rodar noutra máquina

`gpu.starfield`. Os dez estágios dele isolam camada por camada — catálogo, malha,
material, shader, profundidade, exposição — e o estágio que falhar nomeia a
camada. É o instrumento certo para descobrir se um driver novo trouxe um terceiro
defeito da família dos dois primeiros.

---

## 5. O que este milestone mudou aqui

Nada de estrutural (§22). O arnês de validação do starfield foi **preservado
como está**. O que mudou:

* o teste visual entrou explicitamente na suíte de regressão, com rótulo `gpu`;
* `ctest` deixou de dar a impressão de cobrir gráficos;
* os dois scripts passaram a sair 77 em vez de 1 quando faltam pré-requisitos;
* esta página passou a existir.
