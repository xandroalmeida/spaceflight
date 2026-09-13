# ADR-0002 — Engine gráfico: Godot 4.x via GDExtension

Status: **aceito** · Data: 2026-09-13 · Decisor: definido no enunciado do projeto

## Contexto

Precisamos de renderização, UI de cockpit, câmera, áudio, input e shaders, sem
que nada disso contamine a física. O núcleo precisa ser compilável e testável
sem abrir o engine.

## Decisão

**Godot 4.x estável**, integrado por **godot-cpp + GDExtension**, a partir do
Milestone 2.

Godot é responsável por: renderização, UI, cockpit, câmera, áudio, input,
shaders, modelos, efeitos.

Godot **não** é responsável por: física orbital, integração numérica, tempo da
simulação, estado da nave.

Proibições explícitas:

* `RigidBody3D` (ou qualquer corpo físico do Godot) para movimento orbital;
* física interna do Godot como fonte de verdade;
* coordenadas do Scene Tree como coordenadas astronômicas.

A posição no Godot é uma **projeção** do estado, produzida pelo `RenderTransform`
(camera-relative, floating origin), e a projeção nunca escreve de volta no estado.

## Alternativas consideradas

| Alternativa | Por que não |
|---|---|
| GDScript chamando o core | overhead por frame e tentação de mover lógica para o script |
| Godot como módulo customizado (recompilar o engine) | build muito mais pesado; GDExtension dá a mesma potência com ciclo rápido |
| Unreal / Unity | licenças e peso maiores; nenhuma vantagem para o problema |
| Renderizador próprio (Vulkan/OpenGL) | mês de trabalho que não é o objetivo do projeto |
| Sem engine, só CLI | insuficiente a partir do Milestone 2 |

## Consequências

* `spaceflight_core` compila e é testado sem Godot instalado — é a verificação de
  que a separação está sendo respeitada.
* A GDExtension é um alvo CMake separado (`godot/gdextension`), desativado por
  padrão (`-DSPACEFLIGHT_BUILD_GODOT=ON` para ligar).
* O contrato entre as duas metades é `SimulationSnapshot`, uma estrutura de dados
  simples; a UI jamais consulta o integrador.
* `double` → `float` acontece uma única vez, depois da subtração da origem da
  câmera (§23 do enunciado).
