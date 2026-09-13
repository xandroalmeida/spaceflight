# Frame dragging: formulação e plano de validação

Status: **investigação; não implementado**  
Última revisão: 2026-09-13

## Formulação candidata

No exterior de um corpo isolado, lentamente rotativo e em campo fraco, o termo
de spin da métrica de Kerr linearizada pode ser escrito, para `x⁰=ct`, como

```text
g0i_spin = -2 G (J × r)_i / (c³ r³)
```

O sinal depende das convenções de métrica e produto vetorial; portanto o teste
deve fixar também o sentido da precessão, não apenas sua magnitude. Na métrica
pós-newtoniana geral, `g0i=-4 w_i/c³+O(c⁻⁵)`: o potencial vetorial contém tanto
o spin intrínseco quanto o movimento translacional das fontes. Implementar só
“uma aceleração Lense–Thirring” sem o restante de `g0i` e sem `∂t gμν` quebraria
a contagem de ordem. Ver as eqs. 49–51 da [revisão de efemérides
planetárias](https://link.springer.com/article/10.1007/s41114-023-00047-0) e a
discussão de gravitomagnetismo na [revisão de testes de
gravidade](https://link.springer.com/article/10.12942/lrr-2014-4).

Para um giroscópio, a escala da precessão é

```text
Omega_LT = G/c²r³ [J - 3 (J·r_hat) r_hat]
```

Para a órbita, a taxa nodal secular de uma fonte axial é

```text
dOmega_node/dt = 2 G J / [c² a³ (1-e²)^(3/2)]
```

Essas duas observáveis, mais uma trajetória Kerr linearizada, serão os oráculos
analíticos. O resultado do Gravity Probe B fornece um terceiro oráculo
experimental; o projeto não usará uma função do próprio core como `expected`.

## Domínio de validade

- `GM/(rc²) << 1`;
- `a_spin=J/(Mc) << r`;
- fonte aproximadamente estacionária e axial;
- primeira ordem em `J`; sem quadrupolo acoplado ao spin;
- partícula-teste; sem reação de radiação;
- fora do corpo.

Não serve perto do horizonte de um objeto compacto, para rotação extrema, nem
como substituto de uma métrica de Kerr completa.

## Onde importa

| Corpo / local | escala de aceleração `2GJv/(c²r³)` | leitura |
|---|---:|---|
| Terra, LEO (`v≈7.7 km/s`) | `2.2e-10 m/s²` | pequeno por órbita, secular no nó/giroscópio |
| Sol, 1 AU | `2.6e-15 m/s²` | irrelevante para uma missão curta |
| Júpiter, órbita baixa | `7e-8 m/s²` | melhor planeta do Sistema Solar para inspeção |
| Sol, passagem rasante rápida | `~4e-7 m/s²` | mensurável em integração longa/precisa |

Os números usam `J_Terra≈5.86e33`, `J_Sol≈1.92e41` e
`J_Júpiter≈4.33e38 kg m²/s`; antes do código esses valores devem ganhar fonte,
incerteza e orientação por época.

## Nave lenta e relativística

Para uma nave lenta, o efeito é uma perturbação secular minúscula e separável de
`J2` apenas com um caso cuidadosamente escolhido. Para uma nave relativística, o
acoplamento com a velocidade cresce aproximadamente com `β`; isso torna a
omissão relativamente mais importante, mas uma passagem rápida acumula o efeito
por menos tempo. Não se deve extrapolar a fórmula de aceleração 1PN em `v/c`
para `β≈1`; deve-se integrar a geodésica da métrica com `g0i` mantendo a
dependência exata na 4-velocidade.

## Interação com o modelo atual

`WeakFieldMetric` hoje fornece apenas `A`, `B` e seus gradientes. A extensão
mínima correta precisa fornecer `g0i`, gradientes espaciais e derivadas
temporais, recalcular o vínculo da camada de massa (agora com termo cruzado
`2g0i u0 ui`) e gerar Christoffels completos. Também precisa distinguir:

1. corrente de massa pela translação do corpo;
2. spin intrínseco e sua orientação;
3. mudança temporal dos dois.

## Gates antes da implementação

1. teste de sinal e magnitude da taxa nodal fechada;
2. teste de precessão de giroscópio contra Gravity Probe B;
3. comparação de uma geodésica com solução Kerr linearizada independente;
4. teste `J→0` bit a bit com o modelo atual;
5. estudo de convergência separado do tamanho físico do efeito;
6. documentação do gauge e de todos os termos da mesma ordem.

