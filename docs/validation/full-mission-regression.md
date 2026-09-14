# Regressão de missão completa

**Milestone 6.2, seção 18.**

`tests/scientific/test_full_mission.cpp`, rótulo `scientific`, roda em
`ctest-headless`.

---

## 1. O cenário dourado

```text
Terra, órbita de estacionamento circular de 400 km
    no plano orbital da Lua (i = 28,3° nesta época)
    ponto de partida 170° de onde a Lua estará

    →  transferência trans-lunar
    →  órbita lunar de 100 km
```

Época: `2026-01-01 00:00:00 TDB`. Nave: `Tug`, 1 t seca + 19 t de propelente,
Fusion Torch Mk III em modo IMPULSE (w = 0,03 c, 200 kN).

É o **mesmo estado** que a campanha de 365 épocas voa, deliberadamente: uma
falha aqui e uma falha na campanha têm de ser a mesma falha, não duas
investigações.

---

## 2. O que "completa" quer dizer

Tudo no laço ao mesmo tempo:

| componente | no teste |
|---|---|
| busca de partida | grade completa de 10 tempos de voo × 16 amostras × 2 ramos |
| corretor de posição | `departure_targeting`, tolerância 1e7 m |
| corretor de plano B | 4 passadas, tolerância derivada de dr_p/db |
| queimas | **finitas**, massa variável, através do executor |
| guiagem | **autopiloto** — o motor aponta para onde o casco aponta |
| atitude | quatérnio integrado, controlador PD, 12 propulsores RCS |
| RCS | saem do mesmo tanque, pelo mesmo F = η·q·w |
| gravidade | N corpos do catálogo padrão + J2 da Terra |
| duração | da órbita de estacionamento até duas revoluções após o corte |

Isto **não é** um teste unitário e não pretende ser. Todo o resto da suíte
isola um mecanismo e consegue dizer exatamente o que quebrou; este só consegue
dizer que a missão parou de funcionar — uma afirmação diferente e igualmente
necessária. Uma suíte de unidades verdes com um simulador que não chega à Lua é
precisamente o modo de falha que ele existe para pegar.

---

## 3. Os critérios

Os da §18, usados como escritos:

```text
CAPTURED                                   ε < 0 sobre a Lua
80 km <= periapsis altitude <= 120 km
80 km <= apoapsis  altitude <= 120 km
eccentricity <= 0.01
fuel >= 0
no collision
no NaN
no superluminal state
```

Mais três que a §18 implica e não nomeia:

* as duas queimas existem, têm duração positiva e **não se sobrepõem** — um
  motor não queima em duas direções (o `ManeuverPlan` já recusa a sobreposição,
  mas a exigência fica escrita onde um leitor a procura);
* a massa nunca cai abaixo da massa seca em nenhum ponto do arco;
* `propellant_required + propellant_remaining <= tanque inicial`.

### Sobre "sem colisão" e "sem NaN"

Verificados **no arco voado**, amostrado em 2000 pontos, e não nos extremos. Um
teste de extremos aprovaria uma trajetória que atravessou a Terra e saiu do outro
lado — que é exatamente o modo de falha que o Milestone 6 entregou: 52 épocas em
100 terminavam com `trajectory entered Earth` depois de um estágio de plano B
reportar `converged`.

### Sobre "sem estado superluminal"

A velocidade máxima da nave relativa à Terra ao longo de todo o arco, comparada
com *c*. Uma injeção trans-lunar chega perto de 11 km/s, então há cinco ordens de
grandeza de folga — e isso é o ponto. Não é um teste sobre a missão; é um teste
de que o integrador nunca produziu um estado que não é física.

---

## 4. Por que os limites são largos

A campanha entrega, sobre 365 épocas, capturas em torno de **96,4 × 102,8 km com
e = 0,0017**. A banda de 80–120 km tem, portanto, muita folga: este teste
continuaria passando com a missão consideravelmente degradada.

Isso é intencional. Um **limite de aceitação** responde "a missão ainda
funciona?"; um **pino de regressão** responde "algum número mudou?". São
perguntas diferentes e este arquivo faz a primeira. A segunda é feita pela
campanha, cujo CSV tem quarenta colunas por época e onde uma mudança de 4 km no
periapsis aparece.

O que o teste faz além de aprovar: **imprime as medidas**. A margem real de cada
critério fica no log em vez de ser inferida do fato de ter passado — e é de lá
que virá a evidência quando os limites forem apertados.

---

## 5. O segundo teste: predito × realizado como mecanismo

`the_execution_monitor_records_predicted_against_actual`.

Arma um plano em `navigation::MissionExecution`, percorre o arco gravado e deixa
o monitor classificar cada instante. Exige que ele **visite** as fases que o
plano contém:

```text
PLANNED → ... → INJECTION_BURN → COAST → APPROACH → CAPTURE_BURN
        → ORBIT_INSERTION → COMPLETE
```

`ORIENTING` e `CAPTURE_ORIENTING` não estão entre elas neste teste, e por uma
razão declarada: ele roda em `FINITE_BURN`, onde a lei de guiagem *é* a direção
do empuxo e o erro de apontamento é zero por construção. Fingir que passaram
seria pior do que não visitá-las.

E depois exige que o monitor consiga **dizer** o que previu contra o que
aconteceu. Como o voo é o mesmo que o planejador voou, isto é um teste da
**contabilidade**, não da física — o escopo certo: se a previsão bate com a
realidade é pergunta da campanha; se o simulador sequer consegue lhe dizer é
pergunta desta.

A diferença aceita entre as duas leituras é de 200 m no periapsis, e a origem é
declarada: o planejador lê os elementos num passo aceito e o monitor num ponto
interpolado da saída densa. Com 4000 amostras sobre um arco de 5 dias o
espaçamento é ~110 s, sobre uma órbita lunar de 2 h.
