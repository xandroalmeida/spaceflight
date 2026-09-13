# Renderização: do estado à tela

Status: camada de núcleo implementada (Milestone 2, metade científica)
Decisão de engine: ADR-0002
Última revisão: 2026-09-13

## 1. O problema, em números

A simulação guarda posições em metros, em `double`, relativas ao baricentro do
Sistema Solar. O Godot renderiza em `float` de 32 bits, cujo épsilon é
1,19·10⁻⁷ — **relativo**. Isso significa:

| Distância | Resolução do `float` |
|---|---|
| 1 m | 1,2·10⁻⁷ m |
| 10³ m | 1,2·10⁻⁴ m |
| 10⁶ m | 0,12 m |
| 1,5·10¹¹ m (1 UA) | **1,8·10⁴ m** |
| 4,5·10¹² m (Netuno) | **5,4·10⁵ m** |

Se a posição da nave fosse enviada ao Godot como está, a nave em órbita terrestre
teria sua posição quantizada em **18 km**. Ela tremeria, atravessaria o planeta,
e o cockpit mostraria números que pulam. Este é o problema clássico de
*precisão em mundo grande*, e ele não se resolve "usando `double` no Godot": o
`Transform3D` do Godot é `float`, e os shaders são `float`.

## 2. A solução, e a regra que a governa

Subtrair a origem da câmera **antes** de converter para `float`:

```
posição absoluta (double, SSB/J2000)
        │  subtrai a posição da câmera  ← ainda em double
        ▼
posição relativa à câmera (double, pequena)
        │  converte
        ▼
float3 para o Godot
```

A subtração acontece em `double`, então a precisão do resultado é a do `double`
na escala **absoluta** (3,3·10⁻⁵ m a 1 UA), e a conversão para `float` acontece
sobre um número **pequeno** (metros a quilômetros), onde o `float` resolve
frações de milímetro.

§23 do enunciado dá a regra em uma linha:

> Nunca modifique a posição física para executar floating origin.

O `RenderTransform` **lê** o estado. Não existe caminho de escrita. O estado
físico continua em `SSB/J2000`, em metros, em `double`, e continua idêntico quer
a câmera esteja onde estiver, quer não haja câmera nenhuma. Isso é testado:
`tests/unit/test_render_transform.cpp` propaga a mesma trajetória com e sem
projeção e compara bit a bit.

## 3. Escala

Distâncias astronômicas não cabem no alcance útil do `float` nem na câmera do
Godot (que tem `near`/`far` finitos). Além da origem flutuante, há um **fator de
escala** aplicado depois da subtração:

```
render = (r_absoluto − r_câmera) · escala
```

Com `escala = 10⁻⁷`, a Terra a 1,5·10¹¹ m da câmera cai em 1,5·10⁴ unidades de
cena — dentro do alcance de um `far` razoável. O exemplo do enunciado:

```
Física:       Terra = 1,4·10¹¹ m
Renderizador: Terra = 8500 unidades relativas à câmera
```

A escala é **de apresentação**, escolhida por quem desenha a cena, e não tem
significado físico. Por isso ela vive no `RenderTransform` e não no core
científico: mudar a escala não pode mudar uma trajetória.

⚠️ **A escala não melhora a precisão.** O `float` tem precisão *relativa*: um
ponto a 10⁴ unidades de cena e outro a 10⁷ unidades perdem a mesma fração do
valor. A resolução, medida de volta em metros, é

```
resolução ≈ |r − r_câmera| · ε_float = |r − r_câmera| · 1,19·10⁻⁷
```

— **independente da escala**. Só a distância à câmera importa. A 1 UA da câmera,
1,8·10⁴ m; a 6 778 km (a nave vista de uma câmera na Terra), 0,81 m; a 100 m da
nave, 1,2·10⁻⁵ m. É por isso que a origem flutuante acompanha a **câmera**, e não
o corpo central: o que a câmera está olhando de perto é o que precisa ser preciso.

Nota prática: o raio de um corpo é escalado pelo **mesmo** fator, senão os
tamanhos relativos mentem. Um simulador que aumenta os planetas para que apareçam
está fazendo uma escolha de gameplay, e essa escolha precisa ser um parâmetro
explícito (`body_scale_exaggeration`), não um número escondido no renderizador.

## 4. O contrato: `SimulationSnapshot`

§22 do enunciado: o Godot recebe *snapshots*, e a UI nunca consulta o integrador.

```cpp
struct SimulationSnapshot {
    time::CoordinateTime time;          // TDB
    time::Duration elapsed_coordinate;
    time::Duration proper_time;         // relógio da nave
    SpacecraftSnapshot spacecraft;
    std::vector<CelestialBodySnapshot> bodies;
};
```

Propriedades deliberadas:

* é um **valor**, não uma janela para dentro do núcleo. Copiar é barato
  (dezenas de corpos), e o consumidor não pode acidentalmente segurar uma
  referência para um estado que o propagador vai sobrescrever;
* traz tudo o que o cockpit do Milestone 3 precisa (§25) — posição, velocidade,
  aceleração, massa, elementos osculadores, β, γ, tempo coordenado e próprio —
  calculado **uma vez** por frame, no core, e não recalculado por cada mostrador;
* `beta` e `lorentz_factor` já existem e valem 0 e 1 no regime newtoniano. São a
  mesma decisão de `proper_time` no Milestone 0: o dia em que a física relativística
  entrar, o contrato não muda.

Quem constrói o snapshot é `build_snapshot(...)`, no core, a partir do
`EphemerisProvider` e do estado propagado. O Godot nunca chama `spkez_c`.

## 5. Tempo de renderização × tempo de simulação

O `SimulationClock` (Milestone 0) já separa os quatro relógios. O renderizador
usa o **dense output** (ADR-0006) para pedir o estado no instante exato do frame:

```
frame a 60 Hz  →  t_frame = t_anterior + 1/60 · warp
                →  Trajectory::state_at(t_frame)      ← interpolação, sem integrar
                →  build_snapshot(...)
                →  RenderTransform → Godot
```

O integrador continua escolhendo os próprios passos. A 144 Hz o interpolante é
avaliado mais vezes e **nada mais muda** — nem a trajetória, nem o número de
avaliações de força. Foi exatamente para isso que a ADR-0006 existe.

## 6. O que ainda é falso na imagem

Honestidade sobre o que a projeção **não** faz, para que não seja descoberto como
bug no Milestone 5:

* **Posições são geométricas, não aparentes.** Vemos os corpos onde eles estão no
  instante `t`, não onde a luz que chega agora saiu. A Lua está 1,28 s-luz de
  distância; Júpiter, entre 33 e 53 minutos. O tempo de trânsito da luz, a
  aberração, o Doppler e o *beaming* são o Milestone 5 e terão documento próprio
  (`docs/physics/relativistic-rendering.md`).
* **Não há contração de Lorentz.** E quando houver, não será escalando meshes
  (§38) — a aparência de um objeto em movimento relativístico é dominada pela
  rotação de Terrell, não pela contração.
* **Sem atmosfera, sem iluminação física, sem eclipses.** Nada disso afeta a
  dinâmica; tudo isso afeta a imagem.

## 7. Limites da camada

```
core  ─────────────────────────────►  RenderTransform  ─────►  Godot
 estado autoritativo                  projeção pura            desenha
 double, SSB/J2000, metros            double → float           float
 nunca sabe que há câmera             nunca escreve no estado  nunca integra
```

Se algum dia esta seta apontar para trás — o renderizador escrevendo no estado,
ou o Godot decidindo um passo de integração — a separação que este projeto
inteiro sustenta terá sido perdida, e o sintoma será uma física que muda com a
taxa de quadros.
