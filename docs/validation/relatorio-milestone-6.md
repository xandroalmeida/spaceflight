# Relatório do Milestone 6 — Verificação e hardening de física

**Data:** 13 de setembro de 2026  
**Escopo:** auditoria científica, validação independente, robustez numérica e verificação visual  
**Resultado global:** **não aprovado para encerramento**

> Este documento é o registro do Milestone 6 e fica como está. Os dois
> bloqueadores que ele levanta foram fechados no Milestone 6.1 —
> [relatório](milestone-6-1-report.md),
> [navegação](lunar-navigation-hardening.md),
> [starfield](starfield-debug.md). Um dos seus números merece correção: os
> "18 sucessos estritos" contavam também a convergência do corretor de posição,
> e a campanha na verdade capturou 41 de 100. A convergência do estágio 1 não é
> condição de captura, e tratá-la como tal escondeu que o problema real era
> outro.

## Resumo executivo

A auditoria encontrou e corrigiu dois defeitos no núcleo físico. Os propagadores, invariantes, transformações de referenciais, efemérides e testes de estresse apresentaram resultados consistentes com referências independentes.

O milestone permanece aberto por dois bloqueadores:

1. a campanha Terra–Lua teve apenas **18 sucessos estritos em 100 épocas**;
2. o starfield carregou **8.786 estrelas**, mas nenhuma apareceu no framebuffer, impedindo validar visualmente aberração, Doppler e beaming.

| Área | Estado | Conclusão |
|---|---|---|
| Núcleo relativístico | PASS | Dois defeitos corrigidos e cobertos por oráculos independentes |
| Propagação e invariantes | PASS | Convergência, determinismo e estresse aprovados |
| Efemérides e referenciais | PASS | Comparações independentes e round-trips aprovados |
| Missão Terra–Lua | **FAIL** | 18% de sucesso estrito |
| Render relativístico | **BLOCKED** | Starfield invisível bloqueia a inspeção óptica |

## Defeitos encontrados e corrigidos

### Conversão de velocidade na métrica curva

A integração usava a métrica curva, mas entrada, saída e dense output convertiam velocidade coordenada e velocidade própria com a relação de Minkowski. O erro era da ordem de `U/c²`.

A conversão agora usa a razão métrica `A/B`, verifica o cone de luz local e rejeita estados que excedam o limite coordenado `c·sqrt(A/B)`. O teste foi refeito com relação analítica independente.

### Tempo retardado em alta velocidade

A iteração de ponto fixo não convergia para uma fonte recedendo a `0,99c`. O solver foi substituído por Newton com velocidade da efeméride e agora concorda com a solução fechada para movimento retilíneo uniforme.

## Domínio relativístico validado

O nome genérico `GeneralRelativistic` foi removido. O modo gravitacional chama-se `Kinematics::WeakFieldStaticMetric`.

O modelo implementado é uma métrica estática de campo fraco:

```text
ψ = U/c²
g00 = -(1 - 2ψ + 2ψ²)
g0i = 0
gij = (1 + 2ψ) δij
```

Ele preserva `g00` até `O(c⁻⁴)` e `gij` até `O(c⁻²)`, mas não representa uma métrica 1PN completa para fontes móveis e não é 2PN. São descartados `g0i`, derivadas temporais do campo, spin, correntes de massa, multipolos, ondas gravitacionais e reação de radiação.

Faixa atualmente qualificada:

- `ψ ≤ 3e-6`;
- `0 ≤ β_local ≤ 0,999` para testes algébricos;
- `β_local ≤ 0,1` para alegações científicas próximas de fontes móveis ou rotativas;
- posição externa ao raio físico dos corpos.

A matriz de referência cobriu `β = 0`, `0,01`, `0,1`, `0,5`, `0,9`, `0,99` e `0,999` em espaço profundo, 1 AU do Sol, órbita de Mercúrio, LEO e proximidade de Júpiter. Velocidade coordenada, velocidade própria, `γ`, tempos, acelerações, energia e momento concordaram com álgebra independente em precisão estendida.

## Convergência e timestep

O propagador Dormand–Prince 5(4) foi comparado com uma solução Kepleriana independente por variáveis universais.

| `rtol` | Erro de posição (m) | Erro de velocidade (m/s) | Deriva de energia | Deriva de momento angular | Passos |
|---:|---:|---:|---:|---:|---:|
| `1e-6` | 819,678 | 0,406604 | 3,23145e-5 | 9,41240e-6 | 29 |
| `1e-8` | 8,54956 | 0,00510217 | 6,47332e-8 | 1,67004e-8 | 60 |
| `1e-10` | 0,0776174 | 5,20141e-5 | 4,47393e-10 | 2,78603e-10 | 145 |
| `1e-12` | 0,000641706 | 4,45688e-7 | 6,15239e-12 | 3,45268e-12 | 361 |
| `1e-13` | 0,0000597816 | 4,19526e-8 | 6,37243e-13 | 3,53629e-13 | 571 |

Posição e velocidade convergiram monotonicamente. A deriva de tempo próprio atingiu o piso de arredondamento e, por isso, não é monotônica.

Entre warp `1×` e `100000×`, variando FPS e particionamento das chamadas, o pior desvio foi `1,054e-3 m` em posição e `1,417e-6 m/s` em velocidade. Todas as execuções terminaram na mesma `CoordinateTime`.

O propagador foi repetido dez vezes com comparação bit a bit; o corretor de partida, vinte vezes. Não houve divergência.

## Campanha Terra–Lua

Foram executadas 100 geometrias determinísticas, uma por dia desde 1º de janeiro de 2026, mantendo nave e parâmetros constantes.

| Indicador | Resultado |
|---|---:|
| Sucesso estrito | **18/100** |
| Convergência Lambert/corretor de posição | 26/100 |
| Convergência do plano B | 79/100 |

Distribuição entre os casos bem-sucedidos:

| Métrica | Mínimo | Mediana | P95 | Máximo |
|---|---:|---:|---:|---:|
| Erro absoluto de periastro (km) | 0,004483 | 0,0728475 | 0,257868 | 0,28252 |
| Delta-v de inserção (m/s) | 809,465 | 841,427 | 904,794 | 916,998 |
| Excentricidade final | 0,00160094 | 0,00176875 | 0,00210949 | 0,00218251 |
| Altitude de apoastro (km) | 102,78 | 103,265 | 103,917 | 104,048 |
| Altitude de periastro (km) | 96,0277 | 96,6636 | 97,0025 | 97,2792 |
| Combustível usado (kg) | 8,91059 | 16,6504 | 31,993 | 34,5242 |

Modos de falha:

- 53 inserções lunares permaneceram hiperbólicas;
- 22 corretores de posição não convergiram;
- 6 casos excederam 30 segundos;
- 1 trajetória intersectou a superfície lunar.

Conclusão: a missão lunar não é uma regressão científica robusta. Uma demonstração nominal bem-sucedida não altera esse resultado.

## Efemérides, referenciais e unidades

Foram comparados 27 estados geométricos SSB/ICRF, em épocas passada, presente e futura, entre o caminho SPICE DE440 do simulador e o Horizons DE441. O maior residual foi o da Lua: `10,1613 m` em posição e `2,80032e-5 m/s` em velocidade. O oráculo não chama funções de efeméride do simulador.

Transformações baricêntricas, heliocêntricas, geocêntricas e lunocêntricas foram testadas em ida e volta, incluindo velocidades. A rotação J2000↔ECLIPJ2000 foi comparada com obliquidade IAU independente; o maior residual foi `1,5e-6 m` em posição e `8,8e-13 m/s` em velocidade.

A auditoria de unidades corrigiu a interface do ângulo do plano B, que misturava radianos no core com graus na CLI e no Godot. A API agora usa o strong type `units::Angle`. Permanecem como riscos: ângulos de quaternions, ângulos de elementos orbitais e tolerância do solver de tempo de luz ainda expostos como `double`.

## Invariantes e estresse

Os checks de runtime cobrem:

- velocidade dentro do cone de luz local;
- massa positiva e acima do limite seco;
- combustível não negativo;
- quaternion finito e aproximadamente unitário;
- tempo próprio e tempo coordenado monotônicos;
- estados finitos, sem clamp silencioso de `NaN` ou `Inf`.

Passaram os casos de excentricidade `0,99`, quase escape, flyby com periastro a 10 km, alto e baixo empuxo, esgotamento durante queima, 60 janelas de RCS, `β = 0,999999999`, propagação por 100 anos e warp até `100000×`.

## Verificação visual

A extensão nativa foi executada em janela no macOS com Godot 4.5, Metal 3.2 e Forward+. Foram exercitados estados `0c`, `0,1c` e `0,99c`, HUD completo/compacto/oculto, foco na nave e no Sol, warp `1×`/`10×` e os quatro toggles relativísticos.

| Área | Estado | Evidência |
|---|---|---|
| Build e execução | PASS | GDExtension carregada; sem `NaN`, `Inf`, erro de shader ou exceção |
| HUD e câmera | PARCIAL | Caminho principal funciona; faltam resoluções e varredura completa |
| Starfield | **FAIL** | 8.786 estrelas carregadas, nenhuma visível |
| Aberração | **BLOCKED** | Toggle funciona; inspeção depende do starfield |
| Doppler | **BLOCKED** | Fatores variam; efeito visual não pode ser julgado |
| Beaming | **BLOCKED** | Toggle independente; efeito visual não pode ser julgado |
| Tempo retardado | PARCIAL | Centro e toggle ativos; inspeção diferencial pendente |

O cenário de desenvolvimento permite selecionar `0c`, `0,1c`, `0,5c`, `0,9c` e `0,99c` sem alterar o estado físico normal, além de ligar e desligar aberração, Doppler, beaming e tempo retardado separadamente.

## Estudos documentados, ainda não implementados

- **Frame dragging:** exige `g0i`, gradientes, derivadas temporais, correntes translacionais e spin em gauge consistente; implementar apenas uma aceleração de Lense–Thirring quebraria a contagem de ordem.
- **Otimização do plano B:** problema determinístico definido para delta-v, combustível, plano orbital, inclinação e periastro, com critérios explícitos de desempate.
- **Modelo lunar:** qualificação progressiva proposta contra GRAIL grau 420; os graus por altitude ainda são hipóteses a falsificar, não resultados certificados.
- **Objetos extensos:** o caminho atual transforma vértices individualmente e resolve tempo retardado para os offsets; faltam comparação CPU/shader, medição de custo e inspeção próxima da silhueta.

## Decisão e prioridades

O Milestone 6 não deve ser encerrado como aprovação global enquanto os bloqueadores permanecerem.

| Prioridade | Ação | Critério de saída |
|---|---|---|
| P0 | Diagnosticar e corrigir planner/corretor/inserção lunar | Taxa-alvo definida e distribuição estável em nova campanha de ≥100 épocas |
| P0 | Corrigir o starfield | Estrelas visíveis em `0c` e resposta isolada de aberração, Doppler e beaming |
| P1 | Completar checklist visual | Nenhum item pendente ou bloqueado |
| P1 | Qualificar harmônicos lunares por altitude | Menor grau atende limites de 100 m no arco e 10 m após uma órbita |
| P2 | Avaliar frame dragging | Sinal, magnitude, convergência e limite `J→0` aprovados antes da implementação |

## Evidências

- [Domínio de validade relativístico](../physics/relativity-validity-domain.md)
- [Frame dragging](../physics/frame-dragging.md)
- [Otimização do plano B](../physics/b-plane-optimization.md)
- [Modelo gravitacional lunar](../physics/lunar-gravity-model.md)
- [Aberração de objetos extensos](../physics/extended-object-aberration.md)
- [Convergência numérica](numerical-convergence.md)
- [Campanha Terra–Lua](lunar-mission-campaign.md) e [dados CSV](lunar-mission-campaign.csv)
- [Validação Horizons](ephemeris-horizons.md) e [dados CSV](ephemeris-horizons.csv)
- [Testes de estresse](stress-tests.md)
- [Checklist visual](visual-verification-checklist.md)

## Reprodução mínima

```bash
ctest --test-dir build --output-on-failure
./build/bin/test_integrator_convergence
./build/bin/test_verification_hardening
```

