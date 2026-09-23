# Os instrumentos

Quatro mostradores, um por função. A regra que vale para os quatro: **nada aqui é
calculado pela interface.** Cada valor saiu do núcleo científico; o cockpit
escolhe a unidade e desenha.

## `FLIGHT` — para onde o nariz aponta

Não tem horizonte artificial, e a razão é simples: um horizonte é a linha onde o
chão encontra o céu, e em órbita não há nem um nem outro.

**O centro é o nariz.** Cada direção é levada ao referencial da nave e o ângulo
entre ela e o nariz vira um raio — projeção azimutal equidistante, em que a
distância ao centro **é** o erro de apontamento em graus. Os anéis estão a 30, 60
e 90 graus.

| marcador | cor | o que é |
|---|---|---|
| círculo com três hastes | verde | prógrado, ao longo da velocidade |
| círculo com um X | verde | retrógrado |
| triângulo para cima / para baixo | ciano | normal / anti-normal, o polo da órbita |
| círculo com haste | ciano escuro | radial para fora / para dentro |
| círculo com quatro hastes | violeta | o alvo |
| X de quatro hastes | violeta | o anti-alvo, a direção oposta ao alvo |
| barra com haste | branco | o nariz, sempre no centro |

Os marcadores vêm da **mesma rotina pela qual o piloto automático guia**.
Alinhar o nariz com um marcador leva a nave ao mesmo lugar que o autopilot
levaria, por construção e não por coincidência.

Direções a mais de 90 graus do nariz são desenhadas na borda, esmaecidas, em vez
de descartadas: "está atrás de você" é informação.

Com um modo de apontamento armado, uma linha âmbar liga o nariz ao alvo dele, e
o canto inferior direito mostra o modo e o erro em graus — âmbar acima de 2°,
verde abaixo. Siga a linha e o erro fecha.

No canto inferior esquerdo, `SPIN` é a velocidade de rotação da nave, em graus
por segundo; fica âmbar acima de 2 °/s. No alto, à direita, a velocidade.

Todas as direções são relativas ao **corpo de referência** — o que aparece em
`NAVIGATION` como `REFERENCE`. Em órbita da Lua, prógrado é o prógrado em torno
da Lua, e é para lá que o apontamento automático leva o nariz.

## `NAVIGATION` — a órbita

A elipse é desenhada a partir de pontos que vêm do núcleo. O plano do desenho é o
**plano da própria órbita**, orientado pelo periapsis — uma projeção fixa
esconderia a excentricidade de uma órbita inclinada atrás de um escorço, que é
exatamente o número que se quer ler.

`AP` e `PE` marcam **onde**; os números ao lado deles são exatos. A seta na nave
aponta no sentido da marcha.

## `TARGET` — o alvo

Distância, velocidade relativa e **fechamento com sinal**: `+` aproximando, `−`
afastando. O círculo à direita mostra onde o alvo está em relação ao nariz, na
mesma convenção de tela do mostrador de voo.

> `CLOSE IN ... (linear)` é uma extrapolação: a distância dividida pela taxa a que
> ela cai, **ignorando a gravidade inteira**. Quando a distância está a crescer, a
> linha diz `NO INTERCEPT -- opening`. Está rotulada porque não é uma
> previsão de chegada. Com um plano armado, a linha passa a mostrar a chegada do
> planejador, que é uma trajetória de verdade.

## `SYSTEMS` — a faixa larga

Seis células: motor, empuxo, propelente, massa, RCS, relatividade.

A barra do acelerador é o **comando**; `THRUST` é o que o motor está de fato
produzindo. Os dois podem divergir — com o tanque vazio a barra sobe e o empuxo
fica em zero — e é exatamente aí que ver os dois importa. Ao lado da barra, o
estado: `RUNNING` com empuxo, `ARMED` sem. Com o motor aceso, a barra e o empuxo
tomam a cor do modo: rosa em `IMPULSE`, azul em `CRUISE` (ver *A nave*).

As doze lâmpadas de RCS acendem pelo **acionamento**, não pela tecla. Um comando
puro em um eixo abre dois propulsores; um comando diagonal abre quatro, em
frações diferentes. É isso que se vê, e é isso que está queimando propelente.

A célula de relatividade tem `β`, `γ−1`, a diferença entre o relógio coordenado e
o próprio, e a aceleração **própria** — empuxo sobre massa, ou seja, só as forças
não gravitacionais, que é o que um acelerômetro a bordo leria. Em queda livre ela
é zero. A 7,7 km/s isso dá `1,0e-4`, `5,1e-9`, dezenas de femtossegundos e zero:
a física está lá, é real, e é pequena. Os valores com todos os dígitos estão em
`{{key:debug_hud}}`.
