# ADR-0008 — Atitude: quaternions unitários, convenção de Hamilton, corpo→inercial

Status: **aceito** · Data: 2026-09-13
Física: `docs/physics/attitude.md`

## Contexto

§19 do enunciado é explícito:

> A orientação deverá utilizar **quaternions**.
> Não altere diretamente pitch, yaw, roll. Eles são apenas representações para a
> interface.

O que precisa ser decidido não é *se* quaternion, e sim **qual** — porque existem
quatro convenções incompatíveis em circulação, todas escritas "quaternion", e
misturá-las produz rotações que parecem quase certas.

## Decisão

| Aspecto | Escolha |
|---|---|
| ordem das componentes | **escalar primeiro**: `(w, x, y, z)` |
| álgebra | **Hamilton** (`ij = k`), não JPL (`ij = −k`) |
| sentido da rotação | **corpo → inercial**: `v_inercial = q ⊗ v_corpo ⊗ q*` |
| norma | unitária, com deriva **medida** e projeção documentada (§5 de `attitude.md`) |
| velocidade angular | `ω` no **referencial do corpo** |

Cada fronteira converte explicitamente, em um único lugar:

| Fronteira | Convenção de lá | Conversão |
|---|---|---|
| Godot `Quaternion` | escalar **último** `(x,y,z,w)`, Hamilton, corpo→mundo | reordenar componentes |
| SPICE `q` (`m2q_c`) | escalar primeiro, mas **inercial→corpo** | conjugar |
| Euler (pitch/yaw/roll) | só para mostrador | derivado, nunca armazenado |

## Alternativas consideradas

| Alternativa | Por que não |
|---|---|
| Ângulos de Euler como estado | *gimbal lock*: a 90° de pitch dois eixos colapsam e a derivada explode. Proibido por §19, e com razão |
| Matriz de rotação (DCM) como estado | 9 números para 3 graus de liberdade; a ortogonalidade deriva e reortogonalizar é mais caro e menos estável que renormalizar um quaternion |
| Rodrigues modificado (MRP) | compacto (3 números) e sem gimbal lock, mas tem singularidade em 360° e exige troca de "sombra" no meio da integração |
| Convenção JPL (`ij = −k`) | usada em parte da literatura aeroespacial; escolher Hamilton por ser a do Godot e a da maioria das bibliotecas de matemática 3D reduz o número de conversões a fazer |
| Escalar último `(x,y,z,w)` | é a do Godot, mas a literatura de dinâmica escreve `(w,x,y,z)`; como a física vem antes do renderizador, a física ganha e a conversão fica na ponte |

## Consequências

* `math::Quaternion` é um tipo próprio, **não** um `Vec3` com um número extra: a
  multiplicação não é componente a componente e o tipo impede confundi-las.
* `q` e `−q` representam a **mesma** rotação. Comparações de orientação usam
  `|q₁·q₂|`, nunca `q₁ == q₂`, e a interpolação escolhe o sinal mais próximo.
* O estado de propagação cresce de 8 para 15 componentes
  (`[r v τ m q ω]`), e o dense output junto.
* Nenhuma função aceita ou devolve pitch/yaw/roll como estado. Existe
  `to_euler_zyx()` para o mostrador, e ele é rotulado como derivado.
* Toda conversão de fronteira tem teste de ida e volta.
