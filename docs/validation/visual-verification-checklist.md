# Checklist de verificação visual — Milestone 6 / 6.1

Data: 2026-09-13 (Milestone 6), atualizado no Milestone 6.1  
Build: worktree do Milestone 6 sobre `2468a33`; linhas marcadas **6.1** foram
remedidas sobre `7181a30`  
Ambiente: macOS, Godot 4.5, Metal 3.2, Forward+, execução em janela

> **Pilha atual.** A lista foi preenchida com a apresentação em Godot. Desde o
> [ADR-0009](../adr/0009-render-stack.md) o jogo é o executável `spaceflight`
> (SDL3 + SDL_GPU). As quatro linhas numéricas marcadas **6.1** (starfield,
> aberração, Doppler, beaming) são remedidas a cada `gpu.starfield`, e o arnês
> C++ passou nelas em Metal com os mesmos números
> ([starfield-debug.md](starfield-debug.md)). As linhas que dependem de um
> operador (`PARCIAL`, `PENDENTE`) **não** foram refeitas na pilha nova e valem
> como registro do Godot até alguém percorrê-las de novo.

As cinco linhas que o Milestone 6 deixou em `FAIL` ou `BLOCKED` foram fechadas no
Milestone 6.1 e trazem a evidência nova; as `PENDENTE`/`PARCIAL` continuam onde
estavam, porque dependem de um operador humano e não foram exercitadas nesta
entrega. Ver [milestone-6-1-report.md](milestone-6-1-report.md).

Headless não aprova nenhum item desta lista. Para cada linha registrar
`PASS`, `FAIL` ou `BLOCKED`, resolução, FPS, estado/β e evidência (captura ou
vídeo). Um `FAIL` deve ganhar descrição reproduzível; não ajustar a física para
esconder um defeito visual.

| Área | Procedimento | Resultado | Problema/evidência |
|---|---|---|---|
| câmera | orbitar com mouse e WASD; cruzar polos; alternar foco | PARCIAL | foco nave→Sol funcionou; polos/mouse não qualificados nesta sessão |
| sensibilidade | comparar mouse lento/rápido e teclado | PENDENTE | requer operador humano contínuo |
| zoom | percorrer limites em nave, Terra, Lua e Sol | PENDENTE | |
| floating origin | focar nave/corpos a 1 AU e procurar jitter | PARCIAL | nave e Sol permaneceram estáveis; falta varredura a 1 AU |
| escala de planetas | alternar 1×/10×/100×/1000× e detectar câmera interna | PENDENTE | |
| escala da nave | comparar HUD e geometria declaradamente exagerada | PASS | nave visível e o exagero permanece declarado no código/HUD |
| HUD | 1280×720, 1920×1080 e redimensionamento contínuo | PARCIAL | layout de duas colunas legível em 1440×900; outras resoluções pendentes |
| cockpit/RCS | seis apontamentos, torque manual, quaternions sem flip | pendente | |
| starfield | cobertura 360°, sem costura, orientação reconhecível | **PASS (6.1)** | dois defeitos: `unshaded` descarta EMISSION, e a esfera do céu ficava abaixo de um passo do buffer de profundidade de 24 bits. 1851 de 1853 estrelas no quadro encontradas; as duas são pares próximos cujos blobs se fundiram. [starfield-debug.md](starfield-debug.md) |
| aberração | cenário 0/.1/.5/.9/.99c, ON/OFF isolado | **PASS (6.1)** | ângulo da GPU contra `core/relativity/optics.hpp` em β = 0; 0,1; 0,5; 0,9; 0,99 — pior erro 0,199° contra orçamento de 0,359°. `docs/validation/scene/scene_forward_beta_*.png` |
| Doppler | frente azul/traseira vermelha, ON/OFF isolado | **PASS (6.1)** | cromaticidade contra a tabela de Planck deslocada, 3 temperaturas × 2 ângulos: pior erro 0,0012 contra 0,04 |
| beaming | frente/traseira com exposição fixa, ON/OFF isolado | **PASS (6.1)** | razão de intensidades contra D⁴ limitado à banda: pior erro 0,0146 contra 0,12; absoluto 0,0086 contra 0,03 |
| objeto extenso | planeta próximo, silhueta e sentido Terrell | pendente | |
| tempo retardado | Lua/Sol e alvo móvel artificial, ON/OFF | PARCIAL | toggle e posição de centro ativos; inspeção diferencial pendente |
| time warp | 1×…100000×, sem saltar ignição/corte | PARCIAL | 1×→10× confirmado em tela; matriz completa coberta numericamente |
| missão | planejar, executar, inserir e conferir órbita final | **PASS (6.1)** | campanha independente: 100/100 e 365/365, sob critério mais estrito (a órbita pedida, não `ε < 0`). [lunar-navigation-hardening.md](lunar-navigation-hardening.md) |

Comando base:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
./build/bin/spaceflight --gpu-debug          # janela; --gpu-debug liga a validação do driver
```

O ensaio termina somente após revisar também o log por `NaN`, `Inf`, falha de
shader ou de pipeline, recurso ausente (`cannot read ...`) e mensagens da
validação do driver. (No Godot, o último item era "exceção da GDExtension".)

## Execução de 2026-09-13

Build nativa Metal/Forward+ aberta em janela, com capturas em 0c, 0.1c e 0.99c,
HUD full/compact/off, foco em nave e Sol e warp 1×/10×. O log ficou limpo de
`NaN`, `Inf`, erros de shader e exceções. O defeito de starfield acima é
reproduzível e bloqueia a aprovação visual relativística.
