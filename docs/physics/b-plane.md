# Plano B: mirar um sobrevoo, e não um corpo

Status: **implementado** (Milestone 6)
Pré-requisitos: `docs/physics/lambert.md` §5, `docs/architecture/navigation.md`
Última revisão: 2026-09-13

## 1. O problema, no número que o denunciou

O `orbit-cli intercept` fecha uma transferência lunar contra o modelo completo e
avisa, sozinho, o que não sabe fazer:

```
miss at arrival  : 1676.862847 km  (0.965156 target radii)
*** that is INSIDE Moon: this is an impact trajectory, not a flyby ***
```

O corretor diferencial funciona — leva o erro de 363 287 km para 1,9 km. O que
está errado é **o alvo**: ele mira a *posição do centro* da Lua. Acertar o centro
de um corpo é, por definição, colidir com ele.

A correção óbvia — "mire um ponto a 100 km de altitude" — não funciona, e o
motivo é o assunto deste documento.

## 2. Por que não basta mirar um ponto deslocado

A Lua **puxa**. Uma trajetória que, sem gravidade lunar, passaria a uma distância
`B` do centro, na verdade passa a uma distância menor `r_p`, e a diferença não é
pequena. Com `v_∞` a velocidade de chegada no infinito,

```
B = r_p · √( 1 + 2μ / (r_p v_∞²) )
```

que é a **focagem gravitacional**. Para a Lua (`μ = 4,9028·10¹² m³/s²`) e um
periapse rasante:

| `v_∞` | `B / r_p` |
|---|---|
| 100 m/s | **23,1** |
| 200 m/s | 11,6 |
| 500 m/s | 4,73 |
| 1 000 m/s | 2,52 |
| 2 000 m/s | 1,53 |
| 5 000 m/s | 1,10 |

A nossa transferência de 4,5 dias chega com **`v_∞ = 843,5 m/s`**, medido no
estado de chegada. Então:

| altitude de periapse desejada | `r_p` | `\|B\|` necessário | focagem |
|---|---|---|---|
| 0 km (rasante) | 1 737,4 km | 5 192,5 km | ×2,99 |
| **100 km** | **1 837,4 km** | **5 357,0 km** | **×2,92** |
| 1 000 km | 2 737,4 km | 6 724,4 km | ×2,46 |

Mirar um ponto a 1 837 km do centro produziria um periapse de **623 km abaixo da
superfície**. A distância que se mira não é a distância a que se passa.

## 3. O plano B

Definições, todas relativas ao corpo alvo:

* `Ŝ` — direção da **assíntota de chegada**, isto é, a direção de `v_∞`;
* o **plano B** é o plano que passa pelo centro do corpo e é **perpendicular a
  `Ŝ`**;
* `B` — o vetor do centro do corpo até o ponto onde a assíntota de chegada fura
  esse plano.

`|B|` é o *parâmetro de impacto*: a distância a que a trajetória passaria se o
corpo não tivesse gravidade. E o resultado que torna isso computável:

```
|B| = b = |a| √(e² − 1)
```

o **semi-eixo menor da hipérbole**. Três caminhos para o mesmo número — a
definição geométrica, os elementos orbitais, e a fórmula de focagem de §2 — e no
teste os três concordam até o último bit disponível.

### 3.1 As coordenadas `T` e `R`

`B` está no plano B, que é bidimensional. Os eixos convencionais:

```
T̂ = (Ŝ × k̂) / |Ŝ × k̂|        k̂ = polo de referência (J2000)
R̂ = Ŝ × T̂
```

e o alvo de mira são os dois escalares **`B·T`** e **`B·R`**. Eles separam duas
coisas que a distância sozinha mistura:

* `|B| = √((B·T)² + (B·R)²)` decide **a que altura** se passa;
* o **ângulo** de `B` no plano decide **por que lado** — e portanto o plano da
  órbita que uma inserção produziria.

`k̂` é convenção pura: girá-lo gira `T̂` e `R̂` juntos e não move `B`. Fica o polo
J2000 porque é o mesmo eixo do frame de integração
(`docs/architecture/coordinate-system.md`) e não exige rotação nenhuma.

### 3.2 De onde sai `Ŝ`

Com `ĥ` a normal da órbita e `ê` a direção do periapse,

```
Ŝ = (1/e) ê + (√(e² − 1)/e) (ĥ × ê)
B = b (Ŝ × ĥ)
```

A primeira é a direção da posição no infinito de chegada, com sinal trocado
porque a nave vem **de lá para cá**. A segunda põe `B` no plano B — e
`B · Ŝ = 0` é exato, medido em `10⁻¹⁰` contra um `|B|` de `6·10⁶ m`, que é o ulp.

## 4. Invertendo: de uma altitude para um alvo

O que se quer dizer é "passe a 100 km de altitude". O que o corretor precisa
receber é `(B·T, B·R)`. A ponte é §2 invertida:

```
r_p = √( (μ/v_∞²)² + |B|² ) − μ/v_∞²
```

e no sentido que a missão usa, `|B|` a partir de `r_p`, que é a fórmula de §2.
Round-trip medido: **10⁻⁹ m** sobre raios de milhares de quilómetros.

Falta o ângulo. Um sobrevoo com um dado `|B|` pode acontecer em qualquer azimute
em torno de `Ŝ`, e cada um dá um plano de órbita diferente. O parâmetro é
explícito e chama-se `b_plane_angle`:

```
B·T = |B| cos θ        B·R = |B| sen θ
```

`θ = 0` põe `B` ao longo de `T̂`, que é perpendicular ao polo J2000: o sobrevoo é
**equatorial** em relação a esse polo. `θ = 90°` põe `B` ao longo de `R̂` e o
sobrevoo é **polar**. Entre os dois, tudo. Não há escolha "natural" aqui e por
isso não há padrão escondido: quem planeia a missão diz qual.

## 5. Três graus de liberdade, três alvos

A velocidade de partida tem três componentes. O plano B tem dois. Sobra um, e
ignorá-lo deixa o problema mal posto — o corretor encontraria uma variedade de
soluções e o jacobiano seria singular.

O terceiro alvo é o **tempo**: o instante da máxima aproximação. Expresso como
distância, para que as três componentes tenham a mesma unidade e a mesma norma
signifique alguma coisa:

```
erro = ( B·T − alvo_T ,  B·R − alvo_R ,  v_∞ · (t_ca − t_alvo) )
```

A terceira componente é o **erro ao longo da trajetória**: quanto a nave está
adiantada ou atrasada, medido em metros percorridos. Somar metros com segundos
numa norma daria um número sem significado e uma tolerância impossível de
justificar (§32).

## 6. Onde o plano B é avaliado

`B` vem de uma hipérbole **osculadora**, e uma hipérbole osculadora só descreve a
trajetória onde a Lua domina. Avaliada a meio caminho, com a Terra a mandar, o
número é lixo.

O ponto de avaliação é a **máxima aproximação** encontrada na propagação. É onde
a aproximação de dois corpos é melhor, e é suave na velocidade de partida — o que
o jacobiano por diferenças finitas exige.

Isto é uma aproximação e é a principal deste documento: a hipérbole osculadora no
periapse não é a trajetória real, que sofre a Terra e o Sol durante todo o
sobrevoo. O resíduo aparece como a diferença entre o `r_p` pedido e o `r_p`
entregue, e está medido em §8.

## 7. Inserção em órbita lunar

Chegar não é ficar. No periapse da hipérbole a velocidade é

```
v_p = √( v_∞² + 2μ/r_p )
```

e a velocidade circular ali é `v_c = √(μ/r_p)`. A queima de inserção é retrógrada
e vale

```
Δv = v_p − v_c                           (circularizar)
Δv = v_p − √( μ (2/r_p − 2/(r_p + r_a)) )   (elipse com apoapse r_a)
```

Para a nossa chegada (`v_∞ = 843,5 m/s`) numa órbita circular de 100 km:

```
v_p = 2 459,3 m/s     v_c = 1 633,5 m/s     Δv = 825,8 m/s
```

O valor é conferível contra a história: a inserção lunar da Apollo custou cerca
de 900 m/s, de uma chegada um pouco mais rápida. A dependência de `v_∞` é fraca,
e é a razão de ser assim:

| `v_∞` | `Δv` de inserção a 100 km |
|---|---|
| 200 m/s | 685,3 m/s |
| 500 m/s | 730,1 m/s |
| 800 m/s | 811,2 m/s |
| 1 000 m/s | 883,8 m/s |

`v_∞` entra dentro de uma raiz somada a `2μ/r_p = 5,34·10⁶ m²/s²`, que é grande
perto de `v_∞² = 7,1·10⁵`. Chegar 25 % mais depressa custa 9 % mais para parar.

## 8. Domínio de validade

| Hipótese | Quando quebra |
|---|---|
| hipérbole osculadora no periapse representa o sobrevoo | perturbação da Terra durante o sobrevoo; é o resíduo medido em §9 |
| corpo pontual | a Lua tem `J₂ = 2,03·10⁻⁴` e um `C₂₂` comparável; um periapse baixo sente |
| `v_∞ > 0` | uma chegada **elíptica** (capturada sem queima) não tem plano B: não há assíntota. O código rejeita em vez de devolver `NaN` |
| queima impulsiva na inserção | uma queima finita perde para a gravidade, como a injeção já perde (§`lambert.md` §5) |
| `t_ca` é a máxima aproximação da janela | se a propagação parar antes, `t_ca` é o fim da janela e o alvo de tempo perde sentido |

## 9. A viagem, ponta a ponta

```bash
orbit-cli intercept tests/scenarios/lunar-intercept.json --to Moon --tof 4.5 \
          --flyby-altitude-km 100 --tolerance-km 0.2 --insert
```

O alvo é atingido em **duas etapas**, e a primeira não é otimização, é
necessidade:

```
stage 1, reach the body (position targeting):
  initial miss   : 363286.9292 km
  final miss     : 1.880499904 km   converged

stage 2, open the periapsis (B-plane targeting), aiming a flyby at 100 km altitude:
  pass 1   : v_inf 848.52737 m/s, aim |B| 5329.11165 km (focusing x2.9004), r_p now 0.017063862 km
  pass 2   : v_inf 842.19756 m/s, aim |B| 5364.41867 km (focusing x2.9196), r_p now 1815.80132 km
  converged: r_p 1837.36683 km, wanted 1837.4 km
  iterations: 2 (9 trajectories)
```

A partir da solução crua de Lambert **não há plano B para ler**: o encontro está a
363 000 km, não existe hipérbole de sobrevoo, e as três colunas do jacobiano saem
quase paralelas — medido, e o corretor empaca dizendo que nenhum passo de Newton
reduz o erro. A etapa 1 põe a nave na Lua; só então há um sobrevoo para moldar.

Chegada:

```
closest approach : 1837.36683 km at 2026-01-05T11:58:51  (altitude 99.96683 km)
relative speed   : 2458.871345 m/s
```

33 metros do periapse pedido, e a velocidade no periapse bate com a previsão de
§7 (2 459,3 m/s) em 0,02 %.

### 9.1 A inserção, executada

```
insertion at periapsis (2026-01-05T11:58:51):
  delta-v needed : 825.3524658 m/s  (retrograde)
  orbit period   : 117.78781 min

  lunar orbit insertion: 82.50034759 s, 1.83460973 kg,
      engine delta-v 825.3524658 m/s, speed change -824.2199728 m/s
      (difference = gravity loss)

orbit about Moon, one revolution after the burn:
  periapsis      : 1834.304986 km  (altitude 96.904986 km)
  apoapsis       : 1840.496559 km  (altitude 103.09656 km)
  eccentricity   : 0.001684872631
  inclination    : 18.912743 deg
  period         : 117.79107 min

CAPTURED: the orbit is closed about Moon.
```

Uma órbita de **96,9 × 103,1 km**, e os 6,2 km de diferença entre apoapse e
periapse são o que a queima **finita** deixa: 82,5 s de queima em vez de um
impulso, e a nave move-se enquanto queima. A `e = 0,0017` não é erro numérico, é
a física da queima que o modelo de fato executa — a mesma razão de
`engine delta-v 825,35` e `speed change 824,22` diferirem.

O período medido, 117,79 min, é o previsto em §7 por Kepler.

### 9.2 Duas coisas que a implementação obrigou a medir

**O tempo tem de estar dentro da janela.** A terceira componente do alvo é o
instante da máxima aproximação, e se a propagação parar na época de chegada
nominal, esse instante fica **preso no fim da janela**: deixa de responder à
velocidade de partida, a terceira linha do jacobiano vai a zero e Newton não tem o
que resolver. Medido antes da correção: `d(componente de tempo)/dv = 2·10⁻⁶ m` por
m/s, contra `10⁶` das outras duas. A sonda passa a voar 25 % além da chegada.

**A tolerância é no periapse, não em `|B|`.** Diferenciando a fórmula de §2,

```
dr_p / db = b / (r_p + μ/v_∞²)
```

que aqui vale **0,61**. A primeira versão passava a mesma tolerância de 10 km ao
corretor e parava 5,4 km abaixo da altitude pedida — todos os números certos, e a
altitude errada. Agora `--tolerance-km` significa periapse e é convertida.

## 10. O que o teste mede

`tests/scientific/test_b_plane.cpp`, 9 casos:

| Quantidade | Origem da tolerância |
|---|---|
| `\|B\|` por três caminhos (geometria, `\|a\|√(e²−1)`, focagem) | identidade algébrica; ulp |
| `B · Ŝ = 0` | exato por construção; ulp de `10⁶ m` |
| `Ŝ, T̂, R̂` ortonormais | idem |
| `r_p → \|B\| → r_p` | inversão em forma fechada; medido `10⁻⁹ m` |
| invariância de `\|B\|` a `k̂` | `k̂` só gira `T̂` e `R̂`; `|B|` não pode mudar |
| `Δv` de inserção vs `v_p − v_c` | forma fechada recalculada |
| periapse entregue vs pedido, modelo completo | a perturbação de §6, **medida** e não assumida |
