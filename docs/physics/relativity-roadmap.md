# Roteiro de Relatividade

Status: **planejamento**. Nada deste documento está implementado no Milestone 0.
Implementação: Milestone 4 (propulsão) e além, e somente após
`docs/physics/relativistic-propulsion.md` estar consistente.
Última revisão: 2026-09-13

Este documento fixa a **formulação alvo** para que as escolhas feitas agora
(variáveis de estado, interfaces, tempo próprio separado) não precisem ser
desfeitas depois. Ele não autoriza escrever código relativístico.

---

## 1. O que está proibido

Registrado primeiro, porque é a parte mais importante:

1. **Proibido** calcular `γ` a partir de um movimento newtoniano e usá-lo como
   verniz sobre o resultado.
2. **Proibido** qualquer forma de `if (v > c) v = c;`. O limite `|v| < c` deve ser
   uma consequência estrutural da representação escolhida (§3), não um clamp.
3. **Proibido** inventar equações híbridas ("gravidade newtoniana + correção γ
   ad hoc"). Qualquer aproximação precisa de domínio de validade, erro esperado
   e hipóteses explícitas.
4. **Proibido** assumir que uma expansão 1PN válida para planetas continua
   válida para uma nave com `v → c` (§15 e §29 do enunciado). Ver §5.

---

## 2. Separação dos problemas

| Problema | Tratamento | Regime |
|---|---|---|
| Movimento dos planetas | efemérides DE440 (que já embutem a dinâmica EIH 1PN usada pelo JPL) | `v/c ~ 10⁻⁴` |
| Nave em baixa velocidade | gravidade newtoniana de N massas pontuais | `v/c ≲ 10⁻³` |
| Correções gravitacionais | módulo `RelativisticGravity` separado | `v/c ≲ 10⁻¹` |
| Nave em regime relativístico | geodésica exata em métrica de campo fraco | `v/c → 1` |
| Cinemática/propulsão relativística | RE especial exata (espaço plano) | `v/c → 1` |

Nenhuma dessas caixas pode vazar para a outra sem documento.

---

## 3. Representação de estado (decisão central)

A variável de estado da nave **não** será a velocidade coordenada `v`. Será a
parte espacial da 4-velocidade, também chamada *velocidade própria*:

```
u ≡ γ v = p / m₀            [m/s]
```

Estado completo da nave:

```
r        posição              (SSB/J2000, m)
u        γv                   (m/s)
m₀       massa de repouso     (kg)
τ        tempo próprio        (s)
q        orientação           (quaternion, Milestone 3)
ω        velocidade angular
```

com `t` (tempo coordenado TDB) como variável independente da integração.

### 3.1 Por que `u` e não `v`

**Razão estrutural.** A recuperação de `v` a partir de `u` é

```
v = u / √(1 + |u|²/c²)
```

Para **qualquer** `u` finito, `|v| < c` identicamente. O limite de velocidade não
é imposto: ele é a própria forma da função. Nenhum clamp é necessário porque
nenhum `u` alcançável produz `|v| ≥ c`. Erro numérico pode deixar `v` próximo de
`c`, nunca acima.

Se a variável de estado fosse `v`, o integrador poderia perfeitamente produzir
`|v| > c` como excursão numérica, e aí a única "solução" seria o clamp proibido.

**Razão numérica.** Com `v` como variável,

```
γ = 1/√(1 − β²)
```

sofre cancelamento catastrófico quando `β → 1`: em `β = 0,999999`, `1 − β²` perde
~12 dígitos significativos. Com `u` como variável,

```
γ = √(1 + |u|²/c²)
```

é uma soma de termos positivos — **zero cancelamento**, precisão relativa
mantida em qualquer `β`. Esse é o mesmo motivo pelo qual códigos de plasma
relativístico (PIC) guardam `u` e não `v`.

**Razão física.** `p = m₀u` é o que a força integra: `dp/dt = F`. Integrar `u` é
integrar a lei de Newton na sua forma relativisticamente correta.

### 3.2 Equações (relatividade especial, espaço plano, massa constante)

```
dr/dt = v = u / √(1 + |u|²/c²)
du/dt = F/m₀                       (F = força coordenada 3-dimensional)
dτ/dt = 1/γ = 1/√(1 + |u|²/c²)
```

Com massa variável (propulsão), `du/dt` ganha o termo de `dm₀/dt`; a derivação
correta pertence a `docs/physics/relativistic-propulsion.md`, não a este documento.

### 3.3 Limite newtoniano

Para `|u| ≪ c`: `γ → 1`, `v → u`, `dτ/dt → 1`, e as três equações acima colapsam
exatamente em `dr/dt = v`, `dv/dt = F/m`, `τ = t`. O teste §31 ("limite
newtoniano") verifica essa convergência numericamente, não por inspeção.

---

## 4. Invariante de verificação

Em qualquer ponto da integração, a 4-velocidade deve satisfazer

```
g_μν u^μ u^ν = −c²
```

Em espaço plano isso é `γ²c² − |u|² = c²`, identicamente satisfeito pela
construção de §3.1 — portanto, em RE, o invariante verifica a *aritmética*, não a
física. Em espaço curvo (§5) ele é uma verificação real e será monitorado a cada
passo como diagnóstico. **Diagnóstico, não correção:** se o invariante derivar
além da tolerância, a resposta é reduzir o passo ou reportar falha, jamais
reprojetar o estado silenciosamente.

---

## 5. Gravidade no regime relativístico

### 5.1 O erro a evitar

As equações EIH (Einstein–Infeld–Hoffmann), usadas pelo JPL para gerar as DE e
disponíveis no REBOUNDx como `gr`, `gr_full`, `gr_potential`, são uma expansão
**pós-newtoniana**: uma série em `(v/c)²` e em `(GM/rc²)` simultaneamente, com
esses dois pequenos parâmetros tratados como da mesma ordem. Para planetas isso
é excelente (`v/c ~ 10⁻⁴`, `GM_☉/rc² ~ 10⁻⁸`).

Para uma nave com `v/c = 0,9`, o parâmetro de expansão `(v/c)² = 0,81` **não é
pequeno**: a série 1PN não converge de forma útil e os termos truncados são da
ordem dos termos mantidos. Usar REBOUNDx `gr` para uma nave relativística seria
usar uma ferramenta fora do seu domínio de validade — exatamente o que §29 do
enunciado proíbe.

### 5.2 A formulação correta

A assimetria do problema é o que o resolve:

* as **fontes** (Sol, planetas) são lentas (`v/c ~ 10⁻⁴`) e o campo é fraco
  (`|Φ|/c² ≲ 10⁻⁶` até junto ao Sol, `~10⁻⁹` junto à Terra);
* a **partícula-teste** é rápida, mas é uma partícula-teste: não gera campo.

Portanto: expandir a **métrica** em campo fraco/fontes lentas (onde a expansão é
legítima) e integrar a **equação da geodésica exata** para a nave (sem expandir
em `v_nave/c`, onde a expansão seria ilegítima).

Métrica de campo fraco em gauge harmônico, com potencial newtoniano
`U = Σ GM_i/|r − r_i|` (note o sinal: `U > 0`) e potencial vetor
`U^i = Σ GM_i v_i^i/|r − r_i|`:

```
g₀₀ = −(1 − 2U/c² + 2U²/c⁴)
g₀ᵢ = −4Uᵢ/c³
gᵢⱼ =  δᵢⱼ (1 + 2U/c²)
```

Equação do movimento, parametrizada por tempo coordenado (`u^μ = dx^μ/dτ`):

```
du^i/dt = −Γ^i_{μν} (u^μ u^ν)/u⁰  +  u^i (Γ⁰_{μν} u^μ u^ν)/u⁰
dτ/dt   = 1/u⁰ · c   (com u⁰ obtido do vínculo g_μν u^μ u^ν = −c²)
```

Propriedades desta formulação:

* é **exata em `v_nave`** — vale de `v = 0` a `v → c` sem mudar de fórmula;
* reduz-se à gravidade newtoniana quando `v ≪ c` e `U ≪ c²`;
* reproduz os testes clássicos da RG (precessão do periélio, deflexão da luz,
  atraso de Shapiro) porque a métrica está correta a 1PN;
* preserva `|v| < c` estruturalmente (a geodésica é sempre tipo-tempo).

**Domínio de validade declarado:** `|U|/c² ≪ 1` e `v_fonte/c ≪ 1`. Erro relativo
esperado `O(U²/c⁴) ~ 10⁻¹²` junto ao Sol, `O(10⁻¹⁸)` junto à Terra. **Não vale**
perto de um objeto compacto (anã branca, estrela de nêutrons, buraco negro) — o
que não está no escopo deste simulador.

### 5.3 Efeito de arrasto por ordenação temporal

`r_i(t)` vem das efemérides no instante `t` — isto é, a métrica é construída com
as posições *simultâneas* no gauge adotado, não retardadas. Essa é a mesma
aproximação usada na formulação 1PN padrão e é consistente até a ordem mantida.
Registrado porque é o tipo de detalhe que parece bug quando encontrado depois.

---

## 6. Fenômenos a implementar, em ordem

| # | Item | Onde | Teste analítico disponível |
|---|---|---|---|
| 1 | fator de Lorentz a partir de `u` | `core/relativity` | identidades algébricas |
| 2 | momento e energia relativísticos | `core/relativity` | `E² = (pc)² + (m₀c²)²` |
| 3 | tempo próprio (velocidade constante) | propagador | `τ = t/γ`, fechado |
| 4 | composição de velocidades | `core/relativity` | associatividade, limites |
| 5 | aceleração própria vs. coordenada | propulsão | movimento hiperbólico |
| 6 | transformação entre referenciais (boost) | `core/relativity` | invariância de `s²` |
| 7 | equação do foguete relativístico | propulsão | `Δφ = (w/c)·ln(m₀/m₁)` |
| 8 | dilatação gravitacional do tempo | geodésica | redshift GPS: 45,7 µs/dia |
| 9 | precessão do periélio | geodésica | Mercúrio: 42,98″/século |
| 10 | atraso de Shapiro | geodésica | valor clássico Terra–Vênus |
| 11 | Doppler relativístico | renderização | `f'/f = √((1−β)/(1+β))` |
| 12 | aberração | renderização | `cos θ' = (cos θ − β)/(1 − β cos θ)` |
| 13 | beaming, Terrell | renderização | Milestone 5 |

Os itens 8–10 são valiosos não por gameplay, mas porque são **benchmarks reais**:
a precessão de Mercúrio é uma verificação fim-a-fim implacável do módulo de
gravidade relativística, comparável contra um número medido.

---

## 7. Escada de validação (§31 do enunciado)

Cada nível é um teste automatizado, com tolerância justificada:

```
β = 0,01    γ − 1 = 5,0·10⁻⁵      limite newtoniano deve coincidir em 5 dígitos
β = 0,1     γ = 1,005037815…
β = 0,5     γ = 1,154700538…
β = 0,9     γ = 2,294157339…
β = 0,99    γ = 7,088812050…
β = 0,999   γ = 22,36627204…
```

Para cada nível: (a) propagar movimento livre e comparar contra solução
analítica; (b) propagar aceleração própria constante e comparar contra o
movimento hiperbólico exato; (c) verificar `τ` contra a forma fechada; (d)
verificar que `|v| < c` nunca é violado sem nenhum clamp no caminho.

---

## 8. Impacto no Milestone 0

O único compromisso assumido agora, e a razão de este documento existir antes do
código:

* `PropagationState` já carrega `proper_time` como variável de estado;
* `CoordinateTime` tem resolução suficiente para `β → 1` (ver
  `docs/architecture/coordinate-system.md` §5);
* a interface `ForceModel` devolve **força/aceleração**, o que permite trocar a
  lei de movimento sem trocar os modelos de força;
* o propagador é genérico sobre o vetor de estado, de modo que trocar `v` por `u`
  é uma mudança de `PropagationState` e de uma função de derivada — não uma
  reescrita.
