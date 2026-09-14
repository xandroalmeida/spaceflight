# Milestone 7 — First Playable Cockpit

> Relatório orientado a produto (regra 82). O que dá para jogar, o que dá para
> ver, o que funciona, e o que ficou por fazer — com os defeitos classificados em
> vez de escondidos.

---

## WHAT IS PLAYABLE

O roteiro do §83 corre de ponta a ponta:

```
abrir o simulador
  ↓  o cockpit aparece, com a Terra pela janela
pilotar à mão            W A S D Q E giram a nave, I J K L U O transladam
  ↓
vista externa            C, e a nave inteira é visível
  ↓
motor                    Shift/Ctrl/Z/X, com pluma cujo tamanho segue o empuxo
  ↓
RCS                      os jatos que acendem são os que o alocador abriu
  ↓
escolher a Lua           Tab, ◀ ▶
  ↓
planejar                 PLAN TRANSFER (o quadro para ~1 s, e avisa antes)
  ↓
inspecionar              Δv por queima, propelente, órbita prevista, candidatos
  ↓
executar                 EXECUTE, e o piloto automático assume a atitude
  ↓
warp durante o cruzeiro  ., com a fase e a contagem no canto
  ↓
ver a Lua crescer        câmera TARGET
  ↓
queima de captura
  ↓
órbita lunar
```

A sequência inteira é **voada sozinha e fotografada** por
`scripts/m7_screenshots.sh`, que é reproduzível: cada passo é uma condição sobre
o estado da simulação, não um número de quadros escolhido à mão. As imagens
estão em `docs/validation/m7/`.

O plano medido numa corrida típica, a partir de 400 km / 51,6° em
2026-01-01T00:00:00 TDB:

```
2 burns   injection 5713 m/s em 88,0 min   insertion 831 m/s
prograde  tof 4,75 d   transfer angle 150,8°   total 6544 m/s
predicted 96,7 × 103,0 km, e 0,0017, i 19,38°, RAAN 307,3°
```

---

## WHAT IS VISIBLE

| | antes (M6) | agora |
|---|---|---|
| a nave | uma **caixa amarela** de 20 km | 22 m de veículo: cockpit, módulo de comando, habitat, treliça, dois tanques, seção de potência, motor com sino, dois radiadores, antena de alto ganho, painéis solares, seis pods de RCS, luzes de navegação |
| o cockpit | **não existia** | interior 3D: janelas frontais e laterais, quebra-luz, painel inclinado com quatro mostradores, sete botões clicáveis, quatro lâmpadas, consoles laterais, painel superior, escotilha |
| a Terra | uma esfera azul lisa | Blue Marble a 2048×1024, nuvens com ZCIT e ciclones, luzes de cidade no lado noturno, limbo atmosférico; **rodando**, pelo `pxform_c` sobre os mesmos kernels, com a orientação conferida contra o ponto subsolar |
| a Lua | uma esfera cinza lisa | mosaico LROC: mares na face visível, terras altas na oculta, raios de Tycho |
| o casco e o painel | cor lisa | texturas ladrilháveis com costuras, fixadores e passagens de cabo |
| o motor | nada | pluma cônica com núcleo, disco de choque e luz, cujo comprimento e brilho seguem o **empuxo real** |
| o RCS | nada | doze jatos, acesos um a um pelo **acionamento** |
| a interface | 52 números monoespaçados | quatro instrumentos por função, HUD mínimo, mapa orbital, computador de missão, menu |
| a relatividade | uma linha no meio da parede de números | uma célula do painel: β, γ−1, Δt entre o relógio coordenado e o próprio, e a aceleração PRÓPRIA — empuxo sobre massa, zero em queda livre, e não os 8,7 m/s² da aceleração coordenada |

### As duas escalas

A cena tem 1 unidade = 10⁶ m e o plano próximo da câmera do mundo é 0,05 —
50 km — porque com profundidade invertida de 24 bits um plano menor perde as
estrelas por quantização. A nave tem 22 m: duas mil vezes **dentro** desse
plano. Não existe uma câmera só que desenhe a Lua a 358 unidades e um painel a
meio metro do rosto.

São duas, com a mesma orientação e o mesmo campo de visão, e a conversão entre
elas é uma multiplicação num lugar só. Elas não são "uma a seguir à outra": são
a mesma câmera expressa em duas unidades, e é por isso que a paralaxe entre a
nave e o planeta atrás dela sai certa em vez de ser ajustada.

---

## WHAT WORKS

### O renderizador consome o estado dos atuadores, e isso é verificável

Regra 15. `RcsForce::throttles()` é **a mesma função** que o modelo de forças
usa e que o renderizador lê; não há arranjo de código em que o jato desenhado e
o propelente queimado discordem. O teste
`tests/test_m7.gd::_test_rcs_actuator_mapping` fixa o comportamento:

```
um torque puro em +x       abre exatamente 2 bicos
um comando diagonal        abre mais do que 2, a frações diferentes
sem comando                abre 0
uma translação pura         abre bicos sem produzir torque
```

O caso "sem comando abre 0" está lá de propósito: é o que uma implementação
guiada pela tecla acertaria por acidente. Os outros três não.

### Os marcadores do mostrador de voo vêm do piloto automático

`PointingController::direction_for` — a mesma rotina pela qual o autopilot guia.
Alinhar o nariz com o marcador leva a nave ao mesmo sítio por construção, não por
coincidência. Um segundo conjunto de definições no renderizador concordaria até
ao dia em que uma delas fosse corrigida.

### As trajetórias vêm do core

O mapa desenha três conjuntos de pontos e não calcula nenhum:

| | de onde |
|---|---|
| órbita atual | `get_orbit_track()` → `trajectory::state_from_elements` |
| caminho do alvo | `get_body_orbit_track()` → a **efeméride**, amostrada |
| transferência planejada | `get_planned_trajectory()` → o arco que o planejador voou |
| queimas | `get_maneuvers()` → as épocas do plano |

O caminho da Lua é amostrado da efeméride e não de uma elipse, e a diferença é
observável: o teste verifica que o perigeu e o apogeu lunares diferem em mais de
10 000 km, que é o que uma elipse desenhada não daria.

### A verificação do Milestone 5 continua idêntica

`./scripts/run_godot_headless.sh` imprime as mesmas afirmações, produzidas pelo
mesmo código:

```
forward cone 84,859° / 25,204° / 8,110°     = arccos β
4309 estrelas nos três                      o conjunto é invariante
5800 K: 51,48× à frente, 3,295e-7 à ré      banda visível, não bolométrico
v = 7672,592 m/s a 400 km                   = √(GM/r)
render res. 0,8072 m por ulp                = 6771 km · ε_float
```

### A suíte de apresentação

Uma terceira suíte, ao lado da headless e da de GPU:

```bash
cmake --build build --target godot-tests     # 140 verificações, sem tela
```

Ela não é aritmética e não é pixels: verifica que o snapshot chega ao cockpit com
os campos que os instrumentos leem, que a seleção de alvo recusa um corpo
inexistente com o motivo, que a câmera volta ao preset, que o teste de acerto dos
botões rejeita um painel que está nas costas do piloto, que a órbita desenhada
fecha na elipse que o snapshot reporta, e que o arco planejado chega à Lua em vez
de a treze milhões de quilômetros.

Uma delas mede **geometria de renderização** sem olhar para um pixel: a AABB
transformada da pluma tem de estender-se 14 m para trás do bocal e ser mais
comprida do que larga. É a asserção que teria apanhado o disco de 26 m × 2,6 m
que a fotografia do motor encontrou.

---

## O QUE MUDOU EM `core/`, E O QUE NÃO

Cinco adições, todas expondo o que o core já calculava. Nenhum modelo foi
redesenhado (regra 2).

| adição | por que ela existe |
|---|---|
| `RcsSystem::allocate_force()` e `max_force_about()` | os controles de translação da regra 13. O layout em binários **já** permitia empurrar sem girar — dois bicos do mesmo lado dão 2F com torques que se cancelam exatamente — e faltava só quem pedisse |
| `RcsForce::throttles()` | público, e é o ponto: `evaluate()` chama-o e o renderizador também. Não há duas alocações que possam divergir |
| `PointingController::direction_for()` | os marcadores do mostrador de voo, pela rotina que o autopilot usa |
| `SpiceEphemerisProvider::body_fixed_rotation()` | a rotação da Terra. **Fora** de `BodyOrientationProvider`, de propósito |
| `align_attitude_to_flight()` no bridge | a atitude LVLH do primeiro quadro. Escreve o quatérnio diretamente, o que é legítimo em preparação de cenário e em mais lado nenhum |

E uma **correção**: `get_planned_trajectory()` passou a devolver o arco relativo
ao corpo de origem (ver acima).

O que ficou intocado: Lambert, o corretor diferencial, o plano B, a inserção, o
modelo de gravidade, a relatividade, a ótica, o integrador, as tolerâncias, e
`allocate()`. As 38 suítes passam, e `scientific` custou 853 s contra 836 s
antes — 2 %, dentro do ruído de uma máquina que também estava a desenhar.

## CONTROLS

A tabela completa é **gerada** a partir de `scripts/input_actions.gd`:

* no jogo: `F1`
* no repositório: [`docs/gameplay/controls.md`](../gameplay/controls.md), por
  `scripts/dump_controls.sh`

O essencial, e a distinção que mais importa: **`WASD` move a nave e gasta
propelente; as setas movem a câmera e não mudam um número do estado.**

O que mudou em relação ao M5 e porquê: a regra 13 atribui `W A S D Q E` à atitude
e `I J K L U O` à translação, e essas dez teclas estavam ocupadas pela câmera,
pela exposição e pelos interruptores de ótica. O voo ganhou o teclado principal;
as ferramentas técnicas desceram para as teclas de função e para `Alt`. **Nada
foi removido.**

---

## EARTH→MOON WALKTHROUGH

1. **Abra.** O cockpit aparece com a Terra na janela. A nave parte com o nariz
   25° abaixo do prógrado — de 400 km, o limbo da Terra fica a 19,7° abaixo da
   horizontal local, e um nariz exatamente no prógrado põe o planeta inteiro
   debaixo do peitoril.
2. **Olhe em volta** (arrastar com o botão direito). Os três mostradores estão a
   −28° da linha de visada, a faixa de sistemas a −43°, os botões a −49°.
3. **`Tab`** abre o computador. A Lua já é o alvo; `◀ ▶` trocam.
4. **`PLAN TRANSFER`.** O botão muda para `PLANNING…` e o **quadro seguinte**
   bloqueia por cerca de um segundo. É uma operação de missão: busca de
   oportunidades de partida e duas inversões do modelo completo, dezenas de
   propagações.
5. **Leia.** Partida, chegada, tempo de voo, Δv por queima, propelente,
   órbita prevista, e a lista de geometrias que a busca **de facto voou**.
6. **`EXECUTE`.** A fase aparece no canto com a contagem para a próxima queima.
7. **`.` várias vezes.** A injeção acontece sozinha; a nave orienta-se, a pluma
   acende, e depois vem o cruzeiro.
8. **`C` até `TARGET`.** A Lua cresce.
9. **A captura.** A fase passa a `CAPTURE` e depois a `ORBIT_INSERTION`.
10. **`F3`** confirma: `about Moon CAPTURED`, com a órbita e a tabela
    previsto-contra-realizado.

---

## KNOWN BUGS

| | classe | onde |
|---|---|---|
| `POINT TARGET` (`T`) responde que não está disponível em vez de apontar | `IMPROVEMENT` | o controlador do core aceita leis de guiamento, e "para onde a Lua está" não é uma lei. A tecla existe e falha em voz alta, que é melhor do que não existir e melhor do que inventar um modo de guiamento no renderizador |
| a altitude do periapsis fica negativa a caminho da Lua | não é bug | os elementos do cockpit são sobre o corpo de **referência**, que continua a ser a Terra; o periapsis osculador está dentro do planeta e a altitude é negativa. `get_orbit_about_target()` dá a órbita que interessa quando ela existe |
| a sequência de capturas pode terminar ainda em `APPROACH` | `BUG` | quando a máquina é lenta, o orçamento de quadros da última etapa acaba antes da inserção. A etapa fotografa e **diz** que a condição não foi atingida, em vez de abortar sem deixar imagem |
| `Shift` é acelerador e é modificador de seis comandos | resolvido | a colisão vem do mapa da própria regra 13. O trim só arma depois de 0,2 s com o modificador sozinho, e fica bloqueado se um comando modificado disparar durante o toque ([controls.md](../gameplay/controls.md)) |
| a camada próxima e a distante não trocam profundidade | `VISUAL_DEBT`, abaixo | |

---

## AS TEXTURAS CHEGARAM, E O QUE ELAS ENCONTRARAM

Sete imagens externas foram entregues; seis estão integradas e a sétima foi
**recusada por medição**. Todas passaram por conferência antes de serem ligadas,
e a conferência é um alvo do projeto e não um gesto de uma vez:

```bash
cmake --build build --target asset-validation    # 21 verificações, 3,7 s
```

`scripts/validate_textures.py` decodifica os PNG em Python puro — sem engine, sem
kernels, sem tela — e mede proporção, costura, polos, convenção de normal map e o
registro entre o albedo da Terra e as suas luzes noturnas.

| asset | verdito |
|---|---|
| `earth_albedo` | 2048×1024, 2:1, polos uniformes (espalhamento 19/24 contra 152 no meio) |
| `earth_clouds` | cinza de 1 canal, estrutura correta (ZCIT, ciclones, frentes); tom remapeado no shader |
| `earth_night` | 90,6 % preto, âmbar (R>G>B), registra com o albedo em longitude (`dx = 0`) |
| `moon_albedo` | mares na face visível, terras altas nas bordas, raios de Tycho |
| `hull_panels` | 1024×1024, ladrilhável nos dois sentidos |
| `panel_surface` | 1024×1024, **sem um único caractere** (regra 44) |
| `moon_normal` | **recusado**, e depois **substituído por um derivado do LOLA** |

### `moon_normal`: o gerador de imagens era a ferramenta errada

A imagem entregue parecia um normal map — azul-lavanda, relevo em R e G,
registrada com o albedo — e não era. Medido nos três mares, que são as
superfícies mais lisas da Lua e onde um normal map correto vale
`(128, 128, 255)` com |n| = 1:

```
                        pintado                   derivado do LOLA
Mare Imbrium         50,7  54,0 246,5  1,290    131,0 126,5 254,5  1,000
Oceanus Procellarum  44,9  52,2 247,7  1,293    127,3 127,7 255,0  1,000
Mare Serenitatis     47,3  54,8 247,2  1,266    127,3 127,7 255,0  1,000
```

No pintado, R e G seguiam o **albedo** — escuro dava 50, claro dava 94 — e os
vetores iam de 0,68 a 1,29 de comprimento. A causa não é o prompt: **uma normal
é uma quantidade calculada a partir de um campo de altura**, e um gerador de
imagens não tem de onde tirar a altura. Ele pinta o que um normal map parece.

O asset foi então **calculado**, da topografia medida:

```bash
./scripts/fetch_lunar_dem.sh          # LOLA LDEM_16, 5760 x 2880, 33 MB
python3 scripts/make_moon_normal.py   # 2,6 s, Python puro
```

Relevo real de −8 937 a +10 522 m em torno de 1737,4 km; declives a
2048 × 1024 de p50 2,7°, p90 9,0°, p99 15,1° — sem exageração vertical. Duas
coisas que a conta tem de acertar e que quase sempre se erram: a métrica
esférica (uma coluna vale `R·cos(φ)·Δλ` de chão, e ignorar o `cos(φ)` exagera as
encostas leste-oeste por `1/cos(φ)` — seis vezes a 80 graus) e o comportamento
nos polos (onde a correção não é limitar o `cos`, é alargar o estêncil em
longitude na mesma proporção).

O `.import` usa RGTC, que guarda R e G e reconstrói o azul — correto **só**
porque |n| = 1 exatamente, o que o validador mede.

### Três defeitos que só existiam porque a esfera era lisa

| defeito | como foi apanhado |
|---|---|
| a rotação dos corpos estava **transposta** — as LINHAS da matriz do SPICE passadas a um construtor de `Basis` que recebe COLUNAS. A Terra girava ao contrário | contra um facto externo: às 00:00 UTC o ponto subsolar tem de estar perto de 180° E, e a latitude perto de −23° em janeiro. Agora dá `180,92 E, −23,01` |
| o **polo da malha** é o `+y` de `SphereMesh` e o polo do corpo é o `+z` fixo ao corpo: sem conversão, as calotas polares iam para o equador | a cadeia inteira fechada: o subsolar cai em `uv (0,003; 0,628)` — oceano — e o antissolar em `uv (0,503; 0,372)`, o Saara |
| a **costura de UV** desenhava uma linha de polo a polo, porque em `u = 0 ≡ 1` a derivada salta e a GPU escolhe o mipmap mais grosseiro | visível na primeira captura do planeta inteiro, cortando o Pacífico na longitude ±180. Corrigida com `textureGrad` e derivada desembrulhada |

E um quarto, do mesmo tipo: a tecla de **foco num corpo** (`F`) construía o
referencial da câmera com `Vector3.FORWARD` e o polo J2000 como "cima" — e o
produto vetorial dos dois é o vetor nulo. A câmera ficava no centro do planeta,
a tela ficava preta, e nada reportava erro. Fixado por teste.

## O QUE AS CAPTURAS ENCONTRARAM

Treze defeitos que só uma imagem podia achar, e é por isso que a regra 60 pede
imagens. Todos estão comentados no ponto onde acontecem.

| a captura mostrava | a causa |
|---|---|
| `γ-1 5.02e` e `a 0.00 m`, cortados na borda | três algarismos significativos na célula de relatividade não cabem em 294 px de mostrador. O HUD técnico tem os dígitos todos; o cockpit tem dois |
| quatro retângulos cinzentos onde deviam estar os mostradores | um `Control` com âncoras `FULL_RECT` e sem `size` explícito fica com tamanho zero até à primeira passagem de layout; `_draw` não pinta nada e o que se vê é a cor de limpeza do `SubViewport` |
| um mapa com a Lua em cima da Terra e a transferência como um risco | o arco planejado vinha em coordenadas ABSOLUTAS na época de cada amostra. A Terra anda 30 km/s: nos 4,75 dias do voo ela percorre 12,7 milhões de km, e o mapa desenhava a viagem à Lua como uma linha até 13,2 milhões de km contra uma Lua a 361 mil |
| um retângulo castanho uniforme | `CylinderMesh` nasce **com tampas**, e a tampa traseira da carenagem é um disco de 1,05 m a 85 cm do olho: 51° de meio-ângulo contra os 34° da câmera |
| céu vazio no primeiro quadro | a atitude de partida do cenário é a identidade, e nela o nariz aponta para o zênite. De 400 km o limbo fica 19,7° abaixo da horizontal, então mesmo o prógrado exato põe o planeta debaixo do peitoril |
| um painel sem mostradores | o bisel é uma **caixa** de 12 mm centrada em z = 0 e a tela estava a +4 mm, dentro dela |
| a faixa de sistemas cheia de caixas brancas | `Palette.mono_font()` construía um `SystemFont` novo a cada `draw_string`, e um `SystemFont` que ainda não resolveu a face desenha `.notdef` |
| um triângulo de espaço no canto do teto | o teto acabava meio metro antes da janela, e o viewport próximo é transparente onde não há geometria — que é o que faz a janela funcionar |
| o motor a **200 kN sem pluma nenhuma**, e seis bicos de RCS abertos sem um jato | um `SubViewport` transparente devolve cor **já multiplicada pelo alfa**; compô-lo com a mistura normal multiplica outra vez. Tudo o que é ADITIVO escreve cor sem escrever alfa, e alfa zero apagava-o na composição. Os dois estavam acesos no grafo de cena, com `visible = true` e intensidade 1,0, e nenhum chegava à tela. `BLEND_MODE_PREMULT_ALPHA` no container |
| a pluma, já composta, era um **disco de 26 m de largura por 2,6 m de comprimento** | a escala de um `Node3D` aplica-se ANTES da rotação (`basis = R·S`). A malha cresce em +y, e o código esticava `scale.x` |
| e ficava 17 m à frente da tubeira | estava pendurada na origem do casco. `SpacecraftVisual.engine_mount` existia para isto, com um comentário a dizê-lo, e estava por usar |
| baixar o alfa de 0,50 para 0,18 não escurecia a pluma um pixel | com `BLEND_MODE_ADD` quem se soma é o **RGB**. Estes números só puderam ser calibrados depois de a composição estar certa: enquanto nada aparecia, podiam ser o que fosse — e eram |
| a captura do RCS saía com o acelerador a **100 %** e o motor aceso | `get_viewport().get_texture()` devolve o que já foi desenhado, ou seja, o quadro ANTERIOR. Um passo cuja condição fica verdadeira num quadro fotografava o estado de antes dele. Três quadros de espera depois da condição |
| o marcador "motor principal" do manual apontava para um radiador | eram coordenadas escritas à mão sobre uma captura antiga, e envelheciam em silêncio. Agora a posição é dada em metros no referencial do corpo e projetada pela câmera que tira a fotografia (`ShotDirector._write_anchors` → `.anchors.json` → `{{anchor:...}}`) |

Nenhum destes é detectável por aritmética, e todos passariam por uma suíte
inteiramente verde. É o mesmo argumento pelo qual as suítes `gpu` existem desde o
Milestone 6.2 §22.

## VISUAL DEBT

| | |
|---|---|
| a camada próxima não é ocluída pela distante | um planeta nunca passa à frente do casco. Para uma câmera que ou está dentro da nave ou a dezenas de metros dela isso não acontece; resolvê-lo pediria um terceiro viewport com profundidade partilhada |
| o albedo da Lua traz iluminação assada | é o que quase todo mosaico lunar traz, e agora ela soma-se ao relevo do normal map perto do terminador. Removê-la pede um albedo corrigido de topografia, que é outro produto do PDS |
| sem sombras entre corpos | não há eclipse. O mapa de sombras direcional do Godot não cobre 10¹¹ m; um eclipse é geometria que o core sabe fazer e o renderizador não |
| a pluma é um cone de aresta dura | geometria aditiva sem textura não tem como esmaecer ao longo do comprimento. Dois cones encaixados aproximam o degrau; uma pluma que desvanece pede um shader com gradiente, e o M7 não pede física de exaustão (regra 16) |
| o interior habitável não é modelado | o cockpit acaba num anteparo com escotilha. A regra 57 põe "full spacecraft interior" fora do M7 |
| as nuvens deslizam por desenho | não há circulação atmosférica; a alternativa, nuvens paradas sobre um planeta que gira, lê-se como um defeito de renderização |

---

## PHYSICS DEBT DISCOVERED

### A inércia não corresponde à geometria desenhada

O tensor do core é uma caixa sólida de **1000 kg e 8 × 3 × 3 m**. O casco
pressurizado desenhado tem 9 × 3 × 3 m — essa caixa — mas tanques, treliça,
seção de potência, radiadores e motor ficam **fora** dela: mais treze metros de
veículo e a maior parte das 19 toneladas de propelente.

Consequência: os momentos transversais estão subestimados e a nave gira mais
depressa do que uma nave com esta geometria giraria; o centro de massa está no
casco pressurizado e não entre ele e os tanques.

**Não foi corrigido**, e a regra 57 diz porquê: "perfect spacecraft mass
distribution" está fora do M7. Corrigir o tensor mudaria a dinâmica de atitude e
invalidaria as campanhas de apontamento do M6.2, que qualificam o planejador
contra números medidos. A forma de pagar é derivar o tensor da geometria e
**re-qualificar** a campanha — não trocar uma constante e assumir que nada mais
muda. Detalhe em [`docs/assets/spacecraft/dimensions.md`](../assets/spacecraft/dimensions.md).

### A rotação dos corpos entrou pelo lado do rendering

`SpiceEphemerisProvider::body_fixed_rotation()` é novo e devolve a rotação
completa corpo-fixo → J2000. Ele **não** está em `BodyOrientationProvider`, e a
razão está escrita no cabeçalho desse ficheiro: a interface que a dinâmica vê
expõe o polo e nada mais, porque um campo gravitacional axialmente simétrico não
depende de quanto o corpo rodou, e dar uma rotação completa à avaliação de forças
convida a integrar num referencial girante.

Nada em `core/gravity`, `core/propagation` ou `core/navigation` o chama. Essa é a
propriedade a preservar.

### Um defeito do M6.2 que só aparecia quando alguém desenhasse

`get_planned_trajectory()` existia desde o Milestone 6.2 e **nunca tinha sido
chamado**: a cena do M5/M6 não desenhava trajetórias. Ele devolvia o arco em
coordenadas absolutas, reconstruídas somando a posição do corpo de origem na
época de **cada amostra** — o que responde "onde a nave esteve no Sistema Solar"
e não "onde ela passa em relação à Terra".

Medido, com o planejador a correr de uma órbita de 400 km:

```
ship                        6 771 km da Terra
Moon                      361 025 km
arco, absoluto        153 734 .. 13 228 253 km     <- o que o M6.2 devolvia
arco, relativo à origem  6 771 ..    374 587 km    <- agora
```

Nada da trajetória mudou; mudou o referencial em que ela é expressa, que passou a
ser o mesmo de `get_body_orbit_track()`. Três curvas num mapa têm de estar num
referencial só, ou o mapa não se lê. Fixado por
`tests/test_m7.gd::_test_planned_trajectory_frame`, que planeja de verdade e
confere que o arco começa na órbita de estacionamento e acaba à distância da Lua.

### O que NÃO foi tocado

Lambert, o corretor diferencial, o plano B, a inserção, o modelo de gravidade, a
relatividade, a ótica, o integrador, as tolerâncias. Todas as 37 suítes de C++
continuam a passar, incluindo as 21 científicas.

---

## BACKLOG

Registrado, não feito (regra 84):

```
POINT TARGET como lei de guiamento no core
tensor de inércia derivado da geometria + re-qualificação da campanha
eclipses e sombra entre corpos
normal map da Lua ligado no shader
oclusão entre as duas camadas de escala
brilho da Terra sobre o casco (earthshine)
interior habitável para além do cockpit
música e mixagem de UI
modelo 3D artístico da nave (brief pronto)
vibração de casco durante a queima
salvar e carregar um voo
```

E tudo o que a regra 57 põe fora: Marte, Vênus, Júpiter, planejador
interplanetário geral, pouso, voo atmosférico, dano, combate, multiplayer,
simulação de tripulação, suporte de vida, VR, frame dragging, modelo lunar
avançado, nova pesquisa em relatividade.

---

## ASSETS STILL NEEDED

Todos com prompt pronto para copiar e colar. O simulador **roda sem eles** com
substitutos procedurais (regra 81).

```
1. Earth albedo
   Prompt:  docs/assets/planets/earth-albedo-codex-prompt.md
   Destino: godot/project/assets/textures/earth/earth_albedo.png

2. Earth clouds
   Prompt:  docs/assets/planets/earth-clouds-codex-prompt.md
   Destino: godot/project/assets/textures/earth/earth_clouds.png

3. Earth night lights
   Prompt:  docs/assets/planets/earth-night-lights-codex-prompt.md
   Destino: godot/project/assets/textures/earth/earth_night.png

4. Moon albedo
   Prompt:  docs/assets/planets/moon-albedo-codex-prompt.md
   Destino: godot/project/assets/textures/moon/moon_albedo.png

5. Spacecraft hull panels
   Prompt:  docs/assets/spacecraft/hull-panels-codex-prompt.md
   Destino: godot/project/assets/textures/spacecraft/hull_panels.png

6. Cockpit panel surface
   Prompt:  docs/assets/cockpit/panel-surface-codex-prompt.md
   Destino: godot/project/assets/textures/cockpit/panel_surface.png
```

Todos os seis acima estão entregues e integrados; a lista fica porque é a forma
de os regerar. Opcionais que continuam por fazer: `earth_normal`, `radiator`,
`thermal_blanket`, e um modelo 3D artístico da nave
([brief](../assets/spacecraft/3d-model-brief.md)).

⚠️ **`moon_normal` não está nessa lista e não deve voltar a estar.** Ele é
calculado, não pedido: `scripts/fetch_lunar_dem.sh` e
`scripts/make_moon_normal.py`. A mesma regra vale para qualquer mapa que
codifique uma GRANDEZA em vez de uma cor — altura, normal, rugosidade,
temperatura: um gerador de imagens pinta a aparência da grandeza, não a
grandeza.

Os sete arquivos de áudio são substitutos gerados por
`scripts/generate_audio_placeholders.py` — ruído filtrado e envelopes, não
gravações. Substituíveis um a um sem tocar em código.

Estado completo: [`docs/assets/manifest.md`](../assets/manifest.md).

---

## COMO VERIFICAR

```bash
# as texturas, sem engine nem kernels                21 verificações
cmake --build build --target asset-validation

# a física, como sempre                              37 suítes
cmake --build build --target ctest-headless

# a camada de apresentação, sem tela                 140 verificações
cmake --build build --target godot-tests

# a verificação do Milestone 5, sem tela
./scripts/run_godot_headless.sh

# a missão inteira, sem tela
SPACEFLIGHT_HEADLESS_MISSION=1 ./scripts/run_godot_headless.sh 4000

# a demonstração inteira, fotografada                precisa de tela
./scripts/m7_screenshots.sh

# o checklist manual                                 parte da DoD
docs/validation/m7-playtest.md
```
