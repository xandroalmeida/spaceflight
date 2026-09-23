# Brief para um modelo 3D da nave (regra 48)

**Estado: `OPTIONAL`.** A nave procedural do M7 é suficiente para o vertical
slice e não bloqueia nada. Este documento existe para que, se e quando alguém
quiser um modelo artístico, ele não tenha de ser reinventado — e para que ele
possa **substituir** a geometria procedural sem tocar em mais nada.

## O contrato

O modelo entra no lugar de `SpacecraftVisual` e tem de respeitar quatro coisas:

1. **`+x` é o nariz.** É a direção ao longo da qual `MainEngineForce` empurra, e
   ela não é negociável — vem de `core/propulsion/`. `+z` é "cima" da nave.
2. **A origem é o centro de massa**, que é o centro do casco pressurizado
   (ver [dimensions.md](dimensions.md)).
3. **Seis pontos de RCS**, em `(±2, 0, 0)`, `(0, ±2, 0)` e `(0, 0, ±2)`, em
   metros. São as posições que `RcsSystem::couples(2.0, ...)` usa, e os bicos
   têm de estar **ali** — não onde ficariam bonitos.
4. **Um ponto de bocal** em `(−17, 0, 0)` ou onde o modelo puser a saída do
   sino, exposto como um nó vazio chamado `EngineMount`, para a pluma se
   pendurar.

## O que o modelo precisa ter

Na ordem em que importam para a leitura da silhueta:

```
cockpit com janelas          o que diz onde fica a frente
motor com sino               o que diz onde fica a trás
radiadores planos e largos   o que diz "isto opera no vácuo"
tanques cilíndricos          o que diz "isto carrega propelente"
treliça entre os dois        o que diz "não há ar aqui"
antena de alto ganho         escala e detalhe
manta térmica dourada        a única cor saturada do veículo
```

## O que evitar

Asas, superfícies de controle, carenagens aerodinâmicas, cauda. A nave opera
exclusivamente no vácuo (regra 5). Nada de néon, nada de painéis brilhantes,
nada de logotipos grandes.

## Formato

`.glb` (glTF binário), triangulado, com normais e UVs. Máximo indicativo de
40 000 triângulos — é uma nave que ocupa um terço da tela no melhor caso.

Materiais PBR, com os nomes de `app/presentation/scene/ship_materials.hpp` para
que um material do projeto possa substituir um material do arquivo: `hull_paint`,
`bare_metal`, `dark_composite`, `glass`, `radiator`, `thermal_blanket`,
`engine_bell`, `solar_cell`.

## Como substituir

⚠️ **Hoje não há carregador de modelo.** No Godot bastava instanciar o `.glb` no
lugar de `SpacecraftVisual`; o executável do [ADR-0009](../../adr/0009-render-stack.md)
não lê glTF, e a nave é a lista de `Part` que `SpacecraftVisual`
(`app/presentation/scene/spacecraft_visual.cpp`) monta a partir das malhas de
`scene/mesh.cpp`. Substituí-la exige primeiro um leitor de `.glb` que produza
`MeshData` e `Part` — trabalho que não existe.

O contrato que esse leitor teria de manter é o mesmo de antes: a pluma pendura-se
em `SpacecraftVisual::engine_mount()`, a saída do sino no referencial do corpo, e
`RcsVisual::build()` continua a receber `FlightSession::rcs_thrusters()` — ele
coloca os jatos pelas posições do core e não pelo modelo, o que é o que faz a
regra 15 continuar verdadeira seja qual for a geometria.
