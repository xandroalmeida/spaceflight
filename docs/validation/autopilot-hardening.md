# Endurecimento do autopiloto

**Milestone 6.2, seções 8 a 11.**

O planejador entrega uma solução muito melhor do que a execução consegue voar.
Este documento mede a diferença, nomeia o mecanismo, varre o ganho, e escolhe.

```text
IMPULSIVE     e ≈ 0.000003     nenhum erro de controle existe neste modelo
FINITE BURN   e ≈ 0.0017       o motor queima, a guiagem é ideal
AUTOPILOT     e ≈ 0.0105       o motor aponta para onde o casco aponta
```

---

## 1. O mecanismo, em forma fechada

O atraso não é ruído nem "slop" do integrador. É o **erro de regime permanente
de um controlador PD seguindo um alvo que gira**, e tem expressão exata.

Um PD é um sistema de tipo 0. Seguir uma **rampa** — uma direção alvo que roda a
taxa ω constante — exige um torque proporcional permanente, e a única coisa que
pode fornecê-lo é um erro angular permanente:

```text
θ_lag = 2 ζ ω / ω_n
```

Para a queima de captura numa órbita lunar de 100 km, o alvo é a direção
retrógrada relativa à Lua, que no periapse roda à taxa orbital. Com ζ = 1 e
ω_n = 0,05 rad/s:

```text
ω     = sqrt(4.9028e12 / (1838e3)³) = 8.886e-4 rad/s
θ_lag = 2 · 1 · 8.886e-4 / 0.05     = 0.03554 rad = 2.036°
```

**Medido: pico de 2,064° a 2,075°**, sobre três épocas independentes.

A fórmula prevê o **pico** e não a média, e isso é deliberado: `ω` foi avaliado no
periapse, que é onde o retrógrado gira mais depressa, e o pico do erro acontece
lá. 2,036° previsto contra 2,064° medido são **1,4 %** — sobre uma queima de 83 s
através de um campo gravitacional real, com o `r` variando.

Isto importa por uma razão operacional: significa que o atraso escala como
`1/ω_n` e que a varredura da §9 pode ser lida como uma verificação da lei, não
como uma busca cega. Se algum ponto da varredura não cair sobre `1/ω_n`, alguma
outra coisa está acontecendo — saturação, por exemplo — e a discrepância é o
sinal.

### Como o atraso vira excentricidade

O empuxo aponta 2° fora do retrógrado instantâneo durante toda a queima de
captura. Duas consequências:

* **perda de cosseno** — a componente útil cai por `cos θ ≈ 1 − θ²/2`, ou seja
  6·10⁻⁴ a 2°. Sozinha isso seria irrelevante;
* **componente transversal** — `sin θ ≈ θ` do empuxo vai para uma direção que
  não é a do frenagem. É essa componente que abre a órbita, e ela é *linear* no
  atraso enquanto a perda de cosseno é quadrática.

Daí a excentricidade degradar de 0,0017 para 0,0105 com 2° de atraso: o termo
dominante é linear.

---

## 2. O instrumento

O que mede o quê, e onde:

| grandeza | onde é amostrada | intervalo |
|---|---|---|
| erro de apontamento médio | todo passo aceito **dentro da queima de captura**, ponderado pelo passo | a queima |
| erro de apontamento de pico | idem, máximo | a queima |
| tempo de assentamento | último instante acima do limiar, contado da troca de comando | do corte da injeção ao fim |
| propelente de RCS | ∫ ṁ dt sobre a alocação real | a segunda perna |
| duty cycle | média dos throttles dos 12 propulsores, ponderada pelo passo | a segunda perna |
| saturação | fração do tempo em que a demanda excede o torque disponível | a segunda perna |
| pico de \|ω\| | todo passo aceito | a segunda perna |

Duas escolhas que não são óbvias:

**A ponderação pelo passo.** Uma média aritmética sobre passos aceitos pesaria
demais os instantes em que o integrador está com passo curto — que são
exatamente os instantes de maior aceleração. Ponderar pelo passo mede o tempo,
não a contagem.

**O intervalo do assentamento é a segunda perna inteira**, não a queima. A
aquisição acontece durante os dias de deriva — o comando muda para "retrógrado
relativo à Lua" no corte da injeção, quando o nariz ainda está na direção
inercial da injeção, e o casco tem dias para virar. O que a queima mede é o
**rastreamento**, que é outra pergunta.

Consequência: quando o controlador nunca entra na especificação, o "tempo de
assentamento" sai igual à duração da perna inteira. Isso não é um defeito da
métrica — é a forma honesta de dizer que o erro nunca desceu abaixo do limiar.
A ω_n = 0,05 o atraso permanente é 1,65° contra um limiar de 0,5°, então ele
nunca desce, e o número reportado (≈ 4·10⁵ s) diz exatamente isso.

### Um defeito que este instrumento custou

A primeira versão da medida lia a atitude do estado no **fim** da propagação —
duas órbitas depois da queima — e reportava o ângulo entre uma atitude que
ninguém estava mais controlando e uma direção retrógrada de outro ponto da
órbita. Dava 1,5° em **toda** largura de banda.

O que deveria ter denunciado: o atraso de um PD vai como `1/ω_n`, e um número
que não se move quando ω_n quadruplica não está medindo o atraso.

### E um segundo, de custo

A instrumentação roda **apenas no voo que tem queima de captura**. Cada um dos
dois corretores faz algumas centenas de voos de sonda por época, todos pela
mesma função, e o observador pede o estado da Lua à efeméride duas vezes por
passo aceito. Sem a guarda, o custo de uma época AUTOPILOT multiplicava por uma
ordem de grandeza para medir trajetórias de sonda que ninguém voa. Nada se
perde: toda grandeza aqui é sobre a queima de captura ou sobre a manobra que a
precede, e nenhuma das duas existe numa sonda.

---

## 3. Saturação física (§11)

O controlador **não** pode assumir torque infinito, e antes deste milestone ele
assumia.

### O que existe fisicamente

```text
12 propulsores em 6 binários puros
braço                a = 2.0 m
empuxo por propulsor F = η q w = 1.0 · 2e-5 · 0.03c = 179.9 N
torque por binário   2 a F = 719.5 N·m        (dois propulsores por eixo, por sentido)
inércia (caixa sólida 1000 kg, 8 × 3 × 3 m)
    I_xx = m(b²+c²)/12 = 1500 kg·m²
    I_yy = I_zz = m(a²+c²)/12 = 6083 kg·m²
aceleração angular máxima
    sobre x   719.5 / 1500  = 0.480 rad/s²
    sobre y,z 719.5 / 6083  = 0.118 rad/s²
```

### O defeito que isto encontrou

`RcsSystem::max_torque()` somava `|τ|` sobre os doze propulsores e reportava
`4aF` onde a nave entrega `2aF`. Na disposição em binários, quatro propulsores
produzem torque ao longo de x — dois para +x e dois para −x — e abrir os quatro
produz **nada**.

Como string de diagnóstico isso era apenas errado. Como limite contra o qual um
controlador satura, teria deixado o controlador pedir o dobro do torque que
existe e chamar isso de não-saturado. Corrigido: `max_torque()` agora soma
apenas as contribuições **na direção certa**, e há `max_torque_about(eixo)` e
`max_angular_acceleration(inércia, eixo)` para a pergunta geral.

### Por que o limite está no controlador e não só nos atuadores

Sem o limite, a demanda ainda era truncada — `RcsSystem::allocate` prende cada
propulsor em [0, 1]. Mas era truncada **por propulsor**, o que muda a *direção*
do torque entregue tanto quanto o tamanho, sempre que os eixos saturam em
proporções diferentes. A nave então gira em torno de um eixo que ninguém pediu.

Com `ActuatorLimits` o vetor inteiro é escalado por um fator, fixado pelo eixo
que esgota primeiro. Um comando saturado passa a ser **a rotação certa entregue
devagar**, em vez da rotação errada entregue depressa.

---

## 4. A varredura (§9)

Três épocas separadas por sete dias não seriam suficientes se os ganhos vissem
geometrias diferentes, então **cada ganho voa a geometria que a busca
FINITE_BURN fixou para aquela época**. Sem o pino, a coluna da excentricidade
estaria medindo a busca e não o controlador.

### A varredura de especificação: 6 ganhos × 3 épocas

| ω_n [rad/s] | capturas | lag médio | lag de pico | pior pico | RCS | pico \|ω\| | e | pior e |
|---:|:---:|---:|---:|---:|---:|---:|---:|---:|
| 0,050 | **0/3** | 1,653° | 2,065° | 2,075° | 47 mg | 46 mrad/s | 0,0105 | — |
| 0,075 | 3/3 | 1,107° | 1,373° | 1,374° | 69 mg | 70 mrad/s | 0,00639 | 0,00642 |
| 0,100 | 3/3 | 0,829° | 1,031° | 1,032° | 90 mg | 93 mrad/s | 0,00435 | 0,00436 |
| 0,150 | 3/3 | 0,550° | 0,683° | 0,685° | 133 mg | 139 mrad/s | 0,00232 | 0,00239 |
| **0,200** | **3/3** | **0,412°** | **0,512°** | **0,513°** | 177 mg | 186 mrad/s | 0,00132 | 0,00141 |
| 0,300 | 3/3 | 0,275° | 0,341° | 0,341° | 264 mg | 277 mrad/s | 0,00035 | 0,00046 |

A ω_n = 0,05 **nenhuma das três épocas captura**: a órbita sai fora da banda de
80–120 km. É o ponto de partida do brief (`e ≈ 0,0105`) e ele não é apenas
impreciso — é insuficiente.

### A lei `1/ω_n`, verificada

| ω_n | lag médio | `1,6529 × 0,05/ω_n` | pico/média | RCS/ω_n | pico\|ω\|/ω_n |
|---:|---:|---:|---:|---:|---:|
| 0,050 | 1,6529° | 1,6529° | 1,2496 | 940 mg/(rad/s) | 928 |
| 0,075 | 1,1068° | 1,1019° | 1,2409 | 920 | 929 |
| 0,100 | 0,8288° | 0,8265° | 1,2444 | 900 | 929 |
| 0,150 | 0,5498° | 0,5510° | 1,2425 | 887 | 929 |
| 0,200 | 0,4120° | 0,4132° | 1,2425 | 885 | 929 |
| 0,300 | 0,2754° | 0,2755° | 1,2389 | 880 | 923 |

A coluna do meio é a previsão de forma fechada normalizada no primeiro ponto:
**0,4 % de erro sobre um fator de seis em ganho**. O atraso é o que a §1 diz que
é, e não há mais nada acontecendo.

`pico/média = 1,2425 ± 0,004` e `pico\|ω\|/ω_n = 929 ± 3` são constantes, o que é
a evidência da §5 sobre oscilação e sobre a resposta ser puramente de segunda
ordem escalada.

---

## 5. A especificação (§10)

Um ganho escolhido só pela excentricidade compra-a com propelente, com um
atuador encostado no batente, e com um casco a ser sacudido. Nenhuma dessas
coisas aparece num elemento orbital. Daí a especificação ter quatro linhas e não
uma:

```text
peak pointing error   < 1.0°
mean pointing error   < 0.5°
no sustained oscillation
RCS duty cycle acceptable
```

A especificação é medida sobre o **pior caso** das épocas, não a mediana: uma
especificação é sobre o que a nave nunca deve fazer.

| ω_n | pior pico | média | captura | veredito |
|---:|---:|---:|:---:|:---|
| 0,050 | 2,075° | 1,653° | 0/3 | reprova pico, média e captura |
| 0,075 | 1,374° | 1,107° | 3/3 | reprova pico e média |
| 0,100 | 1,032° | 0,829° | 3/3 | reprova pico por 3 %, e média |
| 0,150 | 0,685° | 0,550° | 3/3 | passa pico, **reprova média por 10 %** |
| **0,200** | **0,513°** | **0,412°** | **3/3** | **passa** |

**Escolhido: ω_n = 0,20 rad/s, ζ = 1.** O menor ganho varrido que satisfaz as
duas linhas. Não o maior disponível, e isso é a §9 sendo obedecida literalmente.

`0,100` merece uma nota: falha o pico por 3 % (1,032° contra 1,000°) e a média
por 66 %. O brief mencionava `ω_n ≥ 0,1 rad/s` como a faixa medida, e a varredura
confirma que 0,1 é de fato onde o **pico** começa a ficar próximo — mas a média
precisa de mais que o dobro disso.

### As outras duas linhas

**Sem oscilação sustentada.** Verificada sem inspecionar séries temporais, por um
argumento mais forte: a razão **pico/média** fica em `1,2425 ± 0,004` ao longo de
todo o varrimento, um fator de quatro em ganho. A forma de onda do erro não muda,
só a escala. Um controlador subamortecido teria essa razão **crescendo** com o
ganho, porque o sobressinal cresce enquanto o regime permanente encolhe. Com
ζ = 1 não há sobressinal por construção, e a constância da razão é a evidência
medida disso.

**Duty cycle aceitável.** A ω_n = 0,20 os doze propulsores gastam **0,177 g** de
propelente na segunda perna inteira de uma missão — aquisição de 180° mais a
queima de captura — contra um tanque de 19 000 kg. São 9·10⁻⁹ do tanque. O duty
cycle médio fica abaixo de 0,001: os propulsores estão praticamente fechados
quase o tempo todo, e abrem para o transiente da aquisição.

**Saturação: zero.** Em nenhuma das dezoito corridas a demanda excedeu o torque
disponível. Isso não é sorte — é o que os números da §3 dizem: `0,118 rad/s²`
disponível sobre os eixos fracos contra uma manobra de aquisição que atinge
`0,186 rad/s` de pico de velocidade angular ao longo de dezenas de segundos. A
margem é grande e agora está medida em vez de suposta.

### Excentricidade não é monotônica no ganho, e isso foi previsto antes de medido

A coluna `e` da varredura cai de 0,0105 para 0,00035 entre ω_n = 0,05 e 0,30 —
e passa **abaixo** dos 0,0017 que a queima finita entrega na mesma geometria.
Isso não podia ser "o atraso degradando uma linha de base de 0,0017", porque a
linha de base é o limite de atraso zero.

Um ajuste de mínimos quadrados sobre os seis ganhos diz o que é:

```text
e = | 0,00735 · lag[°] − 0,001710 |        resíduos ±3,6·10⁻⁵ sobre 0,00035 .. 0,0105
```

**O intercepto é a excentricidade da queima finita**, 0,001710 contra 0,001732 —
1,3 %. Leitura: o atraso contribui um vetor de excentricidade e a guiagem
retrógrada-instantânea ideal contribui outro **de sinal oposto**; `|e|` é a
diferença dos dois. Consequências, todas testáveis:

1. `|e|` tem um **mínimo** onde eles se cancelam, em lag = 0,233° → ω_n ≈ 0,355;
2. acima disso `e` tem de **voltar a subir**;
3. com ω_n → ∞ o autopiloto *é* guiagem ideal, então `e` → 0,0017.

O ajuste usou apenas ω_n ≤ 0,30. Dois ganhos foram então medidos **fora** dele:

| ω_n | lag medido | `e` previsto | `e` medido | erro |
|---:|---:|---:|---:|---:|
| 0,40 | 0,206° | 0,000197 | 0,000292 | 33 % (10⁻⁴ absoluto, junto ao mínimo) |
| 0,60 | 0,137° | 0,000704 | **0,000744** | **5,4 %** |

`e` subiu, como exigido. A forma completa:

```text
  ω_n     e
  0,050   0,010468  ████████████████████████████████████████████████████████████
  0,075   0,006389  █████████████████████████████████████
  0,100   0,004351  █████████████████████████
  0,150   0,002324  █████████████
  0,200   0,001316  ████████
  0,300   0,000349  ██
  0,400   0,000292  ██   ← mínimo
  0,600   0,000744  ████ ← subindo de novo
```

`0,40` e `0,60` são uma época cada e **não** fazem parte da varredura de
especificação: existem para testar o mecanismo, não para escolher o ganho.

### Por que isto é o argumento mais forte contra a §10 ser ignorada

Um engenheiro que escolhesse a banda **minimizando a excentricidade** pousaria em
ω_n ≈ 0,355 — e pousaria lá por uma razão que não tem **nada** a ver com
qualidade de controle. É o cancelamento entre dois erros opostos, e o ponto de
cancelamento move-se assim que a duração da queima muda, porque o termo da
guiagem ideal depende de quanto o retrógrado gira durante a queima. O ganho
estaria sintonizado para uma missão, não para uma nave.

A especificação de apontamento não tem essa fragilidade: `pico < 1°` e
`média < 0,5°` são monotônicos em ω_n por construção, e é por isso que a escolha
de 0,20 se apoia neles e não na órbita resultante.

### O que a banda custa

Os três custos da §10, todos **lineares em ω_n** sobre o intervalo varrido:

| | a 0,05 | a 0,20 | razão |
|---|---:|---:|---:|
| propelente de RCS por missão | 0,047 g | 0,177 g | 3,8× |
| pico de \|ω\| (aquisição) | 46 mrad/s | 186 mrad/s | 4,0× |
| passos do integrador | 1× | ≈ 3× | 3× |

O pico de velocidade angular é o que um casco tripulado sentiria: 186 mrad/s são
**10,6 °/s**, uma volta completa em 34 s. Para um rebocador não tripulado isso é
irrelevante; para uma cápsula seria a linha que fixaria o teto do ganho, e é por
isso que a coluna existe.

O custo de integração não é desperdício: a malha de atitude a 0,20 rad/s é muito
mais rígida do que a órbita, e o controlador de erro tem de resolver as duas.
Consequência prática registrada em §7 abaixo.

---

## 6. O que NÃO foi feito, e porquê

**O controlador não foi redesenhado** (§8). O atraso de rastreamento é removível
— por *feed-forward* da taxa do alvo, ou por um termo integral — e nenhum dos
dois está implementado. Ambos estão nomeados em
[docs/physics/attitude.md](../physics/attitude.md) seção 7 desde o Milestone 3.

A §8 é explícita: aplicar e validar a faixa medida primeiro. Um feed-forward
levaria o atraso a zero sem gastar largura de banda nenhuma, o que é
estritamente melhor — e é um redesenho do controlador, com a sua própria
campanha de validação. Fica como o próximo passo, escrito aqui para que a
escolha de ganho não seja confundida com a solução do problema.

**A inconsistência da inércia não foi corrigida.** O casco que o controlador gira
tem 1000 kg; a nave que o integrador translada tem 20 000 kg no início da missão
e ~19 990 kg no fim. São o mesmo objeto com duas massas.

Consequência quantitativa, para que ninguém tenha de estimá-la depois: corrigi-la
multiplica `I` por 20, divide a aceleração angular disponível por 20 — de
`0,118 rad/s²` sobre os eixos fracos para `0,0059` — e a margem de saturação que
a §5 mediu como confortável passa a ser a linha que decide o ganho máximo. A
`0,20 rad/s` a aquisição pediria vinte vezes o torque que pede hoje.

Isso é um defeito real e está registrado aqui em vez de silenciosamente
consertado, porque consertá-lo multiplica a inércia por ~20, divide a aceleração
angular disponível por ~20, e portanto **muda todos os números desta página**.
Seria uma campanha nova apresentada como um ajuste. Ambos os lados —
`core/navigation/lunar_transfer.cpp` e
`godot/gdextension/src/simulation_node.cpp` (hoje `app/session/flight_session.cpp`) — usam consistentemente
`solid_box(1000, {8,3,3})`, então o simulador é internamente coerente; o que não
é coerente é com a massa da nave.

---

## 7. Uma consequência prática: o orçamento de passos

O orçamento de `2·10⁶` passos de integrador — que classifica uma corrida como
`TIMEOUT`, contado em passos e não em segundos para que uma máquina rápida e uma
lenta classifiquem igual — foi calibrado para `FINITE_BURN`.

Sob `AUTOPILOT` a 0,20 rad/s ele não chega perto. Uma busca completa — vinte e
quatro candidatos triados, cada um voado uma vez para prever o esforço de
correção — esgotava o orçamento **antes da primeira tentativa**.

Medido, a **mesma** busca, mesma época (2026-01-01):

| modelo | passos | propagações | tempo |
|---|---:|---:|---:|
| `FINITE_BURN` | 138 050 | 435 | 1,7 s |
| `AUTOPILOT` | 42 393 965 | 773 | 984 s |
| razão | **307×** | 1,8× | 580× |

Só 1,8 vezes os voos e 307 vezes os passos: **é o tamanho do passo que colapsa,
não o número de voos**. O estado carrega um quatérnio e uma velocidade angular, o
controlador de erro tem de resolver as duas ao lado da órbita, e a malha de
atitude na banda qualificada é muito mais rígida do que a órbita.

A resposta é um orçamento por modelo, não um orçamento maior para todos:

```cpp
std::size_t step_budget{2'000'000};               // Impulsive, FiniteBurn
std::size_t autopilot_step_budget{150'000'000};   // Autopilot
```

`2·10⁶` está 3,4× acima do pior caso das 365 épocas `FINITE_BURN` (591 036
passos). `150·10⁶` dá a mesma margem de 3,5× sobre a medida acima — com a
ressalva, dita porque importa: o número do `FINITE_BURN` é o pior de 365
amostras, o do `AUTOPILOT` é **uma**. Aperte-o quando existir uma campanha de 365
épocas de autopiloto para apertá-lo com.

Um número único teria de ser o maior, e aí uma corrida `FINITE_BURN` que deu
genuinamente errado moeria setenta e cinco vezes mais antes de alguém ser
avisado. Um orçamento que nunca dispara não é um orçamento.

O que isso custa quando dispara: uma busca saudável leva cerca de dezesseis
minutos, então uma esgotada reporta `TIMEOUT` depois de aproximadamente uma hora.

### E uma observação que fica registrada em vez de virar otimização

Os voos de triagem — um por candidato, com a velocidade de partida de Lambert não
corrigida, cuja única saída é a distância de erro na chegada — **não precisam do
autopiloto**. Naquele voo não há queima de captura; a injeção é voada com o nariz
já na direção da injeção e velocidade angular zero, então o controlador não tem
erro nem demanda; e a manobra de aquisição que domina o custo acontece durante a
deriva, onde os binários de RCS são exatamente balanceados e a força líquida é
zero. A trajetória é a mesma.

Triar com o modelo barato seria correto e cem vezes mais rápido. Não foi feito
porque a correção "a força líquida é exatamente zero" depende de os binários
cancelarem bit a bit, e construir uma otimização de desempenho sobre essa
invariante exige verificá-la — o que é trabalho para outro milestone, não uma
nota de rodapé neste.
