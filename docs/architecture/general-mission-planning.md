# Planejamento geral de missões

> Milestone 8. O que mudou entre "um simulador Terra–Lua" e "um simulador do
> Sistema Solar", e por que cada diferença existe.

---

## 1. A pergunta que a arquitetura passou a responder

Até o Milestone 7 o planejador respondia a uma pergunta com a Lua embutida no
enunciado. Agora responde a esta:

```text
estado atual da nave
      +  corpo de destino
      +  órbita desejada
      +  capacidades da nave
      +  objetivo
              ↓
          plano de missão
              ↓
           execução
```

Nada no tipo de nenhuma dessas etapas exige `Earth`, `Moon` ou `Mars`.

O que **não** mudou: gravidade, integrador, relatividade, propulsão, atitude,
SPICE, starfield e renderização relativística continuam como estavam. O trabalho
do M8 foi generalização, planejamento, interface e navegação — não uma reescrita
do núcleo.

---

## 2. O diretório de corpos

`core/celestial/solar_system.hpp` é **a** tabela de identidade e hierarquia. O
core, o planejador, a GDExtension e a interface leem dela; nenhum deles mantém
uma lista própria.

Ela é separada de `BodyCatalog` porque as duas respondem a perguntas diferentes:

| | `BodyCatalog` | `SolarSystem` |
|---|---|---|
| pergunta | de quem é a massa no modelo de forças? | o que existe e para onde dá para ir? |
| contém Marte como | baricentro 4 (`GM` do sistema) | planeta 499 |
| contém Fobos | não (não perturba nada) | sim |
| usada por | `PointMassGravity` | seleção, desenho, planejador |

Perguntar ao `BodyCatalog` "para onde o piloto pode ir" erra nos dois sentidos:
oferece pontos no vácuo e omite os corpos interessantes. O M7 disfarçava isso com
`radius > 0`, que funcionou enquanto a Lua era o único destino.

### O que é dado e o que não é

Identidade, tipo, pai e qualificação: **dado**, na tabela. `GM`, raio e se os
kernels sabem colocar o corpo: **não** — vêm da efeméride em tempo de execução.
Duplicar um `GM` aqui é como o número que a gravidade usa e o número que o
mostrador mostra começam a divergir.

### Posições substituídas

Os SPK dos próprios gigantes gasosos não são distribuídos com o projeto (jup347
tem centenas de megabytes). Para eles o **baricentro do sistema** entra no lugar,
e isso é legítimo para desenhar porque o baricentro de um sistema planetário fica
*dentro* do planeta — as luas são um décimo de milésimo da massa.

Medido, onde os dois existem: Marte 499 e o baricentro 4 estão a **10 cm** um do
outro em 2026-01-01.

A substituição é **recusada** para planejamento: uma órbita de captura é em torno
de um corpo, e uma órbita em torno de um baricentro é em torno de um ponto.
`SolarSystemBody::can_be_destination()` é esse teste, e um corpo que o reprova
aparece na interface como *observação*, não como missão (regra 21).

Marte tem SPK próprio porque `mar099s.bsp` entrou no conjunto padrão de kernels —
sem ele o corpo 499 não existe para o SPICE e Marte não é destino nenhum. Ver
`kernels/MANIFEST.md`.

---

## 3. Uma geometria, não um nome de corpo

`TransferGeometry` é a **única** coisa que difere entre os dois tipos de missão, e
ela é lida da hierarquia:

```text
LOCAL            o destino orbita a origem      Terra → Lua
INTERPLANETARY   ambos orbitam um primário      Terra → Marte
```

Acrescentar Titã não acrescenta um ramo: `common_primary(origin, destination)`
responde sozinho. Não existe `if (destination == Mars)` em lugar nenhum do core
(regras 78, 79).

Tudo depois da geração de candidatos é compartilhado: o mesmo corretor
diferencial, o mesmo plano-B, a mesma queima de captura, a mesma classificação.

---

## 4. Geração de candidatos

### LOCAL (inalterado)

Lambert em torno da **origem**, da órbita de estacionamento até onde o destino
estará. É o que a campanha de 365/365 épocas qualificou e o M8 não moveu uma
conta dele.

### INTERPLANETARY

Lambert em torno do **primário**, entre os dois corpos. Ao longo de duzentos dias
o Sol domina por quatro ordens de grandeza, e um Lambert resolvido em torno da
Terra descreveria uma trajetória que não existe.

A velocidade de partida que o Lambert devolve é **heliocêntrica**, não uma queima.
O que a transforma em queima é `core/trajectory/departure_hyperbola.hpp`.

---

## 5. A hipérbole de partida

A receita de manual,

```text
v_p = sqrt(v_inf² + v_escape²)
```

está certa quanto à **velocidade** e é silenciosa quanto à **direção** — porque é
escrita para uma saída do periapsis de uma hipérbole já orientada. Um planejador
que varre uma órbita de estacionamento não ganha isso de graça: ele está num
ponto arbitrário da órbita e precisa descobrir para onde apontar, e a resposta não
é "prógrado".

Dado `r` relativo ao corpo, a velocidade de excesso `v_inf` desejada e o `GM`, há
exatamente uma hipérbole por `r` cuja assíntota de saída está ao longo de `v_inf`
e que passa por `r` na distância certa. Duas equações a fixam:

```text
energia    v² = v_inf² + 2 GM / r          fixa a velocidade
geometria  ν_inf − ν = θ                   fixa a direção
```

com `θ` o ângulo entre `r` e `v_inf`, e `ν_inf = arccos(−1/e)`. Substituindo
`a = −GM/v_inf²`, isso vira uma equação só em `e`, resolvida por bisseção.

Verificado em `tests/unit/test_departure_hyperbola.cpp`: a assíntota reconstruída
a partir do estado devolvido bate com a pedida com erro de **1e-13 grau**, sobre
seis geometrias de partida diferentes.

O resultado é uma resposta de dois corpos e é usada como tal: um chute inicial
para o corretor diferencial, que então inverte o modelo completo (regra 36).

---

## 6. A captura, e por que ela tem três queimas em Marte e uma na Lua

Este é o ponto onde a generalização deixou de ser renomeação e virou física.

### O problema

Uma captura lunar é uma queima de 83 s contra uma velocidade de periapsis de
1,6 km/s. É quase um impulso: pede-se 100 × 100 km e sai 96,9 × 103,2 km.

Uma captura marciana nas velocidades que este motor produz não é. Medido:

```text
v_infinity de chegada     9,5 km/s
velocidade no periapsis  19,0 km/s
queima de captura         7,5 km/s  →  735 s
```

735 segundos a 19 km/s é o motor aceso ao longo de ~14 000 km — quatro raios
marcianos. O impulso não está onde o plano o colocou.

### Três tentativas, e o que cada falha ensinou

**1. Corretor de duas variáveis (Δv, deslocamento da queima).** Varrendo o
deslocamento:

| deslocamento | órbita resultante | e |
|---|---|---|
| −300 s | −957 × 4954 km | 0,549 |
| **0 s** | **192 × 993 km** | **0,101** |
| +366 s | −1493 × 7131 km | 0,695 |

Zero é **ótimo local**, então a coluna do Jacobiano correspondente ao
deslocamento é nula ali e Newton empaca. Mediu-se: 57 voos, sem convergir. O
botão não existe.

**2. Resolver a primeira queima para o PERIAPSIS.** Degenerado: o periapsis sobe
monotonicamente conforme a frenagem diminui, e com frenagem zero ele é o periapsis
da própria hipérbole — que é exatamente a altitude pedida, porque foi ali que o
plano-B mirou. O solver convergiu em "não queime", reportou sucesso, e devolveu
uma órbita sem período.

**3. Resolver a primeira queima para o APOAPSIS.** Frear o bastante para trazer o
lado distante a 500 km leva o lado próximo a **−523 km** — através do planeta.

### A solução

Uma queima não produz um círculo aqui. Duas produzem, desde que a primeira seja a
que a **mira** já acertou:

```text
1. mira do sobrevoo   resolvida, para antecipar a queda do periapsis
2. queima de captura  o Δv impulsivo, inalterado
3. trim               ~270 m/s no periapsis, baixando o apoapsis
```

A mira é resolvida por medição, não por modelo: voa-se a captura impulsiva uma
vez, lê-se quanto o periapsis caiu, e mira-se essa distância mais alto. O passe
seguinte parte da velocidade de partida corrigida do anterior (*warm start*) —
sem isso cada passe refaz uma correção completa de duzentos dias e a busca leva
dez minutos em vez de cinco segundos.

O trim é de ~270 m/s contra 7 500 m/s da captura, o que o torna **15 s** de
queima em vez de 735. É exatamente por isso que ele fecha o que a primeira não
fecha: 15 segundos realmente são quase o impulso que a aritmética de dois corpos
supõe.

### A Lua nunca chega aqui

A primeira coisa que o estágio de captura faz é voar a queima impulsiva e
comparar a órbita resultante com a pedida. Uma captura lunar passa nesse teste, e
nada abaixo dele roda. A sequência continua com uma queima só, e a trajetória é
exatamente a que a campanha qualificou — verificado: `96,901 × 103,184 km`,
`6557,0411 m/s`, idêntico dígito a dígito ao resultado anterior ao milestone.

---

## 7. Números que não escalam entre as geometrias

Três constantes eram lunares sem dizer. Cada uma virou uma escolha por geometria,
com a medição que a justifica no comentário do código:

| constante | LOCAL | INTERPLANETARY | por quê |
|---|---|---|---|
| tolerância de periapsis da mira | 2 km | 50 km | o passo de diferenças finitas do corretor (1e-3 m/s) move a chegada 1 km numa transferência lunar e **34 km** numa marciana; pedir 2 km é pedir um dezesseis avos do próprio passo |
| conic de partida abaixo da superfície | multa | **recusa** | localmente o periapsis vem de um Lambert que o corretor depois move centenas de m/s e frequentemente levanta; interplanetariamente ele vem da hipérbole exata e nada o levanta. Voar assim mesmo custa **226 000 passos por voo** contra 5 003, porque a trajetória atravessa a Terra |
| orçamento de passos | 2e6 | 50e6 | um voo lunar tem quatro dias e um marciano duzentos |
| prolongamento do voo de sondagem | 25 % do tof | 5 % | 25 % de 204 dias são 51 dias extras de integração em **cada** sondagem |

E um orçamento **local**, para os voos curtos do solver de captura: 200 000
passos. Herdar o orçamento da busca inteira significava que uma queima de teste
que raspasse o planeta podia moer cinquenta milhões de passos antes que alguém
soubesse — foi como um solve de dois segundos virou um de dez minutos.

---

## 8. O espaço de busca

### A janela de partida continua curta, inclusive para Marte

A resposta de manual é um período sinódico — 780 dias — porque um foguete químico
só consegue pagar a transferência perto de uma oposição. Esta nave não é um
foguete químico. Medido, partindo de 2026-01-01, que é uma data francamente ruim:

| tempo de voo | v∞ partida | v∞ chegada | total |
|---|---|---|---|
| 60 d | 61,9 km/s | 55,0 km/s | 116,9 km/s |
| 150 d | 33,6 | 18,5 | 52,0 |
| 260 d | 34,0 | 20,6 | 54,6 |
| 400 d | 23,3 | 14,5 | 37,8 |

contra um orçamento de **26 900 km/s** no modo IMPULSE. Todas são pagáveis por
três ordens de grandeza. Não há janela para esperar, então a busca não procura
uma — e a regra 40 é explícita quanto a não confinar artificialmente a nave às
trajetórias que um estágio químico voaria.

O que a janela **é**, nas duas geometrias, é uma revolução da órbita de
estacionamento: onde a nave está quando parte decide para onde a hipérbole de
partida pode apontar, e essa é uma pergunta com período de 92 minutos.

O campo continua configurável, para que um cenário com orçamento químico possa
pedir uma varredura sinódica. Ela custaria um *coast* muito longo na órbita de
estacionamento, que é por que não é o padrão.

### O tempo de voo

Derivado dos dois corpos: o tempo de Hohmann entre as duas órbitas é a **escala**
do problema, não a resposta. Dezesseis amostras de um quarto dele a 1,6 vezes ele
— para Terra→Marte, de 65 a 414 dias — de modo que a varredura *contém* o
compromisso em vez de ficar de um lado dele.

---

## 9. Custo da busca, medido

Terra→Marte, 2026-01-01, 24 pontos de partida × 16 tempos de voo × 2 ramos:

```text
768 geometrias consideradas
 24 sobreviveram ao crivo de dois corpos
  6 voadas
313 propagações
2,4e6 passos de integrador
 64 s
```

Uma única candidata, isolada, planeja em **5,6 s**.

64 s excede o orçamento da regra 119 ("primeiras candidatas úteis em poucos
segundos"), que é exatamente por que a regra 48 exige execução assíncrona com
progresso e cancelamento.

---

## 10. O que continua fora

Gravity assists, otimização de baixo empuxo, controle ótimo, múltiplos legs,
regime relativístico interplanetário. Registrados no backlog, não implementados
(regras 83–86).
