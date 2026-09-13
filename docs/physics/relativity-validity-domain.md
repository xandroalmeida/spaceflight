# Domínio de validade da dinâmica relativística

Status: **normativo para o Milestone 6**  
Última revisão: 2026-09-13

Este documento define o que os modos relativísticos calculam. O nome anterior
`GeneralRelativistic` foi removido porque prometia mais do que o código fazia. O
modo gravitacional chama-se agora `Kinematics::WeakFieldStaticMetric`.

## Modelo implementado

Há dois modelos distintos:

1. `SpecialRelativistic`: espaço-tempo de Minkowski; estado translacional
   `u=γv`; empuxo como força própria; sem gravidade.
2. `WeakFieldStaticMetric`: partícula-teste em coordenadas cartesianas
   conformemente isotrópicas, com fontes pontuais quase estáticas:

```text
ψ = U/c²,                 U = Σ GM_a/|x-x_a| > 0
g00 = -(1 - 2ψ + 2ψ²)
g0i = 0
gij = (1 + 2ψ) δij
```

A métrica coincide com os termos escalares usuais do limite pós-newtoniano em
GR: `g00` é retido até `O(c⁻⁴)` e `gij` até `O(c⁻²)`. Isto **não é um modelo 2PN**:
os termos `O(c⁻⁴)` de `gij`, os potenciais dependentes da velocidade/pressão e
os termos vetoriais `g0i=O(c⁻³)` não estão presentes. Também não é a métrica 1PN
completa para fontes móveis. A revisão de Fienga e Minazzoli, eqs. 49–51, mostra
explicitamente os três blocos necessários: [Living Reviews in Relativity
(2024)](https://link.springer.com/article/10.1007/s41114-023-00047-0).

A geodésica é integrada sem expansão em `β_nave`: a aproximação está no campo,
não na velocidade da partícula-teste. Isso não restaura termos que foram
descartados da métrica.

## Hipóteses

- partícula-teste: a nave não altera o campo;
- `ψ << 1`; nenhum objeto compacto;
- fontes lentas e tratadas como pontuais no potencial escalar;
- posições das fontes podem variar na efeméride, mas `∂t gμν` é posto em zero
  dentro da geodésica;
- sem spin da fonte, correntes de massa, ondas gravitacionais ou reação de
  radiação;
- referencial de integração inercial SSB/J2000;
- luz do renderizador percorre linha reta; Shapiro e lente não estão no solver
  de tempo retardado;
- empuxo no modo curvo usa a base coordenada como aproximação da tétrade local,
  com erro relativo `O(ψ)`.

## Termos preservados e descartados

| Bloco | Preservado | Descartado / erro líder |
|---|---|---|
| cinemática da nave | todas as potências de `β` em `u⁰` e na camada de massa | nenhum termo SR por expansão em `β` |
| `g00` | `1`, `ψ`, `ψ²` | `O(ψ³)` e potenciais 1PN adicionais de fontes móveis |
| `gij` | `δij(1+2ψ)` | `O(ψ²)` e parte anisotrópica |
| `g0i` | zero | translação e spin, `O(ψ v_fonte/c)` |
| derivadas | gradiente espacial instantâneo de `U` | `∂t U`, `∂t g0i` |
| corpos | monopolos | `J2`, `C22` e harmônicos no modo métrico |

## Limites quantitativos

O parâmetro primário é `ψ=U/c²`, não uma distância universal. Para uma fonte
dominante, `r_min=GM/(ψ_max c²)`. A faixa qualificada do projeto é:

```text
ψ <= 3e-6
0 <= beta_local <= 0.999 nos testes algébricos
beta_local <= 0.1 para uso científico perto de fonte móvel/rotativa
r > raio físico de todo corpo
```

`β_local = sqrt(B/A)|v_coord|/c`. O limite coordenado é
`|v| < c sqrt(A/B)`, e a conversão de entrada agora o verifica. Um valor menor
que `c`, mas maior que esse limite local, é recusado.

Valores de escala:

| Local | `ψ` aproximado | truncamento escalar `ψ²` |
|---|---:|---:|
| LEO | `6.5e-10` | `4.3e-19` |
| 1 AU do Sol | `9.9e-9` | `9.7e-17` |
| órbita de Mercúrio | `2.6e-8` | `6.5e-16` |
| nuvens de Júpiter | `2.0e-8` | `4.0e-16` |
| superfície do Sol | `2.1e-6` | `4.5e-12` |

O limite `β<=0.1` não é imposto pelo tipo numérico. É um limite de **alegação**:
perto de fontes móveis, o erro gravitomagnético descartado escala, em ordem de
magnitude, como `4 β_nave v_fonte/c` relativo ao termo newtoniano. A `β=0.9` e
`v_fonte/c=1e-4`, isso chega a `3.6e-4`, muito maior que `ψ`. Trajetórias rápidas
em espaço profundo continuam dentro do modo `SpecialRelativistic`; trajetórias
rápidas perto de uma fonte não devem ser chamadas de validadas por este modelo.

## Orçamento de erro

Não há um único “erro da relatividade”. Para cada arco deve-se reportar:

```text
erro_modelo >= max(ψ², 4 beta v_fonte/c, erro_harmônicos, erro_tétrade)
erro_numérico = estudo de tolerância/max_step/min_step
erro_efeméride = DE440 versus Horizons/DE441
erro_total não é menor que a maior parcela acima
```

`ψ²` é somente um indicador do truncamento escalar e não limita o erro de
`g0i=0`. A validação atual cobre limite newtoniano, Mercúrio, GPS, Shapiro,
deflexão ultrarrelativística, a matriz `β=0…0.999` e comparação REBOUNDx. Não
cobre Kerr, binárias compactas, passagem dentro de corpos ou nave rápida em
campo rotativo.

## Precisão numérica

- a variável integrada é velocidade própria, portanto não há clamp em `c`;
- `τ` participa do estimador de erro nos dois modos relativísticos;
- entrada, saída, observadores e dense output usam a relação curva `v↔u` no
  modo `WeakFieldStaticMetric`;
- a inversão `v→u` perde aproximadamente `γ² ε_machine`; a `β=0.999` o
  round-trip medido fica em `8e-14` relativo;
- `CoordinateTime` usa duas partes; nunca converter um intervalo pequeno pela
  diferença de dois `double` colapsados.

## Critério para ampliar o domínio

Nenhuma etiqueta mais ampla será adotada antes de: implementar `g0i` e
derivadas temporais em gauge consistente; comparar com Kerr linearizado e um
integrador independente; incluir multipolos compatíveis; repetir a matriz e os
estudos de convergência. Até lá, “métrica estática de campo fraco” é o nome do
modelo.
