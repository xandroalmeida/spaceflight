# UI

**Nada aqui é gerado externamente**, e isso é a regra 46 aplicada e não uma
omissão: retículas, molduras, linhas de órbita, marcadores de vetor, barras e
grades ficam melhor desenhados como linhas do que em qualquer PNG, e escalam
sozinhos. Os instrumentos desenham num `Canvas` (`app/presentation/canvas.hpp`)
que, no jogo, é uma lista de desenho do ImGui (`app/gfx/imgui_canvas.cpp`) e,
nos testes, um gravador de chamadas.

| elemento | onde | por que é procedural |
|---|---|---|
| retículas de prógrado, retrógrado, normal, radial, alvo | `app/presentation/instruments/instrument.cpp`, `Instrument::draw_marker` | um círculo com quatro tiques é dez linhas de código e zero bytes de textura, e fica nítido em 4K |
| molduras dos mostradores | `Instrument::draw_frame` | um retângulo |
| linhas de órbita e de trajetória | `instruments/nav_display.cpp`, `instruments/orbit_map.cpp` | os pontos vêm do core; desenhar é ligar pontos |
| anéis de escala do mapa | `instruments/orbit_map.cpp`, `OrbitMap::nice_step` | o raio muda com o zoom; uma textura teria de ser redesenhada |
| barras de acelerador e propelente | `Instrument::draw_bar` | dois retângulos |
| lâmpadas de RCS | `SystemDisplay`, em `instruments/displays.cpp` | doze retângulos cuja cor é o dado |
| listras de atenção | não usadas ainda | seriam um shader |

## Tipografia

**DejaVu Sans Mono**, em `assets/fonts/DejaVuSansMono.ttf` (licença ao lado, em
`DejaVu-LICENSE.txt`), carregada pelo ImGui em `app/ui/interface.cpp`. Até o
Milestone 8 era uma `SystemFont` do Godot que procurava `SF Mono`, `Menlo`,
`Consolas`, `DejaVu Sans Mono`… — a primeira que a plataforma tivesse; agora é a
mesma fonte em todas as máquinas, e ela leva o grego, as setas e os triângulos
que os painéis usam. Monoespaçada de propósito: a leitura do cockpit é uma
tabela de colunas alinhadas, `0`/`O` e `1`/`I` têm de ser distinguíveis num
relance (regra 73), e o HUD técnico depende disso para empacotar em duas colunas
sem layout nenhum.

⚠️ O ImGui mede uma fonte pela altura de ascendente a descendente, e a cena (e o
Godot antes dela) media pelo em. Para a DejaVu Sans Mono a diferença é 1,164×, e
`gfx::font_px` (`app/gfx/imgui_canvas.hpp`) converte — sem isso cada rótulo sai
um sexto menor do que nas capturas do manual.

## Paleta

Toda em `app/presentation/palette.hpp`, restrita de propósito (regra 72): fundo neutro
escuro, dado primário quase branco, navegação em ciano dessaturado, âmbar para
atenção, vermelho para crítico, verde para o vetor de velocidade, violeta para o
alvo. Cinco cores com significado e nada decorativo — um instrumento cuja cor não
quer dizer nada gasta o único canal que um piloto lê sem ler.

## Rótulos em 3D

Os rótulos dos botões do painel são **texto desenhado** pelo ImGui numa textura
dinâmica por rótulo (`caption:<n>`, em `app/presentation/scene/cockpit.cpp` e
`app/gfx/renderer.cpp`), aplicada num quad sobre o botão — e **não** texto
embutido num arquivo de imagem (regra 44). No Godot eram `Label3D`. Ficam
nítidos e podem ser traduzidos; um rótulo errado dentro de um bitmap é pior do que nenhum
rótulo.
