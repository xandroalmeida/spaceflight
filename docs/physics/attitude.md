# Atitude: Rotação de Corpo Rígido

Status: **implementado** (Milestone 3)
Convenções: ADR-0008
Última revisão: 2026-09-13

## 1. Estado

```
q   quaternion unitário, corpo → inercial   (adimensional, 4 componentes)
ω   velocidade angular no referencial do corpo   [rad/s]
I   tensor de inércia no referencial do corpo    [kg m²]
```

`I` é propriedade da nave, não estado: muda quando a configuração muda (tanque
esvaziando, painel girando), e no Milestone 3 é constante.

## 2. Equações

**Cinemática** — como a orientação segue a velocidade angular:

```
q̇ = ½ q ⊗ (0, ω)
```

com `⊗` o produto de Hamilton e `ω` no referencial do corpo. Note a ordem: com a
convenção corpo→inercial de ADR-0008, `ω` multiplica **pela direita**. Trocar a
ordem produz uma rotação que gira para o lado certo em torno do eixo errado — é o
erro mais comum desta área e o teste `quaternion_kinematics_match_finite_rotation`
existe exatamente para pegá-lo.

**Dinâmica** — equações de Euler, no referencial do corpo:

```
I ω̇ + ω × (I ω) = τ
⇒  ω̇ = I⁻¹ ( τ − ω × (I ω) )
```

O termo `ω × (I ω)` é o giroscópico: ele não faz trabalho (é ortogonal a `ω`),
mas é o responsável por toda a riqueza do movimento livre de torque — precessão,
nutação e a instabilidade do eixo intermediário (§4).

As equações são escritas no **corpo** porque é lá que `I` é constante. No
referencial inercial `I` giraria com a nave e a equação teria um termo a mais a
cada passo.

## 3. O tensor de inércia é validado, não aceito

`I` precisa ser simétrico e positivo-definido, e seus momentos principais têm de
obedecer às **desigualdades triangulares**:

```
I₁ + I₂ ≥ I₃        I₁ + I₃ ≥ I₂        I₂ + I₃ ≥ I₁
```

Isso não é formalidade: um tensor que as viola não corresponde a nenhuma
distribuição de massa real, e a dinâmica resultante conserva energia e momento
angular enquanto faz coisas que nenhum corpo faz. O construtor recusa.

Conversão de conveniência para formas comuns (caixa, cilindro, esfera) existe em
`core/attitude/inertia.hpp`, e cada uma vem com a fórmula no comentário.

## 4. Consequências verificáveis (os testes)

### 4.1 Invariantes exatos, sem torque

```
L_inercial = R(q) I ω      conservado exatamente (vetor)
T = ½ ω·(I ω)              conservado exatamente (escalar)
```

Note que `|ω|` **não** é conservado, e `ω` no corpo **não** é constante: apenas as
duas grandezas acima são. Um teste que verificasse `|ω| = const` passaria para uma
esfera e mascararia um tensor errado.

### 4.2 Pião simétrico: precessão com fórmula fechada

Com `I₁ = I₂ = I_a ≠ I₃ = I_c` e sem torque, a componente transversal de `ω` gira
em torno do eixo de simetria **no referencial do corpo** com taxa

```
Ω_corpo = ω₃ (I₃ − I₁) / I₁
```

Para `I_a = 1500`, `I_c = 2400 kg m²`, `ω₃ = 0,05 rad/s`:

```
Ω = 0,030000 rad/s        período 209,44 s
```

Verificação analítica direta, sem número mágico.

### 4.3 Teorema do eixo intermediário (efeito Dzhanibekov)

Com `I₁ < I₂ < I₃`, a rotação em torno dos eixos **extremos** é estável e em torno
do **intermediário** é instável, com taxa de crescimento exponencial

```
λ = ω₂ √( (I₂ − I₁)(I₃ − I₂) / (I₁ I₃) )
```

Para `I = (1200, 1800, 2400) kg m²` e `ω₂ = 0,05 rad/s`:

```
λ = 0,017678 s⁻¹          tempo de e-folding 56,57 s
```

Este é o teste mais forte do módulo: quase qualquer erro no termo giroscópico ou
no tensor mata a instabilidade ou lhe dá a taxa errada, e **nenhum** erro a produz
por acaso.

## 5. A norma do quaternion: por que projetar aqui é honesto e clampar `v < c` não é

A integração numérica tira `q` da esfera unitária. Renormalizamos a cada passo
aceito. Isso é uma **projeção**, e este projeto proíbe clamps (§13). A distinção
não é de conveniência:

* `|q| = 1` é um **vínculo de parametrização**, não um limite físico. A rotação
  vive em SO(3); os quaternions unitários são uma cobertura dupla dele. Um `q` com
  norma 1,0000001 não descreve "uma rotação um pouco proibida" — descreve a mesma
  rotação, escrita com um fator de escala espúrio que **não tem significado**.
  Renormalizar é a identidade sobre a variedade de vínculo.
* `|v| < c` é um **limite físico**. Um estado com `v > c` não é a mesma física mal
  escrita: é física errada, e chegou ali por erro de integração. Clampar apaga a
  evidência de que o passo estava errado.

Mesmo assim, a deriva é **medida** e não presumida: o propagador registra
`max |‖q‖ − 1|` antes de cada renormalização. Se esse número crescer, é sinal de
que o passo está grande demais — informação que o clamp destruiria. Medido em
órbita com RCS ativo e `rtol = 10⁻¹²`: da ordem de 10⁻¹³ por passo.

## 6. RCS: força **e** torque

§19: *"O RCS gera force e torque"*. Um propulsor em `r_corpo` apontando para
`d̂_corpo` com empuxo `F` produz

```
força_corpo  = F d̂
torque_corpo = r × (F d̂)
```

e a força entra na translação depois de rotacionada para o inercial:
`força_inercial = R(q) força_corpo`.

Consequências que o modelo entrega de graça e que um "gira a nave" não entregaria:

* **empurrão parasita**: girar gasta `Δv` de translação, a menos que os
  propulsores estejam em pares acoplados. O modelo mostra o quanto;
* **torque parasita**: um propulsor de translação desalinhado com o centro de
  massa gira a nave;
* o propelente sai do mesmo tanque, pelo mesmo `F = η q w`
  (`docs/physics/propulsion-model.md`). Um RCS gratuito seria um `fuel -= 0`.

O centro de massa é a origem do referencial do corpo, por definição. Quando o
tanque esvaziar e o centro de massa se mover (Milestone 4+), é `r` de cada
propulsor que muda, não a equação.

## 7. Apontamento (§28)

`PointingController` faz o controle PD sobre o **erro de quaternion**:

```
q_erro = q_alvo* ⊗ q_atual          (rotação do alvo para o atual)
τ = −K_p · sinal(w_erro) · vec(q_erro) − K_d · ω
```

O `sinal(w_erro)` é o que evita o caminho longo: `q` e `−q` são a mesma
orientação, e sem ele a nave às vezes gira 300° para chegar onde 60° bastavam.

Modos implementados: `PROGRADE`, `RETROGRADE`, `NORMAL`, `ANTI_NORMAL`,
`RADIAL_IN`, `RADIAL_OUT` — os mesmos alvos que `ManeuverExecutor` já calcula, o
que evita duas definições de "prograde" no mesmo programa. `POINT_TARGET` e
`MATCH_VELOCITY` precisam de um alvo de navegação e vêm depois.

O controlador **pede torque**; quem o produz é o RCS, dentro dos limites dele. Um
apontamento que o RCS não consegue cumprir fica lento, não instantâneo.

### 7.1 O erro residual não é folga: é atraso de rastreio

Um PD é um controlador **tipo 0**, e "prograde" não é um alvo parado — ele gira à
taxa orbital `n`. Manter o nariz apontado exige `ω = n`, e aí o termo derivativo
pede `−2ζω_n n` de torque, que só um erro proporcional permanente pode fornecer:

```
θ_atraso = 2 ζ n / ω_n
```

Em órbita baixa (`n = 1,1314·10⁻³ rad/s`) com `ζ = 1` e `ω_n = 0,05 rad/s`:

```
previsto  2,5930°        medido  2,5932°
```

Quatro dígitos. Não é defeito do RCS nem granularidade dos propulsores — é o que
um PD faz seguindo uma rampa. Zerar isso exige **feed-forward** da taxa do alvo
(fácil: a derivada da direção de guiamento é conhecida) ou um termo **integral**
(que introduz *windup* e precisa de saturação). Nenhum dos dois está implementado,
e o teste `the_pointing_controller_slews_to_prograde_and_stops_there` verifica a
fórmula acima em vez de exigir zero.

## 8. Fora de escopo

Sem rodas de reação, sem *control moment gyros*, sem gradiente de gravidade, sem
torque magnético, sem pressão de radiação sobre painéis, sem flexibilidade
estrutural, sem *sloshing* de propelente. Cada um desses é um termo a mais em `τ`
e nenhum deles muda a arquitetura — entram pela mesma porta por onde o RCS entrou.
