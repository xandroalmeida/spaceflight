# Milestone 8 — playtest manual

> Regra 110. A lista que uma pessoa segue, com o teclado, numa tela.
>
> O que está aqui não é verificável por teste automático: o que os testes
> conseguem dizer é que o número existe e tem a ordem de grandeza certa. Se a
> viagem *se joga* é uma pergunta diferente, e é esta.

---

## Preparação

```bash
./scripts/fetch_kernels.sh          # precisa do mar099s.bsp
cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build-godot --target spaceflight_gdextension -j
external/godot/Godot.app/Contents/MacOS/Godot --path godot/project
```

Sem `mar099s.bsp` Marte não aparece na lista de destinos. Isso é correto e
deliberado (o corpo 499 não existe para o SPICE sem ele), mas se o objetivo é
testar o M8 e Marte não está lá, é o kernel que falta.

---

## O roteiro

### Partida

- [ ] o jogo abre em órbita terrestre, com a Terra na janela
- [ ] `Tab` abre o computador de bordo
- [ ] `◀ ▶` na linha `TARGET` percorre os corpos, e **MARS** está entre eles
- [ ] escolher `MARS` muda o mostrador `TARGET` do cockpit: a distância salta de
      centenas de milhares de km para centenas de **milhões**
- [ ] a linha `TARGET ORBIT` muda sozinha para `500 km circular` ao escolher
      Marte, e para `100 km circular` ao voltar para a Lua
- [ ] `◀ ▶` na linha `TARGET ORBIT` percorre as altitudes

### A busca

- [ ] `SEARCH` começa a busca e o botão vira `CANCEL SEARCH`
- [ ] **o jogo continua a andar**: a nave continua em órbita, os instrumentos
      continuam a atualizar, a câmera responde
- [ ] o painel mostra contagens que sobem: `candidates tested N of 768`
- [ ] `CANCEL SEARCH` interrompe e o painel diz `SEARCH CANCELLED`
- [ ] uma segunda busca, deixada correr até ao fim, apresenta o resumo

### As alternativas

- [ ] o resumo traz `DEPARTURE`, `ARRIVAL`, `FLIGHT TIME`, `INJECTION`,
      `CAPTURE`, `TOTAL ΔV`, `PROPELLANT` e a órbita prevista
- [ ] abaixo dele, `TRAJECTORIES FOUND` com uma coluna por alternativa
- [ ] os tempos de voo e os Δv das colunas são **diferentes entre si** — se forem
      iguais, a tabela está a repetir a mesma trajetória
- [ ] as recusadas aparecem com o motivo, e o motivo é uma frase e não "falhou"
- [ ] há um botão `USE` por coluna, e só quando há mais de uma opção
- [ ] escolher uma delas replaneja em **segundos** e o resumo passa a mostrar o
      tempo de voo daquela coluna, não o da anterior
- [ ] escolher a mesma duas vezes dá o mesmo resultado

### O mapa

- [ ] `M` abre o mapa; `Shift+M` alterna para `SOLAR SYSTEM MAP`
- [ ] o Sol está no centro, os planetas em volta, as órbitas deles desenhadas
- [ ] a nave está sobre a órbita da Terra
- [ ] a transferência planejada aparece como um arco entre a Terra e Marte, e ele
      **não passa pelo Sol**
- [ ] os marcadores de queima estão sobre o arco: um perto da Terra, dois perto
      de Marte
- [ ] a roda do mouse aproxima e afasta, e o passo é confortável tanto na órbita
      da Terra quanto na de Netuno
- [ ] os anéis estão rotulados em UA
- [ ] o canto diz `REFERENCE SUN`
- [ ] os rótulos dos planetas não se sobrepõem

### A execução

- [ ] `Enter` arma o plano e a mensagem `MISSION PLAN ACCEPTED` aparece
- [ ] o mostrador `TARGET` passa a mostrar a fase: `WAITING FOR DEPARTURE`
- [ ] a nave gira sozinha para a atitude de injeção
- [ ] `.` sobe o time warp; a queima de injeção acontece e **dura minutos**, não
      um instante
- [ ] durante a queima o mostrador diz `INJECTION BURN` e a pluma está acesa
- [ ] depois do corte, a fase passa a `COAST`

### O cruzeiro

- [ ] `REFERENCE` no mostrador de navegação muda de `EARTH` para `SUN` quando a
      nave sai da vizinhança da Terra
- [ ] `AP` e `PE` passam a ser heliocêntricos e o rótulo diz de quem são
- [ ] a distância até Marte **cai**, e a velocidade relativa é de dezenas de km/s
- [ ] `ARRIVAL (PLANNED)` conta para trás
- [ ] no mapa do sistema, a nave anda ao longo do arco planejado
- [ ] o warp a 1e6× e 1e7× não desestabiliza nada: nenhum NaN, nenhuma órbita que
      se desfaz, nenhum aviso no log

### A aproximação

- [ ] a fase passa a `APPROACH`
- [ ] `REFERENCE` muda para `MARS`
- [ ] Marte **cresce** na janela ao longo dos últimos dias; com a câmera em
      `TARGET` dá para o acompanhar
- [ ] o disco de Marte é ocre, tem regiões escuras e calotas polares
- [ ] o lado voltado para o Sol está iluminado e o outro escuro

### A captura

- [ ] a fase passa a `CAPTURE ORIENTING` e a nave gira para retrógrado
- [ ] `CAPTURE BURN`, e ela dura **doze minutos** — baixar o warp para ver
- [ ] depois dela, `ORBIT INSERTION`, e uma segunda queima curta (~15 s)
- [ ] a fase termina em `COMPLETE`

### A órbita final

- [ ] `REFERENCE MARS`
- [ ] `AP` e `PE` ficam perto de **500 km** e a excentricidade abaixo de 0,01
- [ ] o mapa local mostra a órbita em torno de Marte
- [ ] o mostrador de sistema diz quanto propelente sobrou
- [ ] `MARS ORBIT ACHIEVED`

---

## O que observar além da lista

**A queima de captura é longa.** Doze minutos com o motor aceso a 19 km/s de
velocidade no periapsis. Se ela parecer instantânea, o warp está alto demais para
se ver o que está a acontecer — baixe-o.

**A segunda queima é minúscula.** Quinze segundos. É ela que fecha o círculo, e a
diferença entre `192 × 993 km` e `497 × 503 km` é inteiramente ela.

**O mapa local durante o cruzeiro é inútil, e está certo que seja.** Centrado no
Sol a nave está num arco; centrado na Terra ela está a afastar-se numa hipérbole
que não diz nada. É para isso que existem dois modos.

---

## Como registar uma falha

Anotar, sempre:

```text
o que se fez              a tecla, o valor no painel, o instante
o que aconteceu           o que a tela mostrou
o que devia ter acontecido
tempo de missão           o relógio do cockpit, não o de parede
fase da missão            a string do mostrador TARGET
```

Classificar como `BLOCKER`, `BUG`, `PHYSICS_DEBT`, `VISUAL_DEBT` ou
`IMPROVEMENT` (regra 3). Só `BLOCKER` interrompe a entrega, e só é `BLOCKER` se
impedir selecionar, planejar, visualizar, executar, navegar, aproximar, capturar
ou orbitar.
