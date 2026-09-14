# Terra → Marte: o cenário, e o que ele mede

> Milestone 8, regras 81 e 82.
>
> Um cenário reproduzível, os números que ele produz, e a comparação entre o que
> o planejamento previu e o que a propagação entregou.

---

## 1. O cenário

```bash
./build/bin/orbit-cli mission \
    --from Earth --to Mars \
    --target-orbit-km 500 \
    --date 2026-01-01
```

| | |
|---|---|
| época | 2026-01-01T00:00:00 UTC |
| órbita de estacionamento | 400 km circular, 51,6° |
| órbita desejada | 500 × 500 km em torno de Marte |
| nave | Torch Mk III, 1 t seca + 19 t de propelente, modo IMPULSE (w = 0,03 c) |
| modelo de execução | `FINITE_BURN` |
| objetivo | `BALANCED` |
| efemérides | DE440 (`de440s.bsp`) + `mar099s.bsp` |

O mesmo pedido, pela mesma API pública, é o que a cena faz quando o piloto carrega
em `SEARCH`. Não há um segundo algoritmo (regra 106).

---

## 2. O resultado

```text
status         PLANNED                 (64,3 s de busca)

departure      2026-01-01T00:52:10
arrival        2026-07-24T10:40:15
flight time    204,41 dias             (ramo prógrado, ângulo de transferência 51,16°)

injection dv   16 863,98 m/s  em 1684,82 s
midcourse      3 514,43 m/s   (dobrado na injeção pelo corretor)
capture dv      7 516,43 m/s  em  735,04 s
trim                ~15 s
total dv       24 380,41 m/s  de 26 942 938 disponíveis

propellant     54,14 kg necessários, 18 945,9 kg restantes
v_infinity      9 547,18 m/s na chegada
flyby rp        4 194,14 km  do centro de Marte

arrival orbit  496,12 × 501,50 km, e = 0,00069, i = 20,68°
requested      500 × 500 km
```

### Custo da busca

```text
768 geometrias consideradas
 24 sobreviveram ao crivo de dois corpos
  6 voadas
313 propagações
2 406 893 passos de integrador
64,3 s
```

Uma candidata isolada — `--tof-days 204.41 --screened 1 --flown 1` — planeja em
**5,6 s**.

---

## 3. As alternativas

```text
geometria                       ok   tof [d]   inject     capture    total      órbita / motivo
prograde/204.41d/coast=0.957h   sim   204,41   18441,54    7516,57   25958,11   499,2 × 504,6 km
prograde/204.41d/coast=0.870h   sim   204,41   16863,98    7516,43   24380,41   496,1 × 501,5 km
prograde/181.12d/coast=0.957h   não   181,12   18551,37    9789,68   28341,04   PERIAPSIS_TOO_LOW
prograde/181.12d/coast=0.870h   não   181,12   16933,22    9789,95   26723,17   PERIAPSIS_TOO_LOW
prograde/227.70d/coast=0.957h   não   227,70   19885,75   11355,19   31240,94   PERIAPSIS_TOO_LOW
prograde/227.70d/coast=0.783h   não   227,70   18390,83   11354,45   29745,28   PERIAPSIS_TOO_LOW
```

Duas de seis. As quatro recusadas são todas o mesmo mecanismo: o laço de mira
esgotou os três passes sem trazer o periapsis da elipse de captura para dentro da
banda pedida. **Registrado como `IMPROVEMENT`, não como bloqueio** — a regra 42
pede pelo menos três boas alternativas *quando disponíveis*, e aumentar
`aim_passes` ou partir do desvio medido no primeiro passe em vez de zero é o
caminho evidente.

---

## 4. A comparação que interessa (regra 82)

O plano é uma previsão. A propagação completa, com gravidade multibody, J2 da
Terra e queimas finitas, é o que de facto acontece. A tabela abaixo é a diferença
entre as duas, medida **na cena**, depois de 204 dias de voo e das três queimas:

| grandeza | previsto | voado | diferença |
|---|---|---|---|
| periapsis | 497 830,09 m | 497 838,90 m | **8,8 m** |
| apoapsis | 503 210,43 m | 503 226,69 m | **16,3 m** |
| excentricidade | 0,00069155 | 0,00069251 | 9,6 × 10⁻⁷ |
| inclinação | 20,67974° | 20,68010° | 0,00036° |
| propelente | 54,1515 kg | 54,1519 kg | **0,37 g** |
| Δv de captura | 7516,580 m/s | 7516,684 m/s | 0,10 m/s |
| época de chegada | — | — | −328 315 s |

Oito metros e oitenta de periapsis, depois de duzentos e quatro dias.

Isso não é sorte e não é uma coincidência de tolerâncias: é o que significa
**inverter o modelo em vez de prever com ele**. O corretor diferencial ajusta a
velocidade de partida até que o modelo COMPLETO chegue onde se pediu, e as duas
queimas de captura são resolvidas contra a trajetória VOADA. O plano não descreve
uma trajetória parecida com a que a nave voa; ele descreve aquela.

### A época de chegada, que difere em 3,8 dias

Essa é a única linha com uma diferença grande, e ela não é um erro de trajetória.

`MissionExecution` guarda a aproximação mais próxima como um mínimo corrente
sobre as amostras que lhe chegam, e nesta corrida o warp estava a **1e7×**: cada
quadro avança 0,87 dia de tempo simulado. A aproximação foi resolvida com essa
granularidade, e 328 315 s são quatro amostras.

É uma limitação da **medição sob warp alto**, não da trajetória — o periapsis
voado, que é lido da órbita depois de ela se estabilizar, confere a oito metros.
A mesma corrida a warp baixo resolveria a época; ninguém voa duzentos dias a warp
baixo.

Registrado como `IMPROVEMENT`: a aproximação mais próxima podia ser refinada por
secção áurea sobre o interpolante denso, como o planejador já faz, em vez de ser
lida das amostras que o quadro calhou de produzir.

---

## 5. Terra → Lua, pelo mesmo código

A prova de que a generalização não custou nada ao caso qualificado:

```bash
./build/bin/orbit-cli mission --from Earth --to Moon --target-orbit-km 100 --date 2026-01-01
```

```text
flight time    4,75 dias      (prógrado, ângulo 151,73°)
injection dv   5726,3582 m/s
capture dv      830,68297 m/s
total dv       6557,0411 m/s
arrival orbit  96,901 × 103,184 km, e = 0,00170975, i = 19,514°
```

Idêntico, **dígito a dígito**, ao que o mesmo comando produzia antes do Milestone
8. O solver de captura nunca aparece no relatório porque nunca é chamado: o
primeiro voo da queima impulsiva já cai dentro da órbita pedida, e tudo abaixo
desse teste fica desligado.

E na cena, sem tela:

```bash
SPACEFLIGHT_HEADLESS_MISSION=1 ./scripts/run_godot_headless.sh 2500
```

```text
plan: Earth -> Moon, 4d 12h, 6661 m/s total
ARRIVED
reference      Moon
orbit          96.9 x 103.3 km altitude, e 0.0018, i 19.50 deg, 117.8 min
```

O corpo de referência trocou sozinho de `Earth` para `Moon` na chegada.

---

## 6. Como reproduzir a corrida completa, sem tela

```bash
SPACEFLIGHT_HEADLESS_DESTINATION=Mars ./scripts/run_godot_headless.sh 15000
```

Quatro minutos e quinze de relógio de parede: cerca de um minuto de busca e o
resto de voo a 1e7×. A corrida planeja, arma, voa, captura e imprime a tabela
previsto-contra-voado da seção 4.

O orçamento de quadros importa. A busca leva tempo de **parede**, o voo leva
**quadros**, e uma corrida sem tela não tem taxa de quadros fixa: 6000 quadros
acabam antes de a busca acabar, 15000 chegam a Marte com folga.

---

## 7. O que este cenário não prova

- **Nada sobre outras datas.** Uma época, uma órbita de estacionamento, um
  objetivo. A campanha de 365 épocas que existe para a Lua não existe para Marte,
  e enquanto não existir o diretório de corpos chama Marte de `experimental` e
  não de `qualified`. Essa palavra está no código, não só aqui.
- **Nada sobre o piloto automático.** Esta trajetória é `FINITE_BURN`. Sob
  `AUTOPILOT` a captura interplanetária não está disponível — ver a limitação
  registrada em `docs/architecture/general-mission-planning.md` seção 6.
- **Nada sobre outros destinos.** Vênus e Mercúrio passam pelo mesmo caminho de
  código e ninguém os voou.
