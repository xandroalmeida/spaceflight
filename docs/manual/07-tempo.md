# O tempo

Uma travessia Terra–Lua leva quase cinco dias. O time warp é como você os vê
passar.

| | |
|---|---|
| `{{key:warp_up}}` / `{{key:warp_down}}` | sobe e desce a escada |
| `{{key:pause}}` | pausa |

A escada vai de 1× a 100 000 000×, em passos de dez. Toda mudança aparece na tela
— o warp nunca muda em silêncio.

```
1×   10×   100×   1 000×   10 000×
100 000×   1 000 000×   10 000 000×   100 000 000×
```

## O que o warp faz, e o que não faz

O warp decide **quanto tempo coordenado um quadro pede**. Ele nunca decide **como**
o integrador chega lá: o passo continua sendo escolhido pelo controle de erro do
propagador. Warpar não degrada a trajetória.

Uma queima planejada é integrada corretamente mesmo que um quadro inteiro caiba
dentro dela: a execução parte o quadro nas épocas de ignição e de corte, de modo
que o motor nunca acende no meio de um passo.

## Pausar

Pausar para o **tempo da simulação**, e só ele. A câmera continua respondendo, a
interface continua viva, e nada no motor gráfico é congelado — o que tem de parar
é o relógio da nave, não o programa.

## Dois relógios

O mostrador técnico (`{{key:debug_hud}}`) mostra os dois:

```
elapsed        tempo coordenado desde a época do cenário
proper time    o relógio da nave
clock diff     a diferença
```

A 7,7 km/s a diferença são dezenas de femtossegundos por minuto. Ela existe, é
calculada a cada passo da integração, e é desprezível — como tem de ser nessa
velocidade. Numa queima de cruzeiro a `β = 0,9` ela deixa de ser.
