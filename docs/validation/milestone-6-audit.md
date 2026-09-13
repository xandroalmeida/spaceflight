# Milestone 6 — resultado da auditoria

Data: 2026-09-13

## Defeitos falsificados e corrigidos

1. **Conversão de velocidade no contorno da métrica.** A integração usava a
   métrica curva, mas entrada, saída e dense output convertiam `v↔u` com a
   relação de Minkowski. O erro era `O(U/c²)` e um teste LEO circular o
   compensava acidentalmente com um oráculo inadequado. A conversão agora usa
   `A/B`, verifica o cone de luz local e o teste usa a diferença analítica da
   frequência circular; Mercúrio continua sendo o teste de precessão apsidal.
2. **Tempo retardado a velocidade alta.** A iteração de ponto fixo deixava de
   convergir para uma fonte recedendo a `0.99c`. O solver agora usa Newton com a
   velocidade da efeméride e concorda com a raiz fechada independente.

## Verificações concluídas

- matriz `beta=0, .01, .1, .5, .9, .99, .999` em deep space, 1 AU solar,
  Mercúrio, LEO e Júpiter: velocidade coordenada/própria, gamma, tempos,
  acelerações, energia e momento contra álgebra `long double` independente;
- convergência `1e-6…1e-13`, invariantes e particionamento FPS/warp;
- matriz de estresse com `e=.99`, quase escape, flyby a 10 km, empuxo/RCS,
  esgotamento, `beta=.999999999` e coast de 100 anos;
- determinismo bit a bit do propagador e do corretor de partida;
- round-trip de origem SSB, heliocêntrica, geocêntrica e lunocêntrica,
  incluindo velocidades, em três épocas, mais rotação independente dos eixos
  J2000↔ECLIPJ2000;
- 27 estados contra Horizons/DE441 e 100 geometrias Terra→Lua;
- ferramenta visual 0/.1/.5/.9/.99c com quatro efeitos independentes.

## Resultado que impede declarar robustez lunar

A campanha Terra→Lua teve apenas **18% de sucesso estrito**. Esse é o achado
mais importante do milestone: uma geometria nominal bem-sucedida não generaliza
para épocas vizinhas. Os modos de falha e distribuições completos estão em
`lunar-mission-campaign.md`; novas features de missão devem esperar a investigação
do corretor, busca de partida e inserção.

## Deliberadamente não implementado

- frame dragging: formulação e gates em `../physics/frame-dragging.md`;
- otimização do ângulo B: problema em `../physics/b-plane-optimization.md`;
- harmônicos lunares: qualificação progressiva em `../physics/lunar-gravity-model.md`.

Adicionar esses modelos antes de seus oráculos independentes contrariaria o
objetivo desta fase.
