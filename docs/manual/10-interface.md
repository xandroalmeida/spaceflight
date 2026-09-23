# A interface

## O HUD

`{{key:hud_cycle}}` alterna entre `COCKPIT`, `MINIMAL` e `OFF`. Hoje os dois
primeiros desenham a mesma coisa — o HUD ligado —, e `OFF` o apaga. O que o HUD
mostra depende de **onde a câmera está**:

| câmera | o que aparece |
|---|---|
| no cockpit | só os cantos: warp e corpo de referência, câmera, alvo, fase da missão. O painel mostra o resto |
| fora dele | mais a faixa de baixo: velocidade, altitude, apoapsis, periapsis, acelerador, propelente |

No cockpit a faixa de baixo não é desenhada, e isso não é economia: o painel já
mostra esses números, em instrumentos, e repeti-los numa tarja por cima deles
taparia exatamente a parte da tela onde eles estão. Com o mapa orbital aberto o
HUD se recolhe.

## O mostrador técnico

`{{key:debug_hud}}` sobrepõe a leitura completa: tempo coordenado e próprio, a
diferença entre eles, os elementos orbitais com todos os dígitos, o fluxo de
massa, os diagnósticos do céu, a taxa de quadros, e a tabela de previsto contra
realizado.

É essa leitura que a verificação sem tela imprime, e é contra ela que as
tolerâncias do projeto são conferidas. Ela existe para ser **comparada com um
número**, não para ser pilotada.

## As unidades

O cockpit escolhe a unidade sozinho.

```
842 m        18.4 km      384 000 km     0.0026 AU
842.0 m/s    7.680 km/s   0.00012 c
42.0 s       12.5 min     2h 45m         4d 18h        8y 37d
T-01:42:17   T-4d 06:12:40
```

O separador decimal é o ponto, como no resto do painel. Você nunca vê
`384000000.000 m` — exceto no mostrador técnico, onde ver o número cru é o
objetivo.

## Pausa e ajustes

`{{key:menu}}` abre o menu. Se houver um painel aberto, `{{key:menu}}` fecha esse
painel primeiro.

| | |
|---|---|
| `RESUME` | volta |
| `CONTROLS` | a mesma ajuda de `{{key:help}}` |
| `QUIT` | sai |

E quatro ajustes: sensibilidade do mouse, volume geral, volume de efeitos, escala
da interface.

## O som

Mesmo no vácuo, sons estruturais existem: o motor e os propulsores estão presos
ao mesmo casco em que você está sentado, e o casco conduz.

| | |
|---|---|
| ventilação | o fundo constante da cabine |
| motor | o volume acompanha o **empuxo real**, não o acelerador |
| RCS | um golpe por disparo |
| botões e interruptores | clique |
| computador de bordo | um aviso curto a cada plano pronto, plano aceito e mudança de fase |
| avisos | tom alternado |

**Na câmera externa a nave fica em silêncio.** Nada atravessa o espaço: não há som
de motor "de fora", não há explosão, não há passagem. A interface continua
soando, porque ela não está no espaço — está no seu monitor. Os sons da interface
também seguem o volume de efeitos.

Os sete arquivos de áudio atuais são substitutos sintetizados: ruído filtrado e
envelopes, não gravações. Estão marcados como tal e podem ser trocados um a um.
