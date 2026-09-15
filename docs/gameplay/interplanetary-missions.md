# Missões interplanetárias

> Como voar da órbita terrestre até uma órbita marciana. Milestone 8.

---

## O roteiro, em teclas

```text
Tab              abre o computador de bordo
◀ ▶  (TARGET)    escolhe o destino          -> MARS
◀ ▶  (ORBIT)     escolhe a órbita desejada  -> 500 km circular
SEARCH           começa a busca
                 (o jogo continua a andar; SEARCH vira CANCEL SEARCH)
Enter            executa o plano
M                mapa
Shift+M          alterna mapa local / sistema solar
.                sobe o time warp
```

Isso é tudo. O resto é o cruzeiro.

---

## A busca

`SEARCH` procura numa grade de 24 pontos de partida × 16 tempos de voo × 2 ramos
de Lambert — 768 geometrias. Ela roda **numa thread**: o cockpit continua a
responder, os instrumentos continuam a atualizar, e o painel mostra as contagens
enquanto elas sobem.

Uma busca Terra→Marte leva cerca de um minuto. `CANCEL SEARCH` para-a — entre
candidatas, nunca dentro de uma, então ela pode demorar um voo a parar de facto.

### O que chega de volta

```text
TRAJECTORIES FOUND

             FAST            BALANCED        LOW ΔV
FLIGHT       181d 03h        204d 10h        227d 17h
DEPART ΔV    18551 m/s       16864 m/s       19886 m/s
CAPTURE ΔV    9790 m/s        7516 m/s       11355 m/s
TOTAL ΔV     28341 m/s       24380 m/s       31241 m/s
ORBIT        ...             496 × 501 km    ...
INC          ...             20.7°           ...
```

Os rótulos `FAST` / `BALANCED` / `LOW ΔV` só aparecem quando há **três** opções
para nomear. Com duas, elas são `FASTER` e `CHEAPER`; com uma, `ONLY OPTION`.
Chamar uma trajetória de "equilibrada" quando não há nada com que a equilibrar
seria inventar uma comparação.

Abaixo delas vem a lista das **recusadas**, com o motivo de cada uma. Uma busca
que encontra pouco diz o que tentou.

### Escolher uma

Uma linha de botões `USE` por baixo da tabela, um por coluna. Escolher uma
replaneja **aquela** geometria — o ponto de partida, o tempo de voo e o ramo de
Lambert ficam fixados — em vez de procurar de novo.

Custa uma candidata em vez de 768: cerca de um segundo, não um minuto.

O crivo continua a correr. Uma geometria fixada que viole uma restrição rígida é
recusada na mesma; isto é uma escolha de pergunta, não uma maneira de contornar a
resposta. E as recusadas não têm botão: elas já foram voadas e já disseram o que
tinham a dizer.

Com **uma** opção só a linha fica vazia, porque não há escolha a oferecer.

---

## Não há janela de lançamento

A resposta de manual para "quando se vai a Marte" é: perto de uma oposição, a
cada 780 dias. Essa resposta pertence a um foguete químico.

Esta nave carrega um propulsor de fusão com **26 900 km/s** de orçamento no modo
IMPULSE. Partindo de 2026-01-01 — uma data francamente ruim, com a Terra e Marte
mal posicionados — as transferências custam entre 38 e 117 km/s de velocidade de
excesso. Todas pagáveis por três ordens de grandeza.

Então a busca não procura uma janela. Ela procura, dentro de **uma revolução da
órbita de estacionamento**, o ponto de onde a hipérbole de partida pode apontar
para onde a transferência precisa — que é uma pergunta com período de 92 minutos,
não de 780 dias.

O que muda com a data não é *se* dá para ir, e sim *quanto custa*. A tabela de
alternativas mostra isso.

---

## O cruzeiro

Duzentos dias. O time warp vai até 1e8×; a 1e7× um quadro são quase dois dias.

Durante o cruzeiro o cockpit muda de contexto sozinho:

```text
REFERENCE                 muda Terra -> Sol -> Marte conforme a nave sai de uma
                          vizinhança gravitacional e entra noutra
TARGET / DISTANCE         a distância até Marte, em milhões de km ou em UA
TARGET / REL SPEED        e a que velocidade ela fecha
TARGET / phase            COAST, APPROACH, CAPTURE ORIENTING, CAPTURE BURN...
ARRIVAL (PLANNED)         a contagem regressiva do plano, não uma extrapolação
```

⚠️ A troca de referência é **contexto de navegação e não física**. A gravidade
continua multibody em todos os instantes; nenhuma esfera de influência aparece na
dinâmica. O que muda é contra que corpo os números são lidos — porque `AP 402 km`
sem o corpo ao lado não é um número.

---

## O mapa do sistema solar

`Shift+M` alterna entre os dois mapas.

```text
LOCAL     centrado no corpo que a nave orbita
SYSTEM    centrado no Sol
```

São dois **referenciais**, não duas escalas. O mapa local ancora tudo no corpo
central; num cruzeiro de duzentos dias a Terra anda 500 milhões de quilômetros, e
um arco heliocêntrico desenhado em torno da Terra de hoje é uma trajetória que
nunca aconteceu.

O mapa do sistema mostra o Sol, os planetas, as órbitas deles amostradas da
efeméride, a nave, a transferência planejada e as queimas. O zoom é
**logarítmico**: o mapa cobre de 1e6 a 1e12 metros, e um passo linear que é
confortável perto da Terra atravessa a órbita de Netuno num clique.

Os anéis de escala são rotulados em **UA**. O canto diz qual é o referencial,
sempre.

---

## A chegada

A captura em Marte tem **três** queimas, e a Lua tem duas. A diferença é física e
vale a pena entender, porque ela aparece no mapa e na linha do tempo:

```text
INJECTION        16 864 m/s, 28 min   sai da órbita terrestre
INSERTION         7 516 m/s, 12 min   freia a hipérbole numa elipse
CIRCULARISATION     ~270 m/s, 15 s    baixa o apoapsis; fecha o círculo
```

A queima de captura dura doze minutos a 19 km/s de velocidade no periapsis: o
motor fica aceso ao longo de quatro raios marcianos. Isso não é um impulso, e uma
queima só não produz um círculo. A segunda, de quinze segundos, produz.

Uma captura lunar dura 83 segundos contra 1,6 km/s e é quase um impulso — por isso
ela continua com uma queima só, exatamente como no Milestone 7.

Detalhes em [`docs/architecture/general-mission-planning.md`](../architecture/general-mission-planning.md)
seção 6.

---

## Sem aerofrenagem

Marte tem atmosfera e este simulador não a modela. A captura é **exclusivamente
propulsiva** e o planejador impõe um piso de 50 km ao periapsis da elipse
intermediária — que é uma margem de segurança declarada, não um efeito físico:
nada na dinâmica impediria uma passagem a 10 km.

---

## Outros destinos

O computador oferece tudo o que o catálogo sabe colocar e desenhar. Nem tudo o
que aparece é uma missão:

```text
qualified      a Lua. Uma campanha de 365 épocas responde por ela.
experimental   Marte, Vênus, Mercúrio, Fobos, Deimos. O planejador tenta.
observation    Júpiter, Saturno, Urano, Netuno e as luas deles.
```

Os gigantes gasosos são desenhados na posição do **baricentro** do sistema deles,
porque os SPK dos próprios planetas têm centenas de megabytes e não são
distribuídos com o projeto. Isso basta para desenhar — o baricentro de um sistema
planetário fica dentro do planeta — e não basta para ser o centro de uma órbita
de captura. Por isso eles não são oferecidos como destino, em vez de serem
oferecidos e falharem.
