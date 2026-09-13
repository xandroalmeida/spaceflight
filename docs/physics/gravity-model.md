# Modelo Gravitacional

Status: implementado no Milestone 0 (Newtoniano, massa pontual, N corpos)
Última revisão: 2026-09-13

## 1. Formulação implementada

Para a nave em posição `r` (frame `SSB/J2000`, metros) no instante `t` (TDB):

```
        N
a(r,t) = Σ  −GM_i · (r − r_i(t)) / |r − r_i(t)|³
       i=1
```

onde `r_i(t)` vem do `EphemerisProvider` (DE440 via SPICE) e `GM_i` vem do
kernel `gm_de440.tpc`.

Propriedades desta escolha:

* **Superposição total e permanente.** Todos os corpos habilitados contribuem em
  todo instante. Não há Sphere of Influence, não há troca de corpo central, não
  há descontinuidade artificial (§11 do enunciado).
* **Sem softening.** Nenhum `+ ε²` no denominador. Aproximação de um corpo é um
  evento físico a ser detectado e reportado, não suavizado.
* **Partícula-teste.** A nave não entra no somatório e não perturba `r_i(t)`.

## 2. Aproximação de partícula-teste: justificativa quantitativa

A aceleração que uma nave de massa `m` imprime na Terra é `G·m/d²`. Para
`m = 10⁵ kg` a `d = 10⁶ m`:

```
a_Terra = 6,674·10⁻¹¹ · 10⁵ / 10¹² ≈ 6,7·10⁻¹⁸ m/s²
```

contra a aceleração da Terra em torno do Sol, ≈ 5,9·10⁻³ m/s². A razão é
≈ 10⁻¹⁵ — abaixo do próprio erro de arredondamento do `double` na posição da
Terra. A retroação é, portanto, **inobservável dentro da precisão do modelo**, e
essa é a razão pela qual as efemérides JPL podem ser usadas como verdade
imutável (§7 e §9 do enunciado).

Domínio de validade: qualquer nave com `m ≲ 10⁹ kg`. Um corpo com massa
comparável à de um asteroide invalidaria a hipótese e exigiria N-corpos real
(aí sim, REBOUND — §29).

## 3. Corpos considerados

Conjunto padrão (`BodySet::default_solar_system()`), com os ids NAIF e a
`GM` correspondente do `gm_de440.tpc`:

| Corpo | NAIF id | GM usado | Observação |
|---|---|---|---|
| Sol | 10 | `BODY10_GM` | |
| Mercúrio (baricentro) | 1 | `BODY1_GM` | baricentro ≡ planeta na prática |
| Vênus (baricentro) | 2 | `BODY2_GM` | idem |
| Terra | 399 | `BODY399_GM` | separado da Lua |
| Lua | 301 | `BODY301_GM` | separada da Terra |
| Marte (baricentro) | 4 | `BODY4_GM` | GM **do sistema** |
| Júpiter (baricentro) | 5 | `BODY5_GM` | GM **do sistema** |
| Saturno (baricentro) | 6 | `BODY6_GM` | GM **do sistema** |
| Urano (baricentro) | 7 | `BODY7_GM` | GM **do sistema** |
| Netuno (baricentro) | 8 | `BODY8_GM` | GM **do sistema** |

Regra de consistência que o código impõe e testa: **posição de baricentro exige
GM de sistema; posição de planeta exige GM de planeta.** Usar `BODY599_GM`
(Júpiter sozinho) com a posição do baricentro 5 introduz um erro de massa de
~2·10⁻⁴ relativo; usar `BODY5_GM` com a posição de 599 introduz um erro de
posição de até ~10⁵ m. Ambos os erros são silenciosos, e por isso o catálogo
(`core/celestial/body_catalog.cpp`) fixa o par (id, GM) explicitamente.

O sistema Terra–Lua é resolvido em dois corpos separados porque a simulação opera
dentro dele; o sistema joviano não é, porque a nave (no Milestone 1) não entra
nele. Quando entrar, basta trocar `5 → 599 + luas` no `BodySet`.

## 4. Orçamento de erro: o que está e o que não está no modelo

Ordens de grandeza para uma nave em LEO (`r = 6778 km`, `h ≈ 400 km`):

| Termo | Aceleração (m/s²) | Relativo ao termo principal | Status |
|---|---|---|---|
| Terra, massa pontual | 8,7·10⁰ | 1 | **implementado** |
| Achatamento da Terra (J₂) | 1,2·10⁻² | 1,4·10⁻³ | não implementado |
| Lua (termo diferencial) | ~1,1·10⁻⁶ | 1,3·10⁻⁷ | **implementado** |
| Sol (termo diferencial) | ~5,6·10⁻⁷ | 6,4·10⁻⁸ | **implementado** |
| Arrasto atmosférico (400 km) | 10⁻⁷ … 10⁻⁵ | até 10⁻⁶ | não implementado |
| Pressão de radiação solar | ~10⁻⁷ | 10⁻⁸ | não implementado |
| Correção relativística (Schwarzschild) | ~2·10⁻⁸ | 3·10⁻⁹ | Milestone 4 |
| Harmônicos de grau > 2 | ~10⁻⁵ | 10⁻⁶ | não implementado |

**Conclusão honesta e explícita:** em órbita baixa da Terra, o modelo do
Milestone 0 é dominado pelo erro de J₂, que é ~10⁴ vezes maior que o maior termo
de terceiro corpo que já implementamos. Uma órbita de LEO propagada por este
modelo deriva em relação à realidade principalmente por causa da precessão nodal
ausente (`dΩ/dt ≈ −7°/dia` para uma órbita a 400 km e 51,6° de inclinação).

Isso **não invalida** o Milestone 0, cujo objetivo é a infraestrutura e os
corpos pontuais, mas define claramente o que os testes podem e não podem exigir:

* teste de conservação de energia/momento angular de dois corpos: **válido**, o
  modelo é exatamente kepleriano nesse caso;
* teste contra efemérides reais de um satélite terrestre: **inválido** até J₂
  existir.

Em espaço interplanetário (longe de qualquer planeta), o modelo de massas
pontuais é excelente: os termos omitidos (J₂ do Sol, ~10⁻¹¹ da aceleração
principal a 1 UA) estão abaixo do erro numérico da integração.

## 5. Extensões previstas (mesma interface `ForceModel`)

```
PointMassGravity        implementado
SphericalHarmonics      J2, depois EGM-truncado           (Milestone 1+)
RelativisticGravity     1PN / geodésica                   (Milestone 4)
SolarRadiationPressure                                    (opcional)
AtmosphericDrag                                           (opcional)
MainEngineForce                                           (Milestone 1)
RcsForce                                                  (Milestone 3)
```

Cada um entra no `CompositeForceModel` sem tocar no integrador. Nenhum deles
pode ser adicionado sem: documento, valor esperado, teste analítico
(§39 do enunciado).

## 6. Notas numéricas

* **Cancelamento.** No frame `SSB/J2000` o somatório é bem condicionado: cada
  termo é calculado a partir de uma diferença `r − r_i` cuja magnitude é a
  distância real ao corpo. A formulação alternativa (integrar relativo a um
  corpo central) exige a diferença de cubos
  `(r−r_i)/|r−r_i|³ − (r_c−r_i)/|r_c−r_i|³`, que sofre cancelamento catastrófico
  para corpos distantes e demanda a reformulação de Battin/`f(q)`. Não usamos
  essa formulação; se um dia usarmos, será com `f(q)`, nunca com a subtração ingênua.
* **Ordem de somatório.** Os corpos são somados em ordem crescente de magnitude
  de contribuição no instante da avaliação? Não: a ordem é fixa (a do `BodySet`)
  para garantir **reprodutibilidade bit-a-bit** entre execuções, que vale mais
  aqui do que o ganho marginal de precisão de somar do menor para o maior.
* **Aproximação extrema.** Se `|r − r_i| < R_i` (raio do corpo), a avaliação
  ainda é matematicamente válida (massa pontual) mas fisicamente sem sentido; o
  `ForceResult` carrega um flag de evento e o propagador o reporta. Não há clamp.

## 7. Verificação

Testes em `tests/scientific/`:

1. `tests/scientific/test_two_body.cpp` — com um único corpo fixo, a órbita
   circular fecha com período `T = 2π√(a³/GM)`; energia específica e momento
   angular se conservam; a propagação bate com a solução fechada de Kepler
   (ver `docs/validation/tolerances.md` para a origem das tolerâncias).
2. `tests/scientific/test_solar_system_gravity.cpp` — a aceleração do Sol sobre a
   Terra é `GM_☉/r²`; a superposição é exatamente a soma das parcelas; **a
   aceleração de maré da Lua e do Sol em LEO reproduz `2GMr/d³`**, o que é a
   verificação direta dos números da tabela do §4 acima.
3. `tests/unit/test_orbital_elements.cpp` — apoastro e periastro recuperados de
   um estado construído batem com `a(1±e)`; `tests/scientific/test_two_body.cpp`
   verifica o mesmo para uma elipse efetivamente propagada.
