# M7 — o que falta para o vertical slice

Levantado **antes** de escrever código, olhando o que a cena do M6 faz hoje
(`godot/project/main.gd`, 1520 linhas) e o que a ponte C++ expõe
(`SpaceflightSimulation`, `SpaceflightSky`).

## O que já existe e vai ser preservado

| já funciona | onde | destino no M7 |
|---|---|---|
| snapshot completo do core → Godot | `get_snapshot()`, 50 campos | alimenta todos os instrumentos |
| starfield BSC5 + aberração/Doppler/beaming | `SpaceflightSky`, `star_field.gdshader` | **não reescrever** (regra 34) |
| posições aparentes, tempo de luz, Terrell | `get_body_observed_position` | mantidas em todas as câmeras |
| atitude real (quatérnio, RCS, inércia) | `get_spacecraft_basis`, `set_manual_torque` | controle manual e visual dos jatos |
| motor de dois modos, empuxo real | `set_throttle`, `thrust_n` | throttle visual + pluma |
| planejador Terra–Lua completo | `plan_transfer`, `get_plan`, `get_plan_alternatives` | computador de bordo |
| fases da missão | `get_mission_phase` | HUD de missão |
| trajetória planejada em unidades de cena | `get_planned_trajectory` | mapa orbital |
| verificação headless | `scripts/run_godot_headless.sh` | **não pode quebrar** |

## Gaps — o que impede o roteiro do §83

| # | gap | classe | resolução |
|---|---|---|---|
| 1 | a nave é uma **caixa amarela** de 20 km | `VISUAL_DEBT` bloqueante do DoD 10 | `SpacecraftVisual` procedural |
| 2 | **não existe cockpit** | bloqueante do DoD 2 | `CockpitInterior` procedural + displays |
| 3 | só existe câmera orbital externa; sem câmera de piloto | bloqueante do DoD 4, 9 | `CameraRig` com 5 modos |
| 4 | UI é **uma parede de 52 números** monoespaçados | bloqueante do DoD 5 | instrumentos por função; a parede vira o HUD de debug (F3) |
| 5 | teclas espalhadas em `match keycode` | `IMPROVEMENT`, atrapalha tudo | `InputMap` central (regra 13) |
| 6 | **sem representação visual do motor** ligado | bloqueante do DoD 7 | `EnginePlume`, intensidade = `thrust_n` |
| 7 | **sem representação visual do RCS** | bloqueante do DoD 8 | `RcsVisual`, aceso pelo atuador — exige nova API |
| 8 | alvo fixo na Lua no C++ (`builder_->set_target(moon)`) | bloqueante do DoD 11 | `set_target_body()` na ponte |
| 9 | sem mapa orbital | bloqueante do DoD 13 | `OrbitMap`, pontos vindos do core — exige `get_orbit_track()` |
| 10 | plano só aparece como texto no HUD gigante | bloqueante do DoD 12, 13 | `MissionPanel` com EXECUTE/CANCEL |
| 11 | **sem áudio** | DoD — feedback | placeholders procedurais |
| 12 | Terra é uma esfera azul lisa | `VISUAL_DEBT` do DoD 3 | textura procedural + nuvens + lado noturno |
| 13 | sem pausa, sem menu, sem settings | DoD — usabilidade | `PauseMenu` |
| 14 | sem marcadores de manobra no mapa | DoD 13 | exige `get_maneuvers()` |
| 15 | HUD dimensionado para 1440×900 | §59 | já se ajusta à fonte; validar em 1080p/1440p/4K |

## O que a ponte C++ precisa ganhar

Cinco entradas, nenhuma delas física nova — todas expõem o que o core já
calcula (regra 77):

1. `set_target_body(name)` / `get_target_body()` / `get_selectable_targets()`
   — o alvo deixa de ser fixo na Lua.
2. `get_rcs_thrusters()` + `get_rcs_throttles()` — geometria e **acionamento
   real** de cada um dos doze thrusters, pelo mesmo alocador que voa
   (`RcsSystem::allocate`). O renderer acende o que o atuador acendeu, não o
   que a tecla pediu (regra 15).
3. `get_orbit_track(samples)` — a elipse osculadora amostrada por
   `trajectory::state_from_elements`, em unidades de cena. Godot não integra
   Kepler (regra 54).
4. `get_body_orbit_track(index, samples)` — o caminho do alvo em torno do
   corpo de referência, amostrado da **efeméride real**.
5. `get_maneuvers()` — as queimas do plano com época, duração, Δv e rótulo.

## O que fica fora

Tudo do §57, mais: modelo 3D artístico da nave, texturas definitivas de
planeta, interior completo habitável, som espacial externo.
