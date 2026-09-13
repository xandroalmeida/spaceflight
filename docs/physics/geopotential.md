# Achatamento e Geopotencial (J₂)

Status: **implementado** (apenas o termo zonal J₂)
Modelo pai: `docs/physics/gravity-model.md`
Última revisão: 2026-09-13

## 1. Por que este termo, e por que primeiro

A tabela de erro em `gravity-model.md` §4 é explícita: em órbita terrestre baixa
o achatamento da Terra vale ~1,25·10⁻² m/s², enquanto o maior termo de terceiro
corpo que já tínhamos (a maré lunar) vale ~1,1·10⁻⁶ m/s². J₂ é **10⁴ vezes
maior** que tudo o que o Milestone 0 acrescentou ao termo de massa pontual.

Sem ele, nenhuma órbita terrestre bate com a realidade por mais de algumas horas:
o efeito dominante que falta é a regressão nodal de ≈ −5°/dia, que em um dia
desloca o plano orbital em milhares de quilômetros.

## 2. O potencial

Para um corpo com simetria axial, o potencial externo em coordenadas esféricas
(latitude `φ` medida a partir do plano equatorial do corpo) é

```
            GM  ⎡      ∞              ⎛ R ⎞ⁿ            ⎤
U(r, φ) =  ──── ⎢ 1 −  Σ   Jₙ         ⎜ ─ ⎟   Pₙ(sin φ) ⎥
             r  ⎣     n=2             ⎝ r ⎠             ⎦
```

com `Pₙ` os polinômios de Legendre e `R` o **raio de referência ao qual os `Jₙ`
pertencem** (ver §5 — isto é uma armadilha real). Mantemos apenas `n = 2`:

```
P₂(u) = (3u² − 1)/2
```

Termos zonais ímpares (`J₃`, a assimetria norte–sul), zonais pares superiores
(`J₄`) e os termos tesserais/setoriais (dependentes da longitude, que produzem o
efeito de ressonância geossíncrona) **não** estão implementados; ver §7.

## 3. Aceleração, na forma que o código usa

A forma cartesiana clássica, no referencial fixo ao corpo com `z` ao longo do
eixo de figura:

```
        3 J₂ GM R²  ⎡                                                    ⎤
a = − ───────────── ⎢ x(1 − 5z²/r²),  y(1 − 5z²/r²),  z(3 − 5z²/r²)      ⎥
            2 r⁵    ⎣                                                    ⎦
```

Escrevendo `n̂` para o versor do polo e `s = r·n̂` (a coordenada ao longo do eixo),
essa expressão se reagrupa em uma forma **sem referencial fixo ao corpo**:

```
        3 J₂ GM R²  ⎡ ⎛      s² ⎞               ⎤
a = − ───────────── ⎢ ⎜ 1 − 5── ⎟ r⃗  +  2 s n̂  ⎥
            2 r⁵    ⎣ ⎝      r² ⎠               ⎦
```

Derivação: decomponha `r⃗ = (r⃗ − s n̂) + s n̂`. A componente perpendicular ao eixo
recebe o fator `(1 − 5s²/r²)` e a paralela recebe `(3 − 5s²/r²)`; a diferença
entre os dois fatores é exatamente `2`, o que produz o termo `2 s n̂`.

### 3.1 A consequência que simplifica tudo

O potencial é **axialmente simétrico**: a rotação do corpo em torno do próprio
eixo não altera o campo. Portanto **não precisamos da matriz de rotação completa
para o referencial fixo ao corpo** — precisamos apenas da **direção do polo** no
referencial de integração.

Isso importa por três razões:

1. evita transformar posição e aceleração para dentro e para fora de um
   referencial girante a cada avaliação de força (7 vezes por passo);
2. evita introduzir um referencial não inercial no caminho da integração, o que
   `docs/architecture/coordinate-system.md` §2 proíbe;
3. o ângulo de rotação (que muda ~15°/hora e exige o kernel de orientação de alta
   precisão para ser exato) simplesmente **não entra na resposta**. Apenas o
   polo, que se move ~50″/ano, entra.

O polo vem do SPICE (`cidfrm_c` para descobrir o referencial fixo ao corpo,
`pxform_c` para obter a rotação até `J2000`, aplicada a `ẑ`), nunca de uma
fórmula de precessão escrita à mão.

## 4. Magnitudes (r = 6778 km, LEO)

| Grandeza | Valor |
|---|---|
| massa pontual, `GM/r²` | 8,6763 m/s² |
| J₂ no equador (para **dentro**) | 1,2476·10⁻² m/s² |
| J₂ no polo (para **fora**) | 2,4953·10⁻² m/s² |
| razão J₂/pontual no equador | 1,438·10⁻³ |

O sinal é um teste em si: no polo, a gravidade é **mais fraca** que a de uma massa
pontual equivalente (a massa extra do bojo equatorial está mais longe), logo a
perturbação aponta para fora; no equador ocorre o contrário. Um erro de sinal
aqui inverte a precessão nodal e é imediatamente visível.

## 5. Constantes: a exceção documentada

`docs/adr/0003-ephemeris.md` estabelece que constantes que um kernel fornece não
são duplicadas no código. **Os kernels carregados não contêm J₂** — verificado:
`pck00011.tpc` traz raios e orientação, `gm_de440.tpc` traz apenas `GM`. Nenhum
`BODY399_J2` existe no pool.

Portanto J₂ é uma constante do código, em `core/celestial/oblateness.cpp`, com
proveniência explícita por corpo:

| Corpo | J₂ | R de referência | Fonte |
|---|---|---|---|
| Terra | 1,0826266835·10⁻³ | 6 378 136,3 m | EGM96, `J₂ = −√5 · C̄₂₀`, `C̄₂₀ = −4,841 653 717·10⁻⁴` |
| Lua | 2,0323·10⁻⁴ | 1 738 000 m | GRGM1200A / valor clássico LP150Q |
| Marte | 1,955 45·10⁻³ | 3 396 190 m | GMM-3, raio equatorial IAU |

⚠️ **`J₂` e `R` formam um par indivisível.** O que tem significado físico é o
produto `J₂ R²`; citar o `J₂` do EGM96 (que se refere a `R = 6378136,3 m`) junto
com o raio do WGS84 (`6378137,0 m`) introduz um erro relativo de 2,2·10⁻⁷ no
termo — pequeno, mas gratuito e invisível. O código armazena os dois juntos, em
uma única estrutura, e nunca aceita um sem o outro.

Se um dia um kernel de geopotencial for carregado (NAIF distribui alguns), a
fonte muda para o kernel e esta tabela é removida, não mantida em paralelo.

## 6. Consequências verificáveis (os testes)

### 6.1 Invariante exato: componente do momento angular ao longo do polo

Um potencial axialmente simétrico não exerce torque em torno do eixo de simetria.
Logo

```
L_z ≡ (r⃗ × v⃗) · n̂   é conservado exatamente
```

enquanto `|L⃗|` **não** é. Este é o teste mais forte do módulo: distingue "J₂
implementado" de "algum termo perturbador implementado", porque quase qualquer
erro na fórmula quebra a simetria axial.

(Rigorosamente, `n̂` se move ~50″/ano por precessão do polo, o que ao longo de
horas é indistinguível de zero.)

### 6.2 Regressão nodal secular

Primeira ordem em `J₂`, para órbita quase circular:

```
dΩ/dt = − (3/2) n J₂ (R/p)² cos i          p = a(1 − e²)
```

Para `a = 6778 km`, `i = 51,6°`, `e ≈ 0` (a órbita da ISS):

```
n       = 1,131 401·10⁻³ rad/s
dΩ/dt   = −1,010 569·10⁻⁶ rad/s = −5,0027 °/dia
```

O valor observado para a ISS é ≈ −5,0 °/dia. A concordância da fórmula com a
realidade é o que justifica usá-la como referência para o nosso integrador.

### 6.3 Precessão do argumento do periastro

```
dω/dt = (3/4) n J₂ (R/p)² (5cos²i − 1) = +7,558 126·10⁻⁷ rad/s = +3,7415 °/dia
```

Muda de sinal na inclinação crítica `i = 63,4°` (`5cos²i = 1`), que é a razão de
existirem as órbitas Molniya. Um teste na inclinação crítica confirma que o
código reproduz a mudança de sinal — coisa que um modelo com o `J₂` errado
raramente acerta.

### 6.4 Casos-limite

* `J₂ = 0` ⇒ aceleração identicamente nula;
* no equador (`s = 0`), `|a| = (3/2) J₂ GM R²/r⁴`, dirigida para `−r̂`;
* no polo (`s = r`), `|a| = 3 J₂ GM R²/r⁴`, dirigida para `+r̂`;
* campo distante: a razão `(3/2) J₂ (R/r)²` cai como `r⁻²` — na distância lunar
  o J₂ terrestre vale 4,5·10⁻⁷ da atração pontual, e a 1 UA é irrelevante.

## 7. Domínio de validade e o que continua faltando

Válido para `r > R` (fora do corpo), que é onde uma nave está. O erro do modelo é
a soma dos termos omitidos; para a Terra em LEO, em fração da aceleração
principal:

| Termo omitido | Ordem de grandeza |
|---|---|
| `J₃` (assimetria N–S) | ~2,5·10⁻⁶ |
| `J₄` | ~1,6·10⁻⁶ |
| tesserais de grau 2–4 (dependem da longitude) | ~10⁻⁶ |
| marés sólidas e oceânicas | ~10⁻⁸ |
| arrasto atmosférico (400 km) | 10⁻⁸ … 10⁻⁶ (variável com a atividade solar) |
| pressão de radiação solar | ~10⁻⁸ |

Ou seja: **com J₂, o próximo termo relevante em LEO passa a ser o arrasto**, cuja
modelagem depende de área, atitude, coeficiente e clima espacial — e por isso
pertence a um documento próprio, não a este.

O caminho de extensão é o mesmo `ForceModel`: `SphericalHarmonicsGravity` com
grau/ordem configuráveis lendo um arquivo de coeficientes. A interface não muda.

## 8. Aviso de interpretação (para o cockpit, Milestone 3)

Com J₂ ligado, os elementos orbitais osculadores **oscilam com período orbital**,
com amplitude relativa da ordem de `J₂ (R/p)² ≈ 10⁻³`. Um mostrador de apoastro
que exiba o valor osculador instantâneo vai tremer alguns quilômetros a cada
órbita, e isso é **correto**, não um bug: é a diferença entre elementos
osculadores e elementos médios. Quando um mostrador estável for necessário, a
resposta é exibir elementos médios (Brouwer–Lyddane), documentando a conversão —
nunca filtrar o valor até ele parecer bonito.
