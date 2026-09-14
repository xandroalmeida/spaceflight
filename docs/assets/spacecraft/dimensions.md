# Dimensões da nave (regra 6)

A nave desenhada por `godot/project/scripts/world/spacecraft_visual.gd`, em
metros, no referencial do corpo (`+x` é o nariz, `+z` é "cima").

| | valor |
|---|---|
| comprimento total | **22,0 m** (do nariz em `x = +5,0` à saída do sino em `x = −17,0`) |
| largura máxima | **14,0 m** (ponta a ponta dos radiadores) |
| largura sobre os tanques | 8,6 m |
| diâmetro do casco pressurizado | 3,0 m |
| comprimento do casco pressurizado | 9,0 m (`x` de −4,0 a +5,0) |
| volume pressurizado bruto | ≈ 56 m³ |
| volume habitável estimado | ≈ 35 m³ |
| cockpit | ≈ 1,8 m de comprimento × 2,0 m de largura × 1,9 m de altura |
| tanques | 2 × (3,4 m de diâmetro × 7,4 m) ≈ 2 × 58 m³ |
| seção de potência | 2,6 m de diâmetro × 2,5 m |
| motor principal | sino de 4,0 m de saída × 4,0 m de comprimento |
| radiadores | 2 painéis de 6,6 m × 5,4 m |
| braço do RCS | 2,0 m — **o mesmo de `RcsSystem::couples(2.0, ...)`** |

## Quatro tripulantes

35 m³ habitáveis para quatro pessoas dá 8,8 m³ por pessoa. Para comparação: a
Orion tem 8,95 m³ **no total** para quatro, o módulo de comando da Apollo tinha
6,2 m³ para três. Esta nave é generosa pelos padrões de uma cápsula e apertada
pelos padrões de uma estação — que é a proporção certa para uma travessia
Terra-Lua de uma semana, e é o que a regra 6 pede ("viagens prolongadas" com
"proporções externas plausíveis", sem engenharia estrutural detalhada).

## ⚠️ A dívida que estas dimensões deixam registrada

O tensor de inércia do core é uma **caixa sólida de 1000 kg e 8 × 3 × 3 m**
(`simulation_node.cpp`, `configure`). O casco pressurizado desenhado aqui tem
9 × 3 × 3 m: essa caixa. Tanques, treliça, seção de potência, radiadores e motor
ficam **fora** dela, para trás — mais treze metros de veículo e a maior parte
dos 19 toneladas de propelente.

Ou seja: **a inércia modela a distribuição de massa de um veículo menor do que o
desenhado.** As consequências são reais e limitadas:

* os momentos de inércia sobre os eixos transversais estão subestimados, então a
  nave gira mais depressa do que uma nave com esta geometria giraria;
* o centro de massa está no centro do casco pressurizado, e não entre ele e os
  tanques.

Isto **não** foi corrigido no M7, e a razão é a regra 57: "perfect spacecraft
mass distribution" está explicitamente fora deste milestone. Corrigir o tensor
mudaria a dinâmica de atitude e invalidaria as campanhas de apontamento do
Milestone 6.2 (`docs/validation/autopilot-hardening.md`,
`docs/validation/lunar-navigation-campaign-v2.csv`), que qualificam o planejador
contra números medidos.

Está no backlog como **PHYSICS_DEBT**, e a forma de o pagar é uma só: derivar o
tensor da geometria — cascas cilíndricas, tanques cheios e vazios, painéis — e
**re-qualificar** a campanha contra os novos números, em vez de trocar uma
constante e assumir que nada mais muda.

## O que NÃO é modelado

Estrutura, cargas, térmica, potência, suporte de vida, acoplamento, tripulação.
Nada disso existe em `core/`, e desenhar um radiador não cria um sistema de
rejeição de calor. A geometria é aparência (regra 7).
