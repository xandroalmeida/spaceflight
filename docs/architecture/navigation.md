# Navegação, Manobras e Execução

Status: vigente a partir do Milestone 1
Última revisão: 2026-09-13
Física do motor: `docs/physics/propulsion-model.md`

## 1. A regra que organiza tudo

§26 e §28 do enunciado dizem a mesma coisa de duas formas:

> O planejador não altera a posição da nave.
> Nunca modificar diretamente position, velocity, orientation.

Portanto a arquitetura tem exatamente uma direção de causalidade:

```
TrajectoryPlanner  →  ManeuverPlan  →  ManeuverExecutor  →  ForceModel  →  integrador  →  estado
   (calcula)           (dados)          (interpreta)        (força)        (integra)
```

Nada à esquerda toca o estado. O planejador produz **dados**: instantes,
durações, throttles, direções. O executor os traduz em **força**. A única coisa
que move a nave é o integrador, e ele só sabe somar acelerações.

Isso não é purismo. É o que permite:

* planejar sem simular (o planejador é uma função pura de estados e tempos);
* simular sem planejar (um plano lido de arquivo se comporta igual);
* comparar o que foi planejado com o que aconteceu — que é o §2 abaixo.

## 2. Impulsivo é planejamento; finito é execução

Toda a mecânica orbital clássica (Hohmann, Lambert, `Δv` de inserção) supõe
**queimas impulsivas**: a velocidade muda instantaneamente e a posição não muda.
Isso é uma aproximação excelente para planejar e **falsa** para executar.

Um motor real queima por um tempo finito `Δt`. Durante esse tempo:

* a nave se move, então o empuxo não é aplicado todo no mesmo ponto;
* a direção "prograde" gira junto com a velocidade;
* parte do empuxo é gasta contra a gravidade em vez de somar energia orbital.

A diferença tem nome: **perda gravitacional** (*gravity loss*). A lei de escala é
quadrática na duração da queima:

```
Δv_perdido / Δv  ≈  κ (n Δt)²            (n = movimento médio)
```

O expoente 2 é robusto; o coeficiente `κ` **não** é universal — depende do modo
de guiamento e da geometria. Dois mecanismos distintos contribuem e têm
coeficientes diferentes:

* com guiamento inercial, o empuxo perde alinhamento com a velocidade, que gira
  durante a queima: `κ = 1/24` para uma queima centrada;
* com guiamento *prograde* a partir de órbita circular, o empuxo está sempre
  alinhado com a velocidade e a perda vem de outro lugar: a nave **sobe** durante
  a queima e converte velocidade em energia potencial.

O segundo é maior. Medido em `tests/scientific/test_propulsion.cpp`, para uma
queima prograde de 214,9 s (3,9 % do período) em LEO com `Δv = 1704 m/s`:

```
perda = 30,80 m/s = 1,81 % de Δv        ⇒  κ ≈ 0,31
```

e com dez vezes o empuxo (queima de 21,5 s) a perda cai para 0,31 m/s — fator
99,2, contra os 100 que a lei quadrática prevê. É esse fator que o teste
verifica, não o coeficiente.

Este projeto **não esconde a perda**: o plano é calculado no modelo impulsivo,
executado no modelo finito, e a diferença é medida e reportada em cada
`BurnReport` (`delta_v_rocket` contra `speed_change`). Fechar a diferença é
trabalho do planejador — ajustar `Δv` e o instante de ignição —, não do executor,
e ainda não está implementado.

## 3. Tipos

```cpp
enum class GuidanceMode {
    Inertial,        // direção fixa no referencial de integração
    Prograde,        // +v relativa ao corpo de referência
    Retrograde,      // -v
    Normal,          // +h = r x v
    AntiNormal,      // -h
    RadialOut,       // +r
    RadialIn         // -r
};

struct Maneuver {
    std::string name;
    time::CoordinateTime ignition;
    time::Duration duration;
    double throttle;              // [0, 1]
    GuidanceMode guidance;
    celestial::BodyId reference;  // corpo em que prograde/normal/radial se apoiam
    math::Vec3 inertial_direction;// usado quando guidance == Inertial
};

class ManeuverPlan {            // sequência ordenada, sem sobreposição
    std::vector<Maneuver> maneuvers_;
    std::vector<CoordinateTime> switch_times() const;  // ver secao 4
};
```

`GuidanceMode` cobre os modos de §28 que não precisam de atitude real. Os que
precisam (`POINT_TARGET`, `MATCH_VELOCITY`) entram no Milestone 3, quando existir
`AttitudeState` — até lá o corpo da nave não tem orientação própria, e fingir que
tem seria inventar estado.

## 4. Descontinuidades: por que o plano expõe seus instantes de chaveamento

Ligar um motor é uma **descontinuidade na derivada** do estado. Um integrador de
passo adaptativo que atravessa uma descontinuidade se comporta mal de um jeito
específico e caro: o controlador de erro rejeita passos repetidamente, encolhe o
passo até quase o mínimo, atravessa, e só então relaxa. O resultado é lento e,
pior, com erro local mal estimado exatamente no ponto mais importante da
trajetória.

A solução não é afrouxar a tolerância: é **não atravessar**. `ManeuverPlan`
expõe `switch_times()` (cada ignição e cada corte), e a propagação de missão
quebra o intervalo pedido nesses instantes, integrando cada trecho com derivada
contínua:

```
t0 ────────── ignição ══════ corte ────────── t1
   trecho 1          trecho 2       trecho 3
   (coasting)        (queima)       (coasting)
```

Cada trecho é uma chamada a `propagate`, e o dense output de todos eles é
concatenado em uma única `Trajectory` (ADR-0006), de modo que quem consome não
percebe a costura.

## 5. O executor

`ManeuverExecutor` é um `ForceModel` como qualquer outro (§10 do enunciado): o
integrador não sabe que existe propulsão, só soma mais uma aceleração.

```cpp
ForceResult ManeuverExecutor::evaluate(state, t) const {
    const Maneuver* active = plan_.active_at(t);
    if (!active) return {};                     // sem empuxo, sem consumo

    if (tank is empty) return {};               // sem caso especial: sem massa,
                                                // sem empuxo (propulsion-model.md s.5)

    const double q = active->throttle * engine_.max_mass_flow();
    const double thrust = engine_.efficiency() * q * engine_.exhaust_velocity();

    result.acceleration  = direction(state, t, *active) * (thrust / state.mass);
    result.mass_flow_rate = -q;                 // dm/dt, negativo
    return result;
}
```

Dois pontos que o código impõe:

* **A massa é variável de estado**, integrada junto com posição e velocidade. Não
  é atualizada "por fora" depois do passo: o integrador precisa de `m(t)` dentro
  dos sete estágios, porque a aceleração depende dela.
* **Tanque vazio não é caso especial.** Quando o propelente acaba, `q = 0` e o
  empuxo some continuamente. Nenhum `if` de gameplay.

### 5.1 Armar o executor: qual limite lateral avaliar

No instante exato do corte, o empuxo é genuinamente ambíguo. O último passo do
trecho de queima está integrando a dinâmica **com motor ligado até** aquele
instante e precisa do limite pela esquerda; o primeiro passo do trecho seguinte
precisa do limite pela direita, no mesmo epoch.

Uma consulta `active_at(t)` não consegue responder às duas: com a regra
semiaberta `[ignição, corte)`, os sete estágios do último passo da queima veem o
motor ora ligado (estágios internos) ora desligado (estágio final em `t = corte`).
O estimador de erro interpreta isso como um erro enorme, rejeita o passo,
encolhe, e repete até bater no passo mínimo. **Foi exatamente o que aconteceu na
primeira implementação** — o sintoma foi `minimum step reached` em uma queima
perfeitamente comum.

Por isso o executor é **armado** pelo runner antes de cada trecho
(`ManeuverExecutor::arm`): dentro de um trecho a força é constante, e quem decide
qual limite lateral vale é quem sabe qual trecho está sendo integrado. Sem armar,
o executor responde por consulta ao tempo — correto como função de `t`, e caro
como dinâmica.

## 6. O planejador

`TrajectoryPlanner` produz `ManeuverPlan` a partir de objetivos. No Milestone 1:

| Objetivo | Método | Documento |
|---|---|---|
| alterar apoastro/periastro | queima tangencial no apside oposto, `Δv` por vis-viva | este, §7 |
| transferência circular↔circular coplanar | Hohmann de dois impulsos | este, §7 |
| escapar da Terra | queima tangencial até `energia ≥ 0` | este, §7 |
| interceptar um corpo | Lambert + targeting diferencial | `docs/physics/lambert.md` §6 |

O planejador recebe um `EphemerisProvider` e o estado atual; devolve dados. Ele
**não** propaga a nave para "ver se dá certo" — quem verifica é quem chamou, e a
verificação é executar o plano e medir.

## 7. Fórmulas usadas no planejamento (impulsivas)

Todas em dois corpos em torno do corpo de referência, com `μ = GM`:

```
vis-viva            v² = μ (2/r − 1/a)
circular            v_c = √(μ/r)
escape              v_esc = √(2μ/r)

Hohmann r1 → r2     a_t = (r1 + r2)/2
                    Δv1 = √(μ/r1) (√(2r2/(r1+r2)) − 1)
                    Δv2 = √(μ/r2) (1 − √(2r1/(r1+r2)))
                    t_transf = π √(a_t³/μ)

novo apoastro ra'   no periastro rp:  v' = √(μ (2/rp − 2/(rp+ra')))
                    Δv = v' − v_atual
```

Estas são aproximações de dois corpos aplicadas a um sistema de N corpos com J₂.
O erro é justamente a perturbação que o Milestone 0 e o J₂ tornaram mensurável —
e é por isso que o plano precisa ser **verificado por propagação**, não confiado.

## 8. Da manobra ao `Δv` efetivo

Converter um `Δv` planejado em uma manobra executável usa a equação do foguete
(`docs/physics/propulsion-model.md` §4.2), no limite newtoniano:

```
m₁ = m₀ exp(−Δv / v_eff)          v_eff = η w
Δm = m₀ − m₁
Δt = Δm / q                        q = throttle · q_max
```

Se `Δm` excede o propelente disponível, o planejador **recusa o plano** e diz de
quanto faltou. Não entrega um plano que a nave não pode cumprir.

## 9. O que fica para depois

* `POINT_TARGET`, `MATCH_VELOCITY`, `CIRCULARIZE` automático — Milestone 3, com atitude;
* otimização de trajetória, gravity assists, empuxo contínuo — §27;
* targeting no plano-B, para mirar altitude de sobrevoo em vez de posição
  (`docs/physics/lambert.md` §6);
* controle em malha fechada (corrigir durante a queima) — precisa de navegação
  com estimativa de estado, que é outro assunto.
