# UI

**Nada aqui é gerado externamente**, e isso é a regra 46 aplicada e não uma
omissão: retículas, molduras, linhas de órbita, marcadores de vetor, barras e
grades ficam melhor num `_draw()` do que em qualquer PNG, e escalam sozinhos.

| elemento | onde | por que é procedural |
|---|---|---|
| retículas de prógrado, retrógrado, normal, radial, alvo | `scripts/cockpit/instrument.gd`, `draw_marker` | um círculo com quatro tiques é dez linhas de código e zero bytes de textura, e fica nítido em 4K |
| molduras dos mostradores | `draw_frame` | um retângulo |
| linhas de órbita e de trajetória | `nav_display.gd`, `orbit_map.gd` | os pontos vêm do core; desenhar é ligar pontos |
| anéis de escala do mapa | `orbit_map.gd`, `_nice_step` | o raio muda com o zoom; uma textura teria de ser redesenhada |
| barras de acelerador e propelente | `draw_bar` | dois retângulos |
| lâmpadas de RCS | `system_display.gd` | doze retângulos cuja cor é o dado |
| listras de atenção | não usadas ainda | seriam um shader |

## Tipografia

`SystemFont` monoespaçada, procurando `SF Mono`, `Menlo`, `Monaco`, `Consolas`,
`DejaVu Sans Mono`, `Liberation Mono`, `monospace` — a primeira que a plataforma
tiver. Monoespaçada de propósito: a leitura do cockpit é uma tabela de colunas
alinhadas, `0`/`O` e `1`/`I` têm de ser distinguíveis num relance (regra 73), e o
HUD técnico depende disso para empacotar em duas colunas sem layout nenhum.

⚠️ Uma `SystemFont` recém-construída ainda não resolveu a face, e o que ela
desenha até resolver são os retângulos de `.notdef`. Por isso cada instrumento
guarda a sua em `_ready` e `Palette.mono_font()` nunca é chamado dentro de um
`_draw`. Isto foi encontrado a olhar para uma captura em que a faixa de sistemas
era uma fileira de caixas brancas.

## Paleta

Toda em `scripts/palette.gd`, restrita de propósito (regra 72): fundo neutro
escuro, dado primário quase branco, navegação em ciano dessaturado, âmbar para
atenção, vermelho para crítico, verde para o vetor de velocidade, violeta para o
alvo. Cinco cores com significado e nada decorativo — um instrumento cuja cor não
quer dizer nada gasta o único canal que um piloto lê sem ler.

## Rótulos em 3D

Os rótulos dos botões do painel são `Label3D`, desenhados pelo Godot, e **não**
texto embutido numa textura (regra 44). Ficam nítidos em qualquer resolução e
podem ser traduzidos; um rótulo errado dentro de um bitmap é pior do que nenhum
rótulo.
