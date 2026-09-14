# A missão Terra–Lua

O roteiro completo, do estacionamento em órbita terrestre à órbita lunar.

```
{{key:nav_panel}}        abre o computador
    ↓
escolher o alvo          ◀ ▶, ou {{key:target_prev}} / {{key:target_next}}
    ↓
PLAN TRANSFER            o quadro para por cerca de um segundo
    ↓
ler o resumo             Δv por queima, propelente, órbita prevista
    ↓
EXECUTE                  arma as queimas
    ↓
{{key:warp_up}}          e deixar o warp levar os quase cinco dias
    ↓
órbita lunar
```

## 1. Escolher o alvo

![O computador recém-aberto: o alvo já é a Lua, e ainda não há plano](../validation/m7/moon-target.png)

O alvo já é a Lua num voo novo. ◀ e ▶ percorrem os corpos que são **lugares** —
baricentros não aparecem, porque um baricentro é um ponto no vazio e não um
destino.

## 2. Planejar

`PLAN TRANSFER` **trava o quadro por cerca de um segundo**, e isso é de propósito.
Planejar é procurar oportunidades de partida e depois inverter o modelo completo
duas vezes, o que são dezenas de propagações de trajetória. É uma operação de
missão, não de quadro. O botão muda para `PLANNING…` antes de bloquear, para que
a pausa tenha um motivo visível.

![O resumo do plano](../validation/m7/mission-plan.png)

O que o resumo diz:

| | |
|---|---|
| `DEPARTURE` | quanto falta para a ignição |
| `ARRIVAL` | quanto falta para a chegada |
| `FLIGHT TIME` | quantos dias de voo, e qual ramo |
| `INJECTION` | o Δv e a duração da queima de partida |
| `MIDCOURSE` | a correção, embutida na injeção e não uma queima separada |
| `CAPTURE` | o Δv e a duração da queima de captura |
| `TOTAL ΔV` | a soma, contra o que a nave tem |
| `PROPELLANT` | quanto custa, e quanto sobra |
| `PREDICTED ORBIT` | periapsis, apoapsis, excentricidade, inclinação, RAAN |

Abaixo do resumo aparece a lista de **candidatos que a busca de fato voou** —
cada geometria de partida que ela tentou, com a órbita em que teria chegado. Ela
está ali porque o planejador **não mira uma inclinação**: a inclinação em que
você acaba é consequência da geometria de partida, e a dispersão da lista é como
o planejador diz isso em vez de esconder.

> O tempo de voo **não** é um parâmetro. Ele é o que a busca decide. Fixá-lo é
> precisamente o defeito que fez uma campanha anterior falhar em 82 % das suas
> épocas: com o ponto de partida e o tempo de voo ambos presos, o ângulo de
> transferência é o que o calendário disser.

## 3. Inspecionar

`CANCEL` descarta. Planejar **não arma**: o plano fica à vista até você mandar
executá-lo.

`{{key:orbit_map}}` mostra a transferência no mapa antes de você decidir.

![O arco translunar, com os marcadores de queima](../validation/m7/transfer-map.png)

## 4. Executar

`EXECUTE` — ou `{{key:mission_execute}}` — instala as queimas. A partir daí o plano
pilota: a nave se orienta sozinha para a injeção, o motor acende na hora, e a
fase aparece no canto superior direito com a contagem para o próximo evento.

```
AWAITING DEPARTURE → ORIENTING FOR INJECTION → INJECTION BURN
→ COAST PHASE → TARGET APPROACH → ORIENTING FOR CAPTURE
→ CAPTURE BURN → ORBIT INSERTION → ORBIT ACHIEVED
```

## 5. A travessia

`{{key:warp_up}}` até 100 000× e a viagem leva alguns minutos de relógio de parede.
`{{key:camera_cycle}}` até `TARGET` para ver a Lua crescer.

![A aproximação, com a contagem para a inserção](../validation/m7/lunar-approach.png)

## 6. A chegada

![A Lua, no terminador, onde o relevo se vê](../validation/m7/moon-whole-disc.png)

A queima de captura acontece sozinha. Quando ela termina, `{{key:debug_hud}}`
confirma:

```
about Moon      CAPTURED   1838,2 km at 1632,7 m/s
  orbit        96,8 x 103,1 km de altitude, e 0,0017, i 19,5 deg, 117,8 min
```

![Em órbita lunar](../validation/m7/lunar-orbit-external.png)

O mostrador técnico também mostra uma tabela de **previsto contra realizado**:
periapsis, apoapsis, excentricidade, inclinação, propelente e Δv de captura, com
a diferença entre o que o planejador disse e o que a integração fez. É a linha
que diz se o simulador prevê a própria física.

## Abortar

`{{key:mission_abort}}` cancela o plano e devolve a atitude ao piloto. A nave
**não** volta magicamente para a Terra: ela fica exatamente no estado físico em
que está, com a velocidade que tem, e o controle é seu.
