# Renderização: do estado à tela

Status: implementado (Milestone 2); ótica relativística no Milestone 5
Decisão de pilha de renderização: ADR-0009 (substitui o ADR-0002)
Ótica: `docs/architecture/relativistic-shaders.md`, `docs/physics/relativistic-rendering.md`
Última revisão: 2026-09-23

## 1. O problema, em números

A simulação guarda posições em metros, em `double`, relativas ao baricentro do
Sistema Solar. A GPU renderiza em `float` de 32 bits, cujo épsilon é
1,19·10⁻⁷ — **relativo**. Isso significa:

| Distância | Resolução do `float` |
|---|---|
| 1 m | 1,2·10⁻⁷ m |
| 10³ m | 1,2·10⁻⁴ m |
| 10⁶ m | 0,12 m |
| 1,5·10¹¹ m (1 UA) | **1,8·10⁴ m** |
| 4,5·10¹² m (Netuno) | **5,4·10⁵ m** |

Se a posição da nave fosse enviada à GPU como está, a nave em órbita terrestre
teria sua posição quantizada em **18 km**. Ela tremeria, atravessaria o planeta,
e o cockpit mostraria números que pulam. Este é o problema clássico de
*precisão em mundo grande*, e ele não se resolve "usando `double` na GPU": os
vértices, as matrizes e os shaders (`app/shaders/`) são `float`.

## 2. A solução, e a regra que a governa

Subtrair a origem da câmera **antes** de converter para `float`:

```
posição absoluta (double, SSB/J2000)
        │  subtrai a posição da câmera  ← ainda em double
        ▼
posição relativa à câmera (double, pequena)
        │  converte
        ▼
float3 para o renderizador (app/gfx)
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

Distâncias astronômicas não cabem no alcance útil do `float` nem numa câmera
com `near`/`far` finitos. Além da origem flutuante, há um **fator de
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

§22 do enunciado: a apresentação (`app/`) recebe *snapshots*, e a UI nunca consulta o integrador.

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
`EphemerisProvider` e do estado propagado. A apresentação nunca chama `spkez_c`.

## 5. Tempo de renderização × tempo de simulação

O `SimulationClock` (Milestone 0) já separa os quatro relógios. O renderizador
usa o **dense output** (ADR-0006) para pedir o estado no instante exato do frame:

```
frame a 60 Hz  →  t_frame = t_anterior + 1/60 · warp
                →  Trajectory::state_at(t_frame)      ← interpolação, sem integrar
                →  build_snapshot(...)
                →  RenderTransform → renderizador (SDL_GPU)
```

O integrador continua escolhendo os próprios passos. A 144 Hz o interpolante é
avaliado mais vezes e **nada mais muda** — nem a trajetória, nem o número de
avaliações de força. Foi exatamente para isso que a ADR-0006 existe.

## 6. O que era falso na imagem, e o que ainda é

Esta seção era, do Milestone 2 ao 5, a lista de dívidas da imagem. O Milestone 5
pagou as três primeiras; o registro fica porque a forma como foram pagas é a
parte que importa.

**Pago** (`docs/physics/relativistic-rendering.md`,
`docs/architecture/relativistic-shaders.md`):

* ~~Posições são geométricas, não aparentes.~~ Agora são aparentes: tempo de
  trânsito resolvido contra a efeméride real (§2) e aberração exata (§3). A Lua
  aparece 1,195 s-luz atrás de onde está; Júpiter, entre 33 e 53 minutos.
* ~~Não há Doppler nem beaming.~~ `D` sai de `optics.hpp` na CPU e o shader o
  transforma em cor (`T' = D·T`) e brilho (`D⁴`, corrigido para a banda visível,
  §10). O céu à frente a `β = 0,9048` fica 51× mais brilhante — não 400×, que é o
  número bolométrico.
* ~~Não há contração de Lorentz.~~ Há, como **passo** dentro da transformação por
  vértice, e não como resposta (§11.5). O que se vê é a rotação de Terrell,
  `arcsin β`, que ninguém programou.
* ~~O starfield é aleatório.~~ 8 786 estrelas reais do Yale BSC5 (§12).

**Ainda falso**, e registrado aqui pelo mesmo motivo que os anteriores estiveram:

* **Aberração rígida por corpo.** O disco de um planeta não se distorce ao
  atravessar o mapa de aberração; só o centro se move. Irrelevante exceto num
  sobrevoo rasante a `β` alto.
* **Sem lente gravitacional nem atraso de Shapiro.** A luz anda em linha reta,
  inclusive ao passar perto do Sol.
* **Estrelas são corpos negros.** Não têm linhas espectrais, e o deslocamento de
  uma linha não é deslocamento de temperatura.
* **Sem extinção interestelar, sem atmosfera, sem eclipses.** Nada disso afeta a
  dinâmica; tudo isso afeta a imagem.

## 7. Limites da camada

```
core  ─────────────────────────────►  RenderTransform  ─────►  app/gfx
 estado autoritativo                  projeção pura            desenha
 double, SSB/J2000, metros            double → float           float
 nunca sabe que há câmera             nunca escreve no estado  nunca integra
```

Se algum dia esta seta apontar para trás — o renderizador escrevendo no estado,
ou a apresentação decidindo um passo de integração — a separação que este projeto
inteiro sustenta terá sido perdida, e o sintoma será uma física que muda com a
taxa de quadros.
