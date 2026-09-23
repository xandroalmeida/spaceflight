# M8 — Terra → Marte, fotografado

Produzido por `scripts/m8_screenshots.sh` (`spaceflight --shots m8`). Como o M7,
renderiza fora da tela: precisa de um driver de GPU e não de display, e o
`--headless`, que não rasteriza nada, não serve.

**Demora.** A busca leva um a três minutos e o voo são duzentos e quatro dias sob
warp. `SPACEFLIGHT_SHOT_STOP=N` para a sequência no passo N, o que faz uma volta
de ajuste visual custar segundos.

A sequência é **reproduzível**: cada passo é uma condição sobre o estado da
simulação e não um número de quadros escolhido à mão. O roteiro está em
`app/presentation/shot_director.cpp`, em `m8_script()`.

⚠️ **As imagens são evidência, não oráculo** (regra 60). Elas provam que a cena
põe alguma coisa na tela nos momentos certos. Se aquilo está bonito é julgamento
humano, e nem este arquivo nem o script opinam.

| imagem | o que ela tem de mostrar |
|---|---|
| `01-earth-orbit` | o cockpit, a Terra na janela, `TARGET MARS` a 2,41 UA |
| `02-mission-computer-mars` | `MARS` escolhido e `500 km circular` — o default trocou sozinho ao escolher o destino |
| `03-searching` | `SEARCHING TRAJECTORIES — RANKING`, contagens a subir, o botão em `CANCEL SEARCH`, **e o jogo a andar por trás** |
| `04-trajectory-options` | o resumo do plano e a tabela comparativa |
| `05-solar-system-map` | o Sol, os planetas, as órbitas da efeméride, `DEPARTURE` na Terra e `ARRIVAL` sobre a órbita de Marte, com o arco a ligá-los |
| `06-departure-burn` | `INJECTION_BURN`, a 388 km de altitude, 52 minutos após o início |
| `07-interplanetary-cruise` | `COAST` a 0,38 UA, `REFERENCE SUN` |
| `08-cruise-map` | o mesmo mapa, com a nave já ao longo do arco |
| `09-mars-approach` | `APPROACH` |
| `10-mars-capture` | Marte com terminador e regiões de albedo, a nave contra ele, `INSERTION T−00:29:46`, apoapsis `--` porque a órbita ainda é hiperbólica |
| `11-mars-orbit` | `COMPLETE`, `ORBIT ACHIEVED`, AP 504,6 km / PE 492,9 km |
| `12-mars-orbit-external` | a nave em órbita, com Marte atrás |

## O que a sequência aprendeu a fazer, errando

Três coisas neste roteiro existem por causa de fotografias erradas, e valem mais
do que a imagem que produzem:

**A condição da queima de partida é a FASE e não o empuxo.** `thrust_n` vem de
`main_engine_->current_thrust()`, que é o acelerador manual: uma queima conduzida
pelo executor de manobras não aparece ali. A primeira versão esperou doze mil
quadros por um empuxo que nunca ia ser reportado, enquanto a nave, a 117 825 km
de altitude, já tinha feito a injeção.

**O warp desce em dois degraus antes do encontro, e não num.** Os três quadros de
*assentamento* — os que deixam o que a condição descreve chegar à tela — correm
no warp ANTIGO. A 1e6× são cinquenta mil segundos, quase todo o encontro: a
travagem num degrau só disparou a três milhões de quilômetros e ainda assim
fotografou a captura com a missão já `COMPLETE`, em órbita, a 495 km.

**O modo do mapa é POSTO e não alternado.** Um roteiro que descreve estados não
pode chamar uma função que descreve transições: a primeira versão alternava o
modo e devolvia o mapa a `LOCAL` num passo cujo assunto era o mapa do sistema.
Agora o passo chama `set_map_mode(OrbitMap::Mode::System)`.
