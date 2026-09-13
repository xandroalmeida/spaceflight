# Checklist de verificação visual — Milestone 6

Data: 2026-09-13  
Build: worktree do Milestone 6 sobre `2468a33`  
Ambiente: macOS, Godot 4.5, execução em janela

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
| starfield | cobertura 360°, sem costura, orientação reconhecível | **FAIL** | HUD informa 8786 estrelas e arrays válidos, mas nenhuma aparece no framebuffer, inclusive com HUD oculto e exposição máxima |
| aberração | cenário 0/.1/.5/.9/.99c, ON/OFF isolado | BLOCKED | seletor/HUD e teste de core passam; inspeção visual bloqueada pelo starfield invisível |
| Doppler | frente azul/traseira vermelha, ON/OFF isolado | BLOCKED | fatores no HUD variaram corretamente; efeito visual bloqueado pelo starfield |
| beaming | frente/traseira com exposição fixa, ON/OFF isolado | BLOCKED | toggle independente confirmado; efeito visual bloqueado pelo starfield |
| objeto extenso | planeta próximo, silhueta e sentido Terrell | pendente | |
| tempo retardado | Lua/Sol e alvo móvel artificial, ON/OFF | PARCIAL | toggle e posição de centro ativos; inspeção diferencial pendente |
| time warp | 1×…100000×, sem saltar ignição/corte | PARCIAL | 1×→10× confirmado em tela; matriz completa coberta numericamente |
| missão | planejar, executar, inserir e conferir órbita final | **FAIL** | campanha independente: 18/100 sucessos estritos; não repetir demo nominal como aprovação |

Comando base:

```bash
cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build-godot --target spaceflight_gdextension -j
external/godot/Godot.app/Contents/MacOS/Godot --path godot/project --editor
```

O ensaio termina somente após revisar também o log por `NaN`, `Inf`, falha de
shader, recurso ausente e exceção da GDExtension.

## Execução de 2026-09-13

Build nativa Metal/Forward+ aberta em janela, com capturas em 0c, 0.1c e 0.99c,
HUD full/compact/off, foco em nave e Sol e warp 1×/10×. O log ficou limpo de
`NaN`, `Inf`, erros de shader e exceções. O defeito de starfield acima é
reproduzível e bloqueia a aprovação visual relativística.
