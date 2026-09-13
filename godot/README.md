# Godot (Milestone 2)

O Godot entra aqui **apenas como consumidor** do `spaceflight_core` (ADR-0002).
Nada nesta pasta calcula física.

```
godot/
├── gdextension/     ponte C++: expõe SimulationSnapshot ao engine
└── project/         projeto Godot 4.5: cena, câmera, HUD, starfield
```

## Construir

```bash
./scripts/fetch_godot_cpp.sh                 # ~750 kB; as bindings são geradas na build
cmake -S . -B build -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build --target spaceflight_gdextension -j
```

A biblioteca sai em `godot/project/bin/spaceflight.dylib` (ou `.so`/`.dll`), que é
onde `spaceflight.gdextension` a procura. Depois é só abrir `godot/project/` com o
Godot **4.5 estável** e rodar.

⚠️ A versão do godot-cpp **tem de bater** com a do editor: as bindings são geradas
a partir do `extension_api.json` do engine. Para outra versão:

```bash
GODOT_CPP_VERSION=godot-4.4.1-stable ./scripts/fetch_godot_cpp.sh
```

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
