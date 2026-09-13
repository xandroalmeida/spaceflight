# Otimização do ângulo do plano B

Status: **problema definido; não implementado**  
Última revisão: 2026-09-13

O corretor atual determina `B·T` e `B·R` para um ângulo fornecido. Ele não escolhe
o ângulo. O novo problema é externo ao corretor:

```text
variável: theta_B em [-pi, pi)
estado avaliado: missão completa com queima finita e aproximação ao alvo
restrições duras: convergência, r_p desejado, combustível >= 0, não impacto
objetivos: delta-v de inserção, combustível, plano/inclinação final e erro de r_p
```

Uma função escalar possível é

```text
J(theta) = w_dv Δv_insert + w_f m_fuel
         + w_i wrap(i_final-i_target)^2
         + w_p (r_p-r_p_target)^2
```

Os pesos têm unidades e não podem ser escolhidos silenciosamente. A interface
deve preferir objetivos lexicográficos: primeiro satisfazer periastro/plano,
depois minimizar combustível ou `Δv`. Para órbita circular, `Δv_insert` depende
principalmente de `v_inf` e `r_p`; o ganho do ângulo vem da orientação da
velocidade e da queima necessária para obter o plano desejado.

O método inicial será determinístico: grade uniforme de 72 ângulos, retenção dos
melhores intervalos válidos e refinamento 1D limitado (Brent). Cada avaliação
executa a missão completa; derivadas finitas do corretor interno não serão
reutilizadas como gradiente externo. Empates são resolvidos pelo menor
`|theta|`, depois pelo menor `theta`, para manter reprodutibilidade.

Antes do código são necessários: hiperboles sintéticas com solução geométrica,
casos simétricos `theta`/`theta+pi`, uma órbita-alvo com inclinação conhecida,
e repetição bit a bit de todo o ranking. O ângulo já usa `units::Angle`, impedindo
que graus sejam passados a uma API em radianos.

