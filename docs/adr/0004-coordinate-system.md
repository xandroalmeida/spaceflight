# ADR-0004 — Referencial de integração: SSB / J2000 (ICRF), SI, TDB

Status: **aceito** · Data: 2026-09-13
Detalhamento técnico: `docs/architecture/coordinate-system.md`

## Contexto

O enunciado (§8) exige um referencial baricêntrico do Sistema Solar equivalente a
J2000, apropriado ao SPICE, com a decisão exata documentada, e proíbe usar
coordenadas heliocêntricas como estado absoluto.

## Decisão

O estado autoritativo da simulação é:

```
origem   : Solar System Barycenter (NAIF 0)
eixos    : frame SPICE "J2000" (EME2000, alinhado ao ICRF)
unidades : SI (m, m/s, kg, s)
tempo    : TDB, segundos desde J2000, em representação de duas partes
```

Heliocêntrico, geocêntrico, body-centered, spacecraft-centered e ECLIPJ2000 são
**derivações** obtidas por transformação sob demanda.

## Alternativas consideradas

| Alternativa | Por que não |
|---|---|
| Heliocêntrico como estado absoluto | o Sol acelera em torno do baricentro (amplitude ~1,3·10⁹ m); origem não inercial exigiria forças fictícias |
| Eixos ECLIPJ2000 como frame de integração | introduz uma rotação (e o seu erro) entre o dado DE440 e o estado, sem ganho dinâmico |
| Frame centrado no corpo "dominante", trocando conforme a SOI | descontinuidades artificiais; proibido por §11 |
| Unidades astronômicas (UA, dias) | reduz erro de arredondamento em escala interplanetária, mas obriga conversão em toda interface de engenharia (empuxo, massa, potência) e convida a erros de unidade |
| `long double` / float128 | ganho real, mas custo de portabilidade (ARM vs x86) e de desempenho; a tabela de erro em `coordinate-system.md` §7 mostra que `double` basta |

## Consequências

* Nenhuma conversão entre o dado bruto do SPICE e o estado, exceto km → m.
* A resolução da posição da nave relativa à Terra cai de ~1,5 nm para ~33 µm por
  usar origem baricêntrica. Aceito, quantificado e testado.
* `CoordinateTime` em duas partes (`whole` + `frac`) para não perder resolução
  temporal em regime relativístico — decisão tomada agora para não ser retrofit
  no Milestone 4.
* Frames body-fixed (IAU_*, ITRF93) existem apenas para entrada/saída, nunca para
  integração.
