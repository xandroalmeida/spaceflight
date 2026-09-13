# Matriz de estresse — Milestone 6

Data: 2026-09-13

Cada linha abaixo foi executada pela suíte, não apenas inspecionada. O critério
comum é: estado finito, `|v|<c`, massa acima do limite configurado, combustível
sem overshoot, quaternion unitário e tempos no sentido da integração.

| Caso | Cobertura executada | Resultado |
|---|---|---|
| excentricidade muito alta | órbita completa `e=0.99`, periapsis a 300 km | PASS |
| quase escape | velocidade a `1e-9` relativo abaixo da fuga, arco de 5 dias | PASS |
| flyby próximo | hipérbole com periapsis a 10 km e `1.2 v_escape` | PASS |
| alto empuxo | queima curta converge ao impulso (`test_propulsion`) | PASS |
| baixo empuxo | comparação de perdas em queimas 10× mais longas (`test_propulsion`) | PASS |
| combustível esgota durante queima | corte analítico no dry mass, sem massa negativa | PASS |
| atividade RCS | slew orbital em 60 janelas, força/torque e consumo integrados (`test_attitude`) | PASS |
| `beta -> 1` | `beta=0.999999999`, coast SR por 100 anos | PASS |
| longa duração | mesmo coast de 100 anos | PASS |
| warp grande | partições até `100000x` contra Kepler independente | PASS |

Os três primeiros casos e o coast relativístico vivem em
`test_verification_hardening`; os demais reutilizam testes físicos existentes,
mas seus oráculos não são funções do caminho de produção. A matriz testa
robustez numérica e invariantes. Ela não amplia o domínio de validade físico
definido em `../physics/relativity-validity-domain.md`.
