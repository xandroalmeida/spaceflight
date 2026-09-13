# Spaceflight Simulator — Arquitetura Inicial

Vamos construir um simulador científico de voo espacial restrito ao Sistema Solar.

As decisões fundamentais de arquitetura abaixo já foram tomadas e **não devem ser rediscutidas sem uma razão técnica forte e demonstrável**.

O objetivo do projeto é:

> Construir uma simulação interativa fisicamente consistente do voo de uma nave através do Sistema Solar, incluindo eventualmente regimes relativísticos, mantendo separação estrita entre física, astronomia, visualização e gameplay.

---

# 1. Stack definida

## Linguagem principal

Utilize:

**C++20**

C poderá ser utilizado para integração com bibliotecas nativas, especialmente CSPICE e outras bibliotecas científicas.

O núcleo da aplicação deve utilizar C++ moderno.

Preferir:

* RAII;
* ownership explícito;
* `std::unique_ptr`;
* `std::shared_ptr` apenas quando realmente necessário;
* `std::span`;
* `std::chrono`;
* strong types para unidades quando útil;
* interfaces pequenas;
* código testável;
* ausência de estado global desnecessário.

---

# 2. Build

Utilize:

**CMake**

O projeto inteiro deve poder ser compilado sem abrir o engine gráfico.

Devemos conseguir executar:

```bash
cmake
cmake --build
ctest
```

e testar completamente o núcleo científico.

O Godot será apenas outro consumidor da biblioteca.

---

# 3. Engine gráfico

Utilize:

**Godot 4.x estável**

Integração nativa:

**godot-cpp + GDExtension**

Godot será responsável apenas por:

* renderização;
* UI;
* cockpit;
* câmera;
* áudio;
* input;
* shaders;
* modelos 3D;
* efeitos visuais.

Godot NÃO será responsável pela física orbital.

Não utilizar:

* Godot RigidBody para movimento orbital;
* física interna do Godot como fonte de verdade;
* coordenadas do Scene Tree como coordenadas astronômicas.

A posição mostrada no Godot é apenas uma projeção do estado da simulação.

---

# 4. Biblioteca principal

Criar uma biblioteca independente:

```text
spaceflight-core
```

Ela não deve depender do Godot.

Exemplo:

```text
libspaceflight_core
```

O executável gráfico depende do core.

O core nunca depende do executável gráfico.

---

# 5. Arquitetura geral

Estruture o projeto aproximadamente como:

```text
spaceflight/
│
├── CMakeLists.txt
│
├── cmake/
│
├── docs/
│   ├── architecture/
│   ├── physics/
│   ├── adr/
│   └── validation/
│
├── external/
│
├── kernels/
│   └── spice/
│
├── core/
│   ├── math/
│   ├── units/
│   ├── time/
│   ├── coordinates/
│   ├── ephemeris/
│   ├── celestial/
│   ├── gravity/
│   ├── propagation/
│   ├── relativity/
│   ├── propulsion/
│   ├── spacecraft/
│   ├── attitude/
│   ├── navigation/
│   ├── autopilot/
│   ├── trajectory/
│   └── simulation/
│
├── tools/
│   ├── orbit-cli/
│   └── validation/
│
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── scientific/
│   └── regression/
│
└── godot/
    ├── project/
    └── gdextension/
```

Não considere essa estrutura imutável, mas preserve a separação arquitetural.

---

# 6. Efemérides

Utilizar:

**NASA/JPL NAIF CSPICE**

Efemérides padrão:

**JPL DE440**

DE441 poderá ser suportada posteriormente para períodos temporais muito extensos.

O sistema deverá carregar kernels SPICE.

Criar interface:

```cpp
class EphemerisProvider {
public:
    virtual BodyState state(
        BodyId body,
        CoordinateTime time,
        ReferenceFrame frame
    ) const = 0;
};
```

Implementação inicial:

```cpp
class SpiceEphemerisProvider final
    : public EphemerisProvider
{
    ...
};
```

---

# 7. Fonte de verdade astronômica

Não reintegre as órbitas planetárias apenas para descobrir onde os planetas deveriam estar.

O SPICE será a fonte autoritativa dos estados dos corpos massivos.

Dado um instante `t`, devemos conseguir consultar:

```text
Sun(t)
Earth(t)
Moon(t)
Mars(t)
Jupiter(t)
...
```

obtendo pelo menos:

```text
position
velocity
```

em um referencial conhecido.

---

# 8. Referencial principal

Utilizar inicialmente um referencial:

**Solar System Barycentric / J2000-equivalent apropriado ao SPICE.**

A decisão exata deverá ser documentada.

Não utilize coordenadas heliocêntricas como estado absoluto universal.

Poderemos derivar coordenadas:

```text
barycentric
heliocentric
geocentric
body-centered
spacecraft-centered
```

através de transformações.

---

# 9. Nave como test particle

A nave tem massa desprezível em relação aos corpos do Sistema Solar.

Portanto:

```text
planetas → afetam nave

nave → NÃO afeta planetas
```

Isso permite utilizar SPICE continuamente sem precisar alterar as efemérides.

---

# 10. Campo gravitacional

Para cada avaliação do integrador em tempo `t`:

1. obter posição dos corpos relevantes através do EphemerisProvider;
2. obter posição da nave;
3. calcular contribuição gravitacional;
4. somar forças/acelerações;
5. adicionar propulsão;
6. adicionar outras correções físicas habilitadas.

Arquitetura:

```cpp
class ForceModel {
public:
    virtual ForceResult evaluate(
        const PropagationState& spacecraft,
        CoordinateTime t
    ) const = 0;
};
```

Exemplos:

```text
PointMassGravity
RelativisticGravity
MainEngineForce
RcsForce
```

---

# 11. Não utilizar Sphere of Influence como física

Sphere of Influence poderá ser utilizada para:

* UI;
* planejamento;
* classificação;
* otimização.

Mas nunca para decidir artificialmente:

```text
"agora a Terra parou de puxar e o Sol começou"
```

Todas as fontes gravitacionais relevantes devem contribuir simultaneamente.

---

# 12. Propagador da nave

Crie uma abstração independente:

```cpp
class SpacecraftPropagator
{
public:
    virtual PropagationResult propagate(
        const PropagationState& initial,
        CoordinateTime from,
        CoordinateTime to
    ) = 0;
};
```

O integrador deve ter:

* passo adaptativo;
* controle de erro;
* tolerância configurável;
* limite mínimo de timestep;
* limite máximo de timestep;
* logging;
* estatísticas numéricas.

Não vincule o timestep ao framerate.

---

# 13. Regime relativístico

Esta parte é crítica.

Não implemente simplesmente:

```cpp
gamma = ...
```

em cima de um movimento Newtoniano.

O estado relativístico da nave deverá ser matematicamente consistente.

Investigue como representação principal:

```text
posição
momento relativístico
massa de repouso
tempo próprio
orientação
```

ou equivalentemente 4-velocidade/4-momento quando apropriado.

Em relatividade especial:

```text
p = γmv
```

e não:

```text
p = mv
```

O propagador deverá garantir matematicamente:

```text
|v| < c
```

sem simplesmente aplicar:

```cpp
if (v > c)
    v = c;
```

Esse tipo de clamp é proibido.

---

# 14. Relatividade especial

Implementar progressivamente:

* fator de Lorentz;
* momento relativístico;
* energia relativística;
* tempo próprio;
* composição relativística de velocidades;
* aceleração própria;
* transformação de referenciais;
* Doppler relativístico;
* aberração relativística.

Manter:

```text
coordinate_time
proper_time
```

como grandezas distintas.

---

# 15. Gravidade relativística

Não assumir que uma aproximação 1PN clássica permanece válida arbitrariamente quando:

```text
v → c
```

Separar os problemas:

## Movimento dos planetas

As posições vêm das efemérides JPL.

## Nave em baixa velocidade

Gravidade Newtoniana de múltiplos corpos é aceitável como primeira implementação.

## Correções gravitacionais relativísticas

Criar módulo separado.

## Nave em velocidade relativística

Antes de implementar, produzir documento técnico específico analisando uma formulação adequada baseada em espaço-tempo de campo fraco / geodésicas ou outra aproximação consistente.

Não inventar equações híbridas.

Qualquer aproximação deverá documentar:

```text
domínio de validade
erro esperado
hipóteses
```

---

# 16. Motor fictício

O motor é fictício.

A matemática não.

Representá-lo como um motor relativístico configurável.

Parâmetros possíveis:

```text
maximum thrust
effective exhaust velocity
mass flow
efficiency
propellant mass
```

Exigir:

```text
0 < effective_exhaust_velocity <= c
```

O combustível deve possuir massa.

Conforme o propelente é consumido:

```text
spacecraft mass ↓
```

---

# 17. Modelo energético

Não aceite simplesmente:

```text
thrust = throttle * maxThrust;
fuel -= arbitraryNumber;
```

Derive uma relação fisicamente consistente entre:

```text
thrust
mass flow
exhaust momentum
energy
effective exhaust velocity
efficiency
```

O motor poderá utilizar tecnologia fictícia como conversão extremamente eficiente de massa em energia.

Mas conservação de:

```text
energia
momento
massa-energia
```

deve ser respeitada dentro do modelo adotado.

---

# 18. Configuração experimental do motor

Os parâmetros deverão ficar fora do código.

Exemplo:

```yaml
engine:
  max_thrust: ...
  exhaust_velocity_fraction_c: ...
  efficiency: ...
  propellant_mass: ...
```

Isso permitirá experimentar diferentes tecnologias.

---

# 19. Atitude

A orientação deverá utilizar:

**quaternions**

Estado:

```text
orientation
angular_velocity
moment_of_inertia
```

O RCS gera:

```text
force
torque
```

Não altere diretamente:

```text
pitch
yaw
roll
```

Eles são apenas representações para a interface.

---

# 20. Nave

Criar modelo lógico independente do modelo gráfico:

```cpp
Spacecraft
├── DryMass
├── PropellantTank
├── MainEngine
├── RCS
├── AttitudeState
├── NavigationComputer
└── Instrumentation
```

---

# 21. Relógio da simulação

Criar:

```cpp
SimulationClock
```

Separando:

```text
wall clock
simulation coordinate time
spacecraft proper time
render time
```

Time warp não deve alterar diretamente o timestep físico.

Exemplo:

```text
1x
10x
100x
1000x
10000x
100000x
```

O propagador continua escolhendo seus próprios passos numéricos.

---

# 22. Renderização

O Godot receberá snapshots.

Exemplo:

```cpp
struct SimulationSnapshot {
    CoordinateTime time;
    SpacecraftSnapshot spacecraft;
    std::vector<CelestialBodySnapshot> bodies;
};
```

A UI nunca deverá consultar diretamente detalhes internos do integrador.

---

# 23. Escala

O core utilizará `double`.

A precisão do Godot não deve limitar a física.

Implemente:

```text
camera-relative rendering
floating origin
local coordinate frames
```

Exemplo conceitual:

```text
Physics:
Earth = 1.4e11 m

Renderer:
Earth = 8500 m relative to camera
```

Nunca modifique a posição física para executar floating origin.

---

# 24. Conversão Core → Renderer

Criar uma camada explícita:

```text
RenderTransform
```

responsável por:

```text
absolute astronomical position
        ↓
camera-relative position
        ↓
Godot Vector3
```

---

# 25. Instrumentação

O cockpit deverá ler dados do SimulationSnapshot.

Instrumentação inicial:

```text
THRUST
THROTTLE
FUEL
MASS

POSITION
VELOCITY
ACCELERATION

TARGET DISTANCE
TARGET RELATIVE VELOCITY

APOAPSIS
PERIAPSIS
ECCENTRICITY
INCLINATION

β
γ

COORDINATE TIME
PROPER TIME
TIME DIFFERENCE
```

---

# 26. Computador de navegação

Criar arquitetura:

```text
NavigationComputer
TrajectoryPlanner
ManeuverPlan
ManeuverExecutor
```

Uma trajetória planejada produz algo como:

```text
Maneuver 1
time
orientation
throttle
duration

Maneuver 2
...
```

O planejador não altera a posição da nave.

---

# 27. Lambert Solver

Para trajetórias interplanetárias convencionais, prever implementação de:

**Lambert problem solver**

Entrada:

```text
departure position
arrival position
time of flight
```

Saída:

```text
required departure velocity
required arrival velocity
```

Posteriormente:

```text
optimization
gravity assists
continuous thrust
relativistic trajectories
```

---

# 28. Piloto automático

Comandos planejados:

```text
PROGRADE
RETROGRADE
NORMAL
ANTI_NORMAL
RADIAL_IN
RADIAL_OUT

POINT_TARGET
POINT_ANTI_TARGET

MATCH_VELOCITY
CIRCULARIZE
ENTER_ORBIT
INTERCEPT
```

Todos devem utilizar:

```text
main engine
RCS
```

Nunca modificar diretamente:

```text
position
velocity
orientation
```

---

# 29. REBOUND / REBOUNDx

REBOUND e REBOUNDx poderão ser incluídos como ferramentas científicas auxiliares.

Usos:

* comparação;
* validação;
* experimentos N-body;
* validação de integradores;
* comparação de correções relativísticas.

Não os torne dependência obrigatória da arquitetura principal sem necessidade.

Em particular, não assuma automaticamente que correções pós-newtonianas para sistemas planetários resolvem o caso de uma nave viajando próxima a `c`.

---

# 30. Validação

Criar um executável:

```text
orbit-cli
```

Exemplo:

```bash
orbit-cli body Earth --date 2026-01-01
```

Retorno:

```text
position
velocity
reference frame
```

Outro:

```bash
orbit-cli propagate scenario.json
```

Isso permitirá testar a simulação sem Godot.

---

# 31. Testes científicos obrigatórios

Criar testes para:

### SPICE

Comparar posições conhecidas.

### Órbita circular

Verificar período e estabilidade.

### Órbita elíptica

Verificar apoastro/periastro.

### Hohmann

Comparar Delta-V analítico.

### Conservação

Sem motor:

```text
specific orbital energy
angular momentum
```

### Relatividade especial

Testar:

```text
0.01c
0.1c
0.5c
0.9c
0.99c
0.999c
```

### Proper time

Comparar contra solução analítica em velocidade constante.

### Limite Newtoniano

Para:

```text
v << c
```

o propagador relativístico deve convergir numericamente para a solução clássica.

---

# 32. Tolerâncias

Nenhum teste científico deve simplesmente usar:

```cpp
EXPECT_NEAR(a, b, 0.01)
```

sem justificativa.

Documente:

```text
valor esperado
erro absoluto
erro relativo
origem da tolerância
```

---

# 33. Milestone 0

Antes de produzir interface gráfica significativa, implementar:

```text
CMake
spaceflight-core
CSPICE
DE440
CoordinateTime
ReferenceFrame
EphemerisProvider
SpiceEphemerisProvider
BodyState
SpacecraftState
PointMassGravity
Propagation interface
SimulationClock
tests
orbit-cli
```

---

# 34. Milestone 1

Criar cenário:

```text
Sun
Earth
Moon
spacecraft
```

A nave começa em órbita terrestre baixa.

Deveremos conseguir pela CLI:

```text
propagar 1 órbita
propagar 1 dia
executar burn
alterar apoastro
escapar da Terra
interceptar região da Lua
```

Ainda sem cockpit complexo.

---

# 35. Milestone 2

Adicionar Godot.

Implementar:

```text
camera externa
Terra
Lua
nave
starfield
floating origin
time warp
```

O Godot deve consumir somente snapshots do core.

---

# 36. Milestone 3

Adicionar cockpit inicial.

Instrumentos:

```text
velocity
acceleration
fuel
throttle
mass
apoapsis
periapsis
target distance
target velocity
```

---

# 37. Milestone 4

Propulsão relativística.

Antes de escrever o código, produzir:

```text
docs/physics/relativistic-propulsion.md
```

contendo:

```text
estado matemático
equações
conservação de momento
consumo de massa
proper acceleration
coordinate acceleration
rocket equation
limites Newtonianos
testes analíticos
```

Só implementar após a formulação estar consistente.

---

# 38. Milestone 5

Renderização relativística.

Criar separadamente:

```text
docs/physics/relativistic-rendering.md
```

Investigar:

```text
light travel time
aberration
Doppler
beaming
apparent position
Terrell rotation
```

Não representar contração de Lorentz simplesmente escalando meshes.

---

# 39. Regra fundamental de desenvolvimento

Toda feature física deve seguir:

```text
modelo matemático
        ↓
documentação
        ↓
teste analítico
        ↓
implementação
        ↓
teste numérico
        ↓
visualização
```

e nunca:

```text
efeito visual
        ↓
inventar física que combine com ele
```

---

# 40. Primeira tarefa

Agora execute somente o Milestone 0.

Antes de escrever código, produza:

```text
docs/architecture/system-architecture.md
docs/architecture/coordinate-system.md
docs/physics/gravity-model.md
docs/physics/relativity-roadmap.md
docs/physics/propulsion-model.md
docs/adr/0001-core-language.md
docs/adr/0002-render-engine.md
docs/adr/0003-ephemeris.md
docs/adr/0004-coordinate-system.md
docs/adr/0005-propagation.md
```

Depois:

1. apresente a estrutura definitiva do repositório;
2. configure CMake;
3. integre CSPICE;
4. carregue DE440;
5. implemente EphemerisProvider;
6. implemente `orbit-cli`;
7. implemente testes;
8. valide pelo menos Sol, Terra e Lua;
9. somente então avance para propagação da nave.

Não crie ainda um jogo bonito.

Primeiro construa uma base científica verificável.

