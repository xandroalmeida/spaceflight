# Validação visual da renderização relativística

**Milestone 6.1, Parte B, seções 22 a 26.** Aberração, Doppler e beaming
verificados **numericamente** contra o `core/`, com a imagem como evidência e não
como oráculo.

**Resultado: PASS.**

Pré-requisito: o céu normal precisava funcionar primeiro, e não funcionava. Os
dois defeitos que o impediam estão em
[starfield-debug.md](starfield-debug.md). Nada nesta página foi medido antes de
os dez estágios daquele arnês passarem.

---

## 1. Ordem

A seção 22 pede o ladder isolado, e ele foi seguido literalmente. Cada linha é um
quadro do BSC5 completo em β = 0,9, com a câmera olhando para dentro do cone:

| caso | aberração | Doppler | beaming | blobs | energia |
|---|---|---|---|---:|---:|
| baseline | off | off | off | 1 714 | 84,8 |
| + aberração | **on** | off | off | 4 574 | 246,2 |
| + Doppler | off | **on** | off | 746 | 25,3 |
| + beaming | off | off | **on** | 1 899 | 5 589,6 |
| todos | on | on | on | 3 978 | 4 737,5 |

O critério do estágio é modesto de propósito: o baseline não pode estar vazio, e
cada toggle tem de mudar **alguma coisa**. É um teste de fiação, não de física —
a física é medida nas seções 3, 4 e 5 abaixo, estrela a estrela.

Vale ler as direções: a aberração empurra estrelas para dentro do cone e o número
de blobs sobe; o Doppler sozinho *reduz* a contagem, porque a maior parte do céu
está atrás e desloca-se para o vermelho para fora da banda; o beaming sozinho
multiplica a energia por 66.

---

## 2. Por que a captura de tela não é o oráculo

Uma captura comparada com uma impressão humana não é um teste. O que este
documento compara, em todos os casos, é:

```text
o que o core/ diz    <-  SpaceflightSky.get_apparent_direction()
                         SpaceflightSky.get_expected_response()
                         SpaceflightSky.get_expected_colour()

o que a GPU fez      <-  centroide ponderado do blob, desprojetado pela
                         MESMA matriz de câmera que o motor usou
```

Os acessores do lado esquerdo são novos e existem só para isto: eles chamam
`RelativisticSky::response_of()` e `PlanckTable::sample_rgb()`, em precisão
dupla, de modo que uma discordância com a GPU é uma discordância sobre o
**pipeline** e não sobre `float`.

As quatro imagens de referência da seção 26 são a única exceção, e estão
marcadas como tal.

---

## 3. Aberração — ângulo da GPU contra ângulo da CPU

Cinco estrelas sintéticas a ângulos conhecidos do eixo do boost, medidas em
`β = 0; 0,1; 0,5; 0,9; 0,99`. O orçamento é o ângulo que **dois pixels**
subtendem no eixo óptico: a medição não pode ser mais fina do que o raster, e
exigir que fosse seria medir o detector de blobs.

| β | estrelas | pior erro GPU × CPU | orçamento |
|---:|---:|---:|---:|
| 0,0 | 5/5 | 0,19873° | 0,35938° |
| 0,1 | 5/5 | 0,19597° | 0,36092° |
| 0,5 | 5/5 | 0,14829° | 0,33651° |
| 0,9 | 5/5 | 0,07235° | 0,19324° |
| 0,99 | 5/5 | 0,01875° | 0,06527° |

O caso `β = 0` é o controle: sem aberração nenhuma, o que sobra é só o erro de
medição, e ele é da mesma ordem dos outros — ou seja, a aberração não contribui
com erro algum acima do ruído do raster.

Ângulo em repouso → ângulo aparente, lido do `core/` e não da fórmula escrita
aqui:

```text
β = 0,10    30->27,25   55->50,43   80->74,40  105->99,38  130->125,46
β = 0,50    30->17,59   55->33,46   80->51,70  105->73,92  130->102,15
β = 0,90    30-> 7,04   55->13,62   80->21,79  105->33,29  130-> 52,39
β = 0,99    30-> 2,18   55-> 4,23   80-> 6,81  105->10,56  130-> 17,29
```

A manchete da seção 3 do documento de física, calculada ao vivo pelo código
embarcado: uma fonte a 90° em repouso é vista em `arccos β`.

```text
β = 0,10   ->  84,261°
β = 0,50   ->  60,000°
β = 0,90   ->  25,842°
β = 0,99   ->   8,110°
```

O estágio usa `STARFIELD_DEBUG`: a pergunta é **geométrica**, e em β = 0,99 o
céu de ré está a `e⁻⁵²` do seu fluxo de repouso. Deixar a fotometria participar
transformaria um teste de geometria num teste de a estrela estar clara o bastante
para ser achada — que é a pergunta da seção 5, não desta.

---

## 4. Doppler — cromaticidade

Três temperaturas conhecidas, cada uma a dois ângulos do eixo do boost em
`β = 0,5`: uma à frente (desviada para o azul) e uma à ré (para o vermelho). A
mesma estrela sob dois fatores de Doppler, no mesmo quadro.

Comparação em **cromaticidade** — `r/(r+g+b)` e `b/(r+g+b)` — porque o que um
desvio Doppler significa para a cor é a razão entre os canais, e separar isso do
brilho é o que permite à seção 5 testar o brilho sozinho.

| estrela | D | T' | esperado r/b | medido r/b | erro |
|---|---:|---:|---|---|---:|
| 3 000 K à frente | 1,5970 | 4 791 K | 0,426 / 0,246 | 0,425 / 0,246 | 0,0012 |
| 3 000 K à ré | 0,8660 | 2 598 K | 0,674 / 0,056 | 0,674 / 0,056 | 0,0003 |
| 5 800 K à frente | 1,5970 | 9 262 K | 0,275 / 0,420 | 0,276 / 0,419 | 0,0012 |
| 5 800 K à ré | 0,8660 | 5 023 K | 0,411 / 0,261 | 0,411 / 0,261 | 0,0002 |
| 10 000 K à frente | 1,5970 | 15 970 K | 0,222 / 0,496 | 0,222 / 0,496 | 0,0002 |
| 10 000 K à ré | 0,8660 | 8 660 K | 0,285 / 0,406 | 0,285 / 0,407 | 0,0004 |

Pior erro **0,0012** contra um orçamento de 0,04 — trinta vezes dentro. `T' = D·T`
está certo, a indexação da tabela de Planck está certa, e a interpolação linear
da textura reproduz o que `PlanckTable::sample_rgb` calcula.

---

## 5. Beaming — intensidade

Cinco estrelas em ângulos conhecidos, `β = 0,5`, aberração **desligada**: esta
seção é sobre intensidade, e mover as estrelas só as tornaria mais difíceis de
achar. O fator de Doppler não depende de onde a estrela aparece, apenas de onde
ela está.

| ângulo | D | resposta esperada | medida | erro relativo |
|---:|---:|---:|---:|---:|
| 0° | 1,7321 | 0,53032 | 0,53328 | 0,0056 |
| 45° | 1,5629 | 0,45521 | 0,45641 | 0,0026 |
| 75° | 1,3041 | 0,31510 | 0,31399 | 0,0035 |
| 90° | 1,1547 | 0,22560 | 0,22697 | 0,0061 |
| 105° | 1,0053 | 0,13963 | 0,13843 | 0,0086 |

Razões contra a estrela de 90°, que é a que o boost deixa mais perto do seu
brilho de repouso:

| razão | esperada | medida | erro relativo |
|---|---:|---:|---:|
| 0° / 90° | 2,3508 | 2,3496 | 0,0005 |
| 45° / 90° | 2,0178 | 2,0109 | 0,0034 |
| 75° / 90° | 1,3967 | 1,3834 | 0,0095 |
| 105° / 90° | 0,6189 | 0,6099 | 0,0146 |

Razão **e** valor absoluto, como duas medidas separadas, para que uma exposição
errada e um expoente errado não possam se cancelar. Pior razão 0,0146 contra 0,12;
pior absoluto 0,0086 contra 0,03.

---

## 6. Magnitude

Antes de qualquer relatividade, a seção 21 pede monotonicidade em V. Seis
estrelas sintéticas, mesma temperatura, diferindo só em magnitude:

| V | resposta esperada | medida | erro relativo |
|---:|---:|---:|---:|
| −1,0 | 0,94065 | 0,93869 | 0,0021 |
| 0,0 | 0,86319 | 0,86316 | 0,0000 |
| +1,0 | 0,71525 | 0,71569 | 0,0006 |
| +3,0 | 0,28475 | 0,28315 | 0,0056 |
| +5,0 | 0,05935 | 0,05951 | 0,0027 |
| +6,0 | 0,02450 | 0,02416 | 0,0141 |

Estritamente decrescente, e o valor concorda com o `core/` dentro de 1,4 % no pior
degrau — que é o mais fraco, onde um passo de um byte já vale mais de 1 %.
Monotonicidade sozinha não bastaria: uma escada pode ser monótona e estar errada.

---

## 7. A cena de produção

O arnês prova que o **shader** está certo e não pode provar que a **cena** está:
o plano próximo, a colocação da câmera e as malhas dos corpos são de `main.gd`, e
a única evidência honesta sobre elas é uma fotografia delas.

```bash
SPACEFLIGHT_CAPTURE="$PWD/docs/validation/scene" \
  external/godot/Godot.app/Contents/MacOS/Godot --path godot/project
```

A cena percorre o ladder de β com os **mesmos** setters que o teclado usa, e
fotografa duas direções por degrau. As duas direções são duas afirmações
diferentes: para a frente mostra o cone e o desvio para o azul; para trás mostra
um céu que de fato se apagou. Uma captura que só olhasse para um lado não
distinguiria um céu de ré escuro de um quebrado.

| arquivo | β | olhando | o que mostra |
|---|---:|---|---|
| `scene_forward_beta_propagated.png` | 1,0e-4 | frente | céu em repouso, 8 786 estrelas |
| `scene_forward_beta_0p1.png` | 0,10 | frente | cone de 84,3°, D = 1,106 |
| `scene_forward_beta_0p5.png` | 0,50 | frente | cone de 60,0°, D = 1,732 |
| `scene_forward_beta_0p9.png` | 0,90 | frente | cone de 25,8°, D = 4,359 |
| `scene_forward_beta_0p99.png` | 0,99 | frente | cone de 8,1°, D = 14,107 |
| `scene_aft_beta_0p9.png` | 0,90 | ré | céu apagado: 5,3e-7 do fluxo de repouso |
| `scene_aft_beta_0p99.png` | 0,99 | ré | céu apagado: 2,2e-23 do fluxo de repouso |

A imagem `scene_forward_beta_0p9.png` é a que o Milestone 6 não conseguiu
produzir: em β = 0,9, olhando para dentro do cone, o céu empilhou-se num disco
azul brilhante centrado na direção do movimento.

O número de estrelas dentro do cone para a frente vale a pena registrar, porque
ele é uma verificação de consistência e não uma medida do render: o cone de
meio-ângulo `arccos β` tem fração de ângulo sólido `(1 − β)/2`, então um céu
uniforme põe exatamente metade das suas fontes dentro dele em **qualquer** β. A
cena reporta 50,17 % em β = 0,1; 0,5; 0,9 e 0,99, contra 49,49 % em β = 0 — a
diferença sendo a anisotropia real do BSC5, que não é um céu uniforme.

---

## 8. Regressão por snapshot

Quatro imagens do BSC5 completo, atitude fixa a 40° do eixo do boost, para que o
que muda entre elas seja a **física** e não a câmera:

```text
β = 0      1 176 pixels acesos
β = 0,5    3 435 pixels acesos
β = 0,9   13 604 pixels acesos
β = 0,99  23 184 pixels acesos
```

A métrica não é igualdade pixel a pixel — uma GPU diferente, um driver diferente
e uma regra de rasterização diferente movem um sprite de ponto por uma fração de
pixel. É a diferença absoluta média de uma redução para 64 × 64, insensível
exatamente a isso e sensível a uma estrela que se moveu, sumiu ou mudou de cor.
Orçamento 0,02; a execução de verificação mediu 0,00000 nos quatro.

Semear as referências é um ato deliberado
(`./scripts/starfield_validation.sh --seed-references`), nunca um efeito
colateral: uma suíte que adota o que acabou de renderizar não pode falhar.

---

## 9. O que continua fora

- **Tempo retardado** tem toggle e centro ativos; a inspeção diferencial de
  silhueta continua pendente, como no Milestone 6.
- **Objetos extensos** (`docs/physics/extended-object-aberration.md`): falta a
  comparação CPU/shader e a medição de custo perto da silhueta.
- Tudo foi medido numa GPU só: Apple M5 Pro, Metal 3.2, Godot 4.5.stable.

Nenhum desses bloqueia a validação óptica relativística, que era o bloqueador
P0 e está fechado.

---

## Evidências

- Arnês: `godot/project/starfield_debug.gd` — estágios 6 a 10
- Log: `docs/validation/starfield/starfield-validation.log`
- Capturas do arnês: `docs/validation/starfield/`
- Capturas da cena: `docs/validation/scene/`
- Referências: `docs/validation/starfield-reference/`
- Diagnóstico e correções: [starfield-debug.md](starfield-debug.md)
