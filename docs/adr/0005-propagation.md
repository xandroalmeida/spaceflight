# ADR-0005 — Propagação: Dormand–Prince 5(4) adaptativo, desacoplado do frame

Status: **aceito** · Data: 2026-09-13

## Contexto

O enunciado (§12) exige passo adaptativo, controle de erro, tolerância
configurável, limites de passo, logging, estatísticas, e proíbe amarrar o passo
ao framerate. A física futura inclui propulsão (força não conservativa) e
relatividade (troca da lei de movimento).

## Decisão

1. Integrador padrão: **Dormand–Prince 5(4)** (RK explícito de 7 estágios com
   FSAL), com estimativa de erro embutida de ordem 4.
2. Controle de passo **PI** sobre a norma de erro escalada por `atol`/`rtol`:

   ```
   err  = sqrt( (1/n) Σ ( e_i / (atol + rtol·max(|y_i|,|ŷ_i|)) )² )
   h_new = h · clamp( safety · err^(−1/5) , min_factor , max_factor )
   ```
   Passo rejeitado se `err > 1`; limites duros `h_min`, `h_max` configuráveis;
   atingir `h_min` é **falha reportada**, não um passo aceito à força.
3. A interface `SpacecraftPropagator` é abstrata; o integrador concreto é
   substituível sem tocar nos modelos de força.
4. O passo do integrador é **independente** do frame de renderização e do time
   warp. `SimulationClock` pede "avance até `t`"; como o propagador chega lá é
   problema dele.
5. Estatísticas obrigatórias por propagação: passos aceitos, rejeitados,
   avaliações de força, `h` mínimo/médio/máximo, erro máximo estimado, tempo de
   parede.

## Alternativas consideradas

| Alternativa | Por que não (agora) |
|---|---|
| RK4 de passo fixo | sem controle de erro; inviável para órbitas excêntricas |
| Euler / Verlet simples | ordem baixa demais para as tolerâncias pretendidas |
| Integradores simpléticos (WHFast, leapfrog) | a vantagem simplética é para sistemas **conservativos e autônomos**; aqui o campo é explicitamente dependente do tempo (efemérides) e haverá empuxo. A conservação de longo prazo dos planetas já é garantida pelo DE440, não pelo nosso integrador |
| Gauss–Radau IAS15 (REBOUND) | excelente (precisão de máquina), e previsto como **validador** independente (§29); adotá-lo como motor principal traria a dependência inteira do REBOUND para dentro da arquitetura |
| DOP853 | melhor para tolerâncias muito apertadas; previsto como opção futura, mas DP5(4) é mais simples de validar primeiro |
| Encke / elementos variacionais | ganho grande perto de um corpo central dominante; adiciona complexidade de "corpo de referência" que conflita com §11. Reconsiderar se os testes mostrarem necessidade |

## Consequências

* O integrador é genérico sobre o vetor de estado (`PropagationState`), de modo
  que acrescentar tempo próprio, massa e, mais tarde, trocar `v` por `u = γv`
  (ver `relativity-roadmap.md` §3) não muda o integrador.
* Como DP5(4) não é simplético, testes de conservação de energia/momento angular
  são feitos **com tolerância derivada da tolerância do integrador**, não com
  números mágicos (ver `docs/validation/tolerances.md`).
* Dense output (interpolação de 4ª/5ª ordem dentro do passo) fica previsto para o
  Milestone 2, quando o renderizador precisar de estados em instantes arbitrários
  sem forçar o passo do integrador.
* Eventos (periapsis, entrada em raio de corpo, fim de queima) serão detectados
  por mudança de sinal + refinamento, no futuro; no Milestone 0 apenas a
  sinalização de aproximação extrema existe.
