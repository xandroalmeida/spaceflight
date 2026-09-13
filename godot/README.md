# Godot (Milestone 2)

Vazio de propósito. O Godot entra no Milestone 2 e apenas como **consumidor** do
`spaceflight_core` (ADR-0002):

```
godot/
├── project/      projeto Godot 4.x: cenas, materiais, shaders, UI
└── gdextension/  ponte C++ (godot-cpp) que expõe SimulationSnapshot ao engine
```

Regras que já valem, antes de existir uma linha de código aqui:

* o core **não** sabe que o Godot existe e compila sem ele;
* o Godot recebe `SimulationSnapshot`, não ponteiros para o integrador;
* nenhuma física orbital em `RigidBody3D` ou no Scene Tree;
* `double` → `float` só depois do `RenderTransform` subtrair a origem da câmera;
* ativar com `cmake -DSPACEFLIGHT_BUILD_GODOT=ON`.
