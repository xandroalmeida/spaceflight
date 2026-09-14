# O cockpit

O que está na tela, de onde cada número vem, e o que fazer com ele.

A regra que vale para a página inteira: **nada aqui é calculado pelo Godot.**
Cada valor saiu de uma chamada a `SpaceflightSimulation`, que é a única porta do
`core/` para a apresentação (ADR-0002, regra 77 do M7). O cockpit escolhe a
unidade e desenha; ele não integra uma órbita, não resolve um Lambert e não
inventa empuxo.

---

## A Terra, e como se sabe que ela está na posição certa

A textura é aparência; a posição, a escala e a **rotação** vêm de `core/` e dos
kernels (regra 47). A rotação em particular é conferível, e é conferida contra um
facto de fora do programa: às 00:00 UTC o ponto subsolar está perto de 180° E,
porque o meio-dia solar em Greenwich é às 12:00, e perto de 23° SUL, porque é
janeiro. `godot/project/tests/probe_orientation.gd` responde
`longitude 180,92 E, latitude −23,01` — e depois fecha a cadeia até a textura:
o ponto subsolar cai em `uv (0,003; 0,628)`, que é oceano, e o antissolar em
`uv (0,503; 0,372)`, que é o Saara.

Isso não é um detalhe de acabamento. A rotação estava **transposta** até as
texturas chegarem, e com uma esfera de cor lisa não havia como ver.

## As duas escalas

A cena tem **1 unidade = 1 000 000 metros**. A Terra tem 6,371 unidades de raio,
a Lua fica a 358. A nave tem 22 **metros** — 0,000022 unidades, duas mil vezes
dentro do plano próximo da câmera.

Por isso há duas câmeras, com a mesma orientação e o mesmo campo de visão:

| | unidades | plano próximo | o que desenha |
|---|---|---|---|
| `world` | 1 = 1e6 m | 0,05 (50 km) | planetas, estrelas, órbitas |
| `near` | metros | 0,05 m | casco, cockpit, pluma, jatos |

A conversão entre elas é uma multiplicação, em um lugar só
(`scripts/camera/camera_rig.gd`, `_commit`). As duas não são "uma a seguir a
outra": são a **mesma câmera expressa em duas unidades**, e é por isso que a
paralaxe entre a nave e o planeta atrás dela sai certa.

O que isso custa, dito por inteiro: a camada próxima não é ocluída pela
distante — um planeta nunca passa à frente do casco. Para uma câmera que ou está
dentro da nave ou a algumas dezenas de metros dela isso não acontece, e está
registrado como VISUAL_DEBT.

---

## O painel

```
 ┌──────────────────────────────────────────────────────────────┐
 │                     JANELAS FRONTAIS                         │
 ├──────────────────────────────────────────────────────────────┤
 │   ● MSTR      ● RCS       ● ENG       ● AP                   │
 ├────────────────┬────────────────┬────────────────────────────┤
 │    FLIGHT      │   NAVIGATION   │          TARGET            │
 ├────────────────┴────────────────┴────────────────────────────┤
 │ ENGINE │ THRUST │ PROPELLANT │ MASS │ RCS │ RELATIVITY       │
 ├──────────────────────────────────────────────────────────────┤
 │ ENGINE   RCS   AP   NAV   MAP   WARP   MODE                  │
 └──────────────────────────────────────────────────────────────┘
```

Os ângulos não são decoração: com 75 graus verticais de campo de visão e a
cabeça a repousar 10 graus abaixo da linha do nariz, a banda visível vai de
+27,5 a −47,5 graus. As lâmpadas estão a −23, os três mostradores a −28, a faixa
de sistemas a −43 e os botões a −49 — ou seja, os botões pedem um olhar para
baixo, como num cockpit de verdade. Os números estão em
`scripts/cockpit/cockpit_interior.gd`.

---

## FLIGHT — o mostrador primário de voo

**Não tem horizonte artificial**, e a razão é simples: um horizonte é a linha
onde o chão encontra o céu, e em órbita não há nem um nem outro.

O **centro é o nariz**. Cada direção é levada ao referencial do corpo e o ângulo
entre ela e o nariz vira um raio — projeção azimutal equidistante, em que a
distância ao centro **é** o erro de apontamento em graus. Os anéis estão a 30, 60
e 90 graus.

| marcador | cor | o que é |
|---|---|---|
| ⊕ com cauda | verde | prógrado — ao longo da velocidade relativa ao corpo de referência |
| ⊗ | verde | retrógrado |
| ▲ / ▼ | ciano | normal / anti-normal (o polo da órbita) |
| ○ com haste | ciano escuro | radial para fora / para dentro |
| ✛ | violeta | o alvo |
| barra com haste | branco | o nariz, sempre no centro |

Os marcadores vêm de `PointingController::direction_for` — a **mesma** rotina
pela qual o piloto automático guia. Alinhar o nariz com o marcador e o autopilot
levaria a nave ao mesmo sítio, por construção e não por coincidência.

Direções a mais de 90 graus do nariz são desenhadas na borda, esmaecidas, em vez
de descartadas: "está atrás de você" é informação.

Quando há um modo de apontamento armado, uma linha âmbar liga o nariz ao alvo
dele — o diretor de voo. Siga a linha e o erro fecha.

## NAVIGATION — a órbita

Os pontos vêm de `get_orbit_track()`, que amostra
`trajectory::state_from_elements` sobre os elementos osculadores do snapshot. O
plano do desenho é o **plano da própria órbita**, orientado pelo periapsis — uma
projeção fixa esconderia a excentricidade de uma órbita inclinada atrás de um
escorço, que é exatamente o número que se quer ler.

`AP` e `PE` marcam onde; os números ao lado deles vêm do snapshot e são exatos.
A seta na nave aponta no sentido da marcha.

## TARGET — o alvo

Distância, velocidade relativa e **fechamento com sinal** — `+` a aproximar, `−`
a afastar. O círculo à direita mostra onde o alvo está em relação ao nariz, na
mesma convenção de tela do mostrador de voo.

⚠️ `CLOSE IN ... (linear)` é uma extrapolação: distância a dividir pela taxa a
que ela cai, **ignorando a gravidade inteira**. Está rotulada porque não é uma
previsão de chegada. Com um plano armado, a linha passa a mostrar a chegada do
**planejador**, que é uma trajetória de verdade.

## SYSTEMS — a faixa larga

Seis células: motor, empuxo, propelente, massa, RCS, relatividade.

A barra do acelerador é o **comando**; `THRUST` é o que o core está de facto a
produzir. Os dois podem divergir — tanque vazio, motor desarmado — e é
exatamente aí que ver os dois importa.

As doze lâmpadas de RCS acendem pelo **acionamento**, não pela tecla: elas vêm de
`RcsForce::throttles`, que é a mesma rotina que o modelo de forças voa. Um
comando puro em um eixo abre dois bicos; um comando diagonal abre quatro, a
frações diferentes. É isso que se vê.

A célula de relatividade tem duas linhas e não mais: `β`, `γ−1`, a diferença
entre o tempo coordenado e o próprio, e a aceleração própria. A física está lá e
é real — a 7,7 km/s isso é `1,0e-4`, `5,1e-9`, dezenas de femtossegundos e zero —
e ocupar metade do cockpit com ela em regime convencional seria uma afirmação
falsa sobre a sua importância. Os valores com todos os dígitos estão em `F3`.

⚠️ A aceleração ali é a **própria**: empuxo sobre massa, ou seja, só as forças
não gravitacionais — o que um acelerômetro a bordo leria. Em queda livre ela é
zero. **Não** é a aceleração coordenada do modelo de forças, que numa órbita de
400 km vale 8,7 m/s² porque inclui a gravidade; mostrar esse número como
aceleração própria diria que a tripulação sente quase um g, que é o contrário do
que acontece.

---

## As lâmpadas

| | acende quando |
|---|---|
| `MSTR` | propelente abaixo de 10 % ou rotação acima de 6 °/s |
| `RCS` | há pelo menos um bico aberto |
| `ENG` | o motor principal está a produzir empuxo |
| `AP` | há um plano armado a pilotar a nave |

## Os botões

Clicáveis com o mouse, e cada um chama **o mesmo caminho** que a tecla
correspondente — não há um segundo comando escondido atrás do botão.

`ENGINE` acelerador cheio / corte · `RCS` liga e desliga · `AP` aponta prógrado ·
`NAV` computador de missão · `MAP` mapa orbital · `WARP` sobe o time warp ·
`MODE` alterna IMPULSO / CRUZEIRO.

---

## As câmeras

| modo | referencial | para quê |
|---|---|---|
| `COCKPIT` | a cabeça, no casco | pilotar |
| `EXTERNAL` | os eixos do casco | olhar a nave — azimute 180° é atrás da cauda |
| `CHASE` | a velocidade | ver para onde se vai |
| `VELOCITY` | a velocidade | olhar o céu; azimute 180° olha para dentro do cone de aberração |
| `TARGET` | a linha do alvo | ver o alvo crescer |

Girar a câmera **não é manobra**: a atitude continua onde o RCS a deixou e nada
no estado muda.

## O fluxo de missão

```
NAV  →  escolher alvo  →  PLAN TRANSFER  →  resumo  →  EXECUTE
                                              ↓
                                           CANCEL
```

**`PLAN TRANSFER` trava o quadro por cerca de um segundo**, e isso é de
propósito: planejar é procurar oportunidades de partida e depois inverter o
modelo completo duas vezes, dezenas de propagações. É uma operação de missão, não
de quadro. O botão muda para `PLANNING…` antes de bloquear, para que a pausa
tenha um motivo visível.

Planejar **não arma**. O plano fica à vista com Δv, tempo de voo, propelente
requerido e a órbita prevista; `EXECUTE` instala as queimas no executor e
`CANCEL` descarta-as. A lista de candidatos que a busca de facto voou aparece
abaixo do resumo — o planejador não mira uma inclinação, e essa lista é como ele
diz isso.

Depois de armado, a fase aparece no canto superior direito com o tempo até o
próximo evento. As fases vêm de `core/navigation/mission_execution.hpp`; a
interface imprime a classificação, não a faz.

`ABORT` (`Shift+K`) cancela o plano e devolve a atitude ao piloto. A nave **não**
volta magicamente para a Terra: ela fica no estado físico em que está.

---

## Os três HUDs

| ` (crase) alterna | mostra |
|---|---|
| `cockpit` | só os cantos: warp, alvo, fase da missão. O painel mostra o resto |
| `minimal` | a faixa de baixo também — velocidade, altitude, AP/PE, acelerador, propelente |
| `off` | nada |

`F3` sobrepõe o **HUD técnico**: a leitura completa do Milestone 5, com todos os
dígitos, o tempo próprio, a diferença de relógios, os diagnósticos do céu e a
tabela previsto-contra-realizado. É essa leitura que a verificação sem tela
imprime, e é contra ela que as tolerâncias são conferidas.
