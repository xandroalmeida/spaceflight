# Starfield: diagnóstico, correção e validação

**Milestone 6.1, Parte B.** Bloqueador P0 do Milestone 6: 8 786 estrelas
carregadas, nenhuma no framebuffer.

**Resultado: PASS.** Dois defeitos independentes foram encontrados, medidos e
corrigidos. Os dez estágios do arnês passam sem nenhum resultado fora do
orçamento.

Reprodução:

```bash
cmake --build build-godot --target spaceflight_gdextension -j
./scripts/starfield_validation.sh          # status 0 = tudo passou
```

---

## 1. Por que "invisível" não era um sintoma

Todo caminho possível de falha produzia exatamente a mesma imagem. Projeção
errada, buffer vazio, malha descartada pelo frustum, teste de profundidade
reprovado e cor preta são cinco defeitos diferentes com uma única aparência: uma
tela preta.

Enquanto o único instrumento fosse olhar para o céu, nenhuma hipótese podia ser
eliminada. O arnês (`godot/project/starfield_debug.gd`) existe para separá-las:
cada estágio responde a uma pergunta cuja resposta é um **número**, e o oráculo
nunca é uma impressão humana nem uma captura anterior — é o próprio `core/`,
alcançado por `SpaceflightSky.get_apparent_direction()`,
`get_expected_response()` e `get_expected_colour()`.

---

## 2. O pipeline, estágio por estágio

```text
catálogo carregado        -> StarCatalog::from_bsc5_file      estágio 4
dados de estrela na CPU   -> RelativisticSky::update          estágios 7, 8, 9
buffer da GPU             -> ARRAY_VERTEX + ARRAY_CUSTOM0     estágio 4
vertex shader             -> POINT_SIZE, star_colour          estágios 1, 5
clip space                -> w, z, near, far                  estágios 3, 3b
rasterização              -> point sprite                     estágios 1, 2
fragment shader           -> ALBEDO, ALPHA                    estágio 1
framebuffer               -> pixels medidos                   todos
```

| # | Estágio | Pergunta | Critério |
|---|---|---|---|
| 1 | `axes` | seis estrelas nos seis eixos caem onde a câmera diz? | ≤ 1,5 px do eixo óptico |
| 2 | `scale` | o raio do céu muda a resposta? | ≤ 1,5 px entre raios 1, 10³ e 1,9·10⁵ |
| 3 | `clip` | toda estrela visível está dentro de `[near, far]`? | zero fora |
| 3b | `depth` | quantas estrelas o teste de profundidade descarta? | zero, no plano próximo de produção |
| 4 | `ladder` | 6 → 12 → 100 → 8 786 chegam ao framebuffer? | sintéticas 100 %, BSC5 ≥ 99,5 % |
| 5 | `magnitude` | o brilho é monótono em V e igual ao do `core/`? | monótono, erro ≤ 3 % |
| 6 | `effects` | cada toggle muda alguma coisa? | baseline não vazio, cada toggle difere |
| 7 | `aberration` | ângulo da GPU × ângulo da CPU | ≤ 2 px de ângulo |
| 8 | `doppler` | cromaticidade da GPU × Planck deslocado | ≤ 0,04 em r/(r+g+b) |
| 9 | `beaming` | intensidade da GPU × D⁴ limitado à banda | razão ≤ 12 %, absoluto ≤ 3 % |
| 10 | `snapshots` | 0c, 0,5c, 0,9c, 0,99c contra referência | diferença robusta ≤ 0,02 |

---

## 3. STARFIELD_DEBUG — e por que são dois interruptores

A seção 18 do prompt pede um modo que ignore magnitude, temperatura, Doppler,
beaming e tone mapping, e desenhe tudo com tamanho fixo e branco fixo. Ele
existe como `uniform bool starfield_debug`.

Mas ele sozinho não bastava, e a razão é que o sprite de uma estrela é moldado
**duas vezes** por motivos de apresentação: o tamanho acompanha o brilho, e o
alfa cai radialmente para que um quadrado leia como um ponto. Nenhum dos dois é
fotometria — a física está em `response` — mas ambos chegam ao pixel que o arnês
mede, e juntos custavam cerca de 10 % da medição e a faziam depender de onde
dentro do texel o sprite caiu.

Por isso há um segundo interruptor, `starfield_flat_sprite`, que desliga a
**moldagem** e deixa a fotometria intacta. Ele muda o que uma estrela parece;
não muda o que uma estrela é. As duas perguntas são diferentes e têm dois
interruptores.

| | `starfield_debug` | `starfield_flat_sprite` |
|---|---|---|
| desliga | magnitude, temperatura, Doppler, beaming, curva de resposta | tamanho variável, queda radial do alfa |
| usado por | estágios 1, 2, 3b, 4, 7 (geometria) | estágios 5, 8, 9 (fotometria) |
| padrão | `false` | `false` |

---

## 4. Defeito 1 — `unshaded` descarta EMISSION

**O achado.** `star_field.gdshader` declarava `render_mode unshaded` e escrevia

```glsl
ALBEDO = vec3(0.0);
EMISSION = star_colour;
```

Sob `unshaded`, o renderizador Forward+ do Godot 4 resolve o fragmento como
`vec4(albedo, alpha)` e **nunca lê EMISSION**: o termo de emissão só existe no
ramo iluminado, somado ao ambiente, difuso e especular. As 8 786 estrelas eram
carregadas, transformadas e rasterizadas — em preto, sobre preto.

**A medição.** Dez primitivas de ponto, Godot 4.5.stable, Metal 3.2, Forward+,
uma variante por quadro:

```text
unshaded  + EMISSION               0 pixels acesos   byte máximo   0
unshaded  + EMISSION x100          0 pixels acesos   byte máximo   0
unshaded  + ALBEDO               360 pixels acesos   byte máximo 255
iluminado + EMISSION             360 pixels acesos   byte máximo 255
iluminado + EMISSION sem ALPHA   360 pixels acesos
unshaded  + ALBEDO + alfa radial 290 pixels acesos
```

A segunda linha é a que decide: multiplicar a emissão por cem não traz nada de
volta, e o byte máximo do quadro continua em zero. Isso elimina "está escuro
demais" e deixa só "não é lido". A sexta mostra a queda radial do alfa
esculpindo o quadrado do sprite num disco — presentação, não fotometria
(seção 3).

**A correção.** `unshaded` é o modo certo — uma estrela não deve ser sombreada
pelas luzes da cena — então a cor pertence a `ALBEDO`. Uma linha.

`relativistic_body.gdshader` não é `unshaded` e não tinha o defeito, o que é
consistente com o relatório do Milestone 6: corpos visíveis, estrelas não.

---

## 5. Casos artificiais antes do catálogo

A seção 19 pede seis estrelas nas seis direções antes do BSC5, e a ordem não é
cerimonial: com o céu real, "a projeção está errada" e "a fotometria devolveu
zero" produzem a **mesma** tela vazia, e o arnês precisa distinguir as duas
antes de ter direito a acreditar em qualquer coisa sobre aberração.

`StarCatalog::from_stars()` e `StarCatalog::add_star()` constroem um catálogo
sintético que passa pela mesma aritmética de um registro do BSC5: a direção é
normalizada, `rest_flux` sai de `flux_from_magnitude` e `colour_index` sai de
`colour_index_from_temperature`, o inverso exato de Ballesteros — a ida e volta
fecha em precisão de máquina entre 1 500 K e 40 000 K.

Medido, olhando direto para cada eixo:

```text
star_right(+x)    1 blob, centrado a 0,71 px, pico 1,000
star_left(-x)     1 blob, centrado a 0,71 px, pico 1,000
star_forward(+y)  1 blob, centrado a 0,71 px, pico 1,000
star_backward(-y) 1 blob, centrado a 0,71 px, pico 1,000
star_up(+z)       1 blob, centrado a 0,71 px, pico 1,000
star_down(-z)     1 blob, centrado a 0,71 px, pico 1,000
```

Escada de contagem, com a câmera fixa e o frustum contendo cerca de 20 % da
esfera:

```text
6 sintéticas        2 no quadro,    2 encontradas
12 sintéticas       2 no quadro,    2 encontradas
100 sintéticas     20 no quadro,   20 encontradas
BSC5 (8786)      1853 no quadro, 1851 encontradas   (1575 blobs)
```

As duas que faltam no BSC5 são pares próximos reais cujos blobs se fundiram: 8 786
estrelas produzem 1 575 blobs neste quadro, e quando dois se fundem o centroide
conjunto pode ficar mais longe de qualquer uma delas do que o raio de busca. É um
limite do detector de blobs, não do renderizador — por isso o catálogo real
responde por 99,5 % e os conjuntos sintéticos, que são dispostos para não se
sobrepor, por 100 %.

---

## 6. Defeito 2 — a esfera do céu abaixo de um passo de profundidade

O primeiro defeito escondia o segundo. Com as estrelas finalmente brancas, o
estágio 4 mostrou **um terço delas ainda ausente** — e dessa vez o sintoma tinha
estrutura.

**O achado.** O buffer de profundidade tem 24 bits. Com Z reverso, a
profundidade de um fragmento é `near/distância`, então uma estrela na esfera do
céu cai em

```text
near / SKY_RADIUS
```

e para ser distinguível do plano distante isso precisa vencer um passo de
quantização, `2⁻²⁴ = 5,96e-8`. Com o `near = 0,01` que a cena trazia:

```text
0,01 / 1,9e5 = 5,26e-8      ->  0,88 passo
```

**abaixo** de um passo. Estrelas cuja profundidade arredonda para o zero do
plano distante são descartadas pelo teste de profundidade, em silêncio, e *quais*
delas arredondam é decidido por aritmética float.

**A medição.** 400 estrelas sintéticas no raio de produção, 83 dentro do quadro,
`far = 2,0e5` fixo:

| `near` | profundidade | passos | no quadro | renderizadas |
|---:|---:|---:|---:|---:|
| 0,01 | 5,263e-8 | 0,88 | 83 | **59** |
| 0,012 | 6,316e-8 | 1,06 | 83 | 83 |
| 0,02 | 1,053e-7 | 1,77 | 83 | 83 |
| 0,05 | 2,632e-7 | 4,42 | 83 | 83 |
| 0,5 | 2,632e-6 | 44,15 | 83 | 83 |

O penhasco está entre 0,88 e 1,06 passo, exatamente onde a aritmética o coloca.

Uma varredura independente (200 estrelas, 39 no quadro) mostra que o plano
distante sozinho não explica nada e o plano próximo explica tudo:

| raio | `near` | `far` | renderizadas de 39 |
|---:|---:|---:|---:|
| 1,9e5 | 0,01 | 2,0e5 | 28 |
| 1,9e5 | 0,01 | 4,0e5 | **0** |
| 1,9e5 | 100 | 4,0e5 | 39 |
| 1,0e3 | 0,01 | 2,0e5 | 39 |
| 1,0 | 0,01 | 1,1 | 39 |

Afastar o plano distante **piora**, porque empurra a esfera ainda mais para
dentro do último passo representável. Encolher o raio resolve, e é por isso que
nenhum teste com uma esfera pequena jamais teria encontrado isto.

**A correção.** `CAMERA_NEAR` foi de `0,01` para `0,05` em `main.gd` — quatro
vezes o penhasco medido e ainda apenas 50 km. O raio do céu não muda: ele tem de
continuar além do Sol, a 1,47e5, para que o Sol ainda o oculte.

O plano próximo maior é alcançável por uma câmera com zoom, então o limite passou
a ser escrito onde a distância é decidida, em `_place_camera`:

```gdscript
var distance := maxf(natural * orbit_zoom, CAMERA_NEAR * NEAR_PLANE_CLEARANCE)
```

na distância e não num fator de zoom, porque o fator teria de ser reajustado toda
vez que qualquer um dos dois números se mexesse.

**O estágio 3b fica na suíte depois da correção**, com a linha `near = 0,01`
mantida como controle e marcada como falha esperada. A regra é explícita: se a
linha de controle parar de perder estrelas, a medição parou de medir. A falha é
invisível — não avisa, não dá erro, e o que deixa na tela é um campo de estrelas
perfeitamente plausível com dois terços das estrelas nele.

---

## 7. Clip space

Com o BSC5 completo, `near = 0,05`, `far = 2,0e5`, raio 1,9e5:

```text
estrelas                    8786
w <= 0 (atrás da câmera)    4439      metade do céu, como esperado
mais perto que near         0
mais longe que far          0
w à frente da câmera        [25,96 , 189931,78]
z em NDC                    [0,996148 , 1,000000]
```

Nenhuma estrela fora de `[near, far]`. A faixa estreita de `z` em NDC é a mesma
observação da seção 6 vista de outro lado: toda a esfera vive nos últimos 0,4 %
da faixa de profundidade, e é por isso que a margem tinha de ser contada em
passos de quantização e não em unidades de cena.

O estágio 2 fecha a pergunta da seção 20 do prompt — o starfield é direcional e
o raio não deve importar. Com `near` e `far` mantidos em proporção ao raio:

```text
raio        1,0    ->  2 estrelas
raio     1000,0    ->  2 estrelas, deslocamento máximo 0,000 px
raio   190000,0    ->  2 estrelas, deslocamento máximo 0,000 px
```

Zero pixels de diferença ao longo de cinco ordens de grandeza. O que importa não
é a escala astronômica: é a razão `near/raio` da seção 6.

---

## 8. Duas convenções de pixel, medidas e não supostas

Uma projeção responde em coordenadas contínuas, onde a fronteira entre o
primeiro e o segundo pixel está em 1,0. Um centroide de blob é uma média
ponderada de índices inteiros, onde o pixel 0 tem centro em 0,5.

O meio pixel entre os dois não é ruído — é um viés fixo. Subtraí-lo move o
resíduo de uma estrela que está exatamente onde deveria estar de 1,05–1,25 px
para 0,54–0,71 px, em todo tamanho de ponto e em todos os estágios. O que sobra
é a colocação sub-pixel do sprite pelo rasterizador, que nenhuma convenção
remove, e é por isso que o orçamento de posição é 1,5 px e não 0,5.

---

## 9. Decodificação sRGB

O framebuffer é **codificado em sRGB**; o shader escreveu um valor linear.
Comparar o byte guardado com o que o `core/` calculou seria comparar uma
grandeza com a sua própria função de transferência aplicada.

Antes de corrigir isto, o estágio 9 lia 0,647 onde o `core/` dizia 0,530 — a
mesma grandeza, contada uma vez a mais. Depois, lê 0,533 contra 0,530. Toda
comparação do arnês é linear, e a decodificação (IEC 61966-2-1 invertida,
tabulada sobre os 256 valores de um byte) acontece num lugar só.

---

## 10. Piso do sensor

Uma estrela cuja resposta linear codificaria abaixo de um byte não está fraca na
imagem: está **ausente** dela, e nenhuma medição a recupera. Isso não é uma
tolerância, é o sensor — e uma estrela desviada para o vermelho que se apaga em
β = 0,5 é a física funcionando.

O arnês separa os dois casos em `_why_missing()`: `atrás da câmera`, `fora do
quadro`, `abaixo do piso do sensor` (esperado) e `NÃO ENCONTRADA — acima do piso
e ausente` (defeito). Sem essa separação, o estágio 8 reprovava três estrelas por
um motivo que era o resultado correto.

---

## 11. O que este documento não afirma

- O arnês precisa de **tela**. O modo `--headless` do Godot tem um rasterizador
  falso que não desenha nada, e uma suíte que passasse nele não provaria coisa
  nenhuma — que é como o starfield atravessou um milestone inteiro quebrado.
- As medições são de **uma** GPU: Apple M5 Pro, Metal 3.2, Godot 4.5.stable. Os
  números de passo de profundidade dependem de o buffer ter 24 bits; num
  dispositivo com profundidade float de 32 bits o penhasco da seção 6 fica em
  outro lugar. O estágio 3b mede onde ele está em vez de assumir.
- Os quatro snapshots do estágio 10 são o único oráculo deste arnês que é uma
  imagem anterior em vez do `core/`. Eles são semeados deliberadamente
  (`./scripts/starfield_validation.sh --seed-references`), nunca como efeito
  colateral de uma execução: uma suíte que adota silenciosamente o que acabou de
  renderizar não pode falhar.

---

## Evidências

- Arnês: `godot/project/starfield_debug.gd`, cena `starfield_debug.tscn`
- Script: `scripts/starfield_validation.sh`
- Log e capturas: `docs/validation/starfield/`
- Referências de regressão: `docs/validation/starfield-reference/`
- Cena de produção: `docs/validation/scene/` e
  [validação visual relativística](relativistic-rendering-visual.md)
