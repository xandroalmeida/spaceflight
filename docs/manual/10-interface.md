# A interface

## Os três HUDs

`{{key:hud_cycle}}` alterna.

| modo | o que aparece |
|---|---|
| `cockpit` | só os cantos: warp, alvo, fase da missão. O painel mostra o resto |
| `minimal` | mais a faixa de baixo: velocidade, altitude, apoapsis, periapsis, acelerador, propelente |
| `off` | nada |

No cockpit a faixa de baixo não é desenhada, e isso não é economia: o painel já
mostra esses números, em instrumentos, e repeti-los numa tarja por cima deles
taparia exatamente a parte da tela onde eles estão.

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
842 m        18,4 km      384 000 km     0,0026 AU
842,0 m/s    7,680 km/s   0,00012 c
1,5 min      2,75 h       4,75 d         8,102 yr
T-01:42:17
```

Você nunca vê `384000000.000 m` — exceto no mostrador técnico, onde ver o número
cru é o objetivo.

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
| avisos | tom alternado |

**Na câmera externa a nave fica em silêncio.** Nada atravessa o espaço: não há som
de motor "de fora", não há explosão, não há passagem. A interface continua
soando, porque ela não está no espaço — está no seu monitor.

Os sete arquivos de áudio atuais são substitutos sintetizados: ruído filtrado e
envelopes, não gravações. Estão marcados como tal e podem ser trocados um a um.
