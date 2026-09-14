class_name Palette
extends RefCounted
## A paleta inteira do cockpit, em um lugar (regra 72).
##
## Restrita de propósito: fundo neutro escuro, dado primário quase branco,
## navegação em ciano dessaturado, âmbar para atenção, vermelho para crítico.
## Cinco cores com significado e nada decorativo -- um instrumento cuja cor não
## quer dizer nada gasta o único canal que um piloto lê sem ler.
##
## Nada aqui é "bonito": é o que sobra depois de tirar tudo que não carrega
## informação.

const BACKGROUND := Color(0.043, 0.051, 0.062)      ## grafite, o fundo dos displays
const PANEL := Color(0.098, 0.106, 0.118)           ## a superfície do painel
const PANEL_EDGE := Color(0.16, 0.17, 0.19)

const PRIMARY := Color(0.898, 0.925, 0.949)         ## dado primário
const SECONDARY := Color(0.541, 0.580, 0.620)       ## rótulo, unidade, eixo
const DIM := Color(0.33, 0.36, 0.40)                ## grade, tique menor

const NAV := Color(0.361, 0.788, 0.769)             ## ciano dessaturado: navegação
const NAV_DIM := Color(0.20, 0.44, 0.44)
const PROGRADE := Color(0.639, 0.831, 0.475)        ## verde: vetor de velocidade
const TARGET := Color(0.792, 0.616, 0.902)          ## violeta: alvo
const PLAN := Color(0.949, 0.749, 0.404)            ## a trajetória planejada

const WARNING := Color(0.949, 0.678, 0.267)         ## âmbar
const CRITICAL := Color(0.902, 0.361, 0.333)        ## vermelho
const OK := Color(0.451, 0.784, 0.541)              ## verde de estado

const ENGINE := Color(0.984, 0.831, 0.596)          ## pluma do motor
const RCS := Color(0.761, 0.878, 0.976)             ## jato de RCS

## O vidro dos displays acesos empurra um pouco de luz no cockpit (regra 51).
const DISPLAY_GLOW := Color(0.145, 0.267, 0.290)


## Um ciano que escurece conforme a confiança cai. Usado onde um valor existe
## mas está velho ou fora de faixa -- em vez de escondê-lo, que faria o
## instrumento mentir por omissão.
static func faded(colour: Color, amount: float) -> Color:
	return colour.lerp(DIM, clampf(amount, 0.0, 1.0))


## A fonte do cockpit. Monoespaçada porque a leitura é uma tabela de colunas, e
## porque `0`/`O` e `1`/`I` têm de ser distinguíveis num relance (regra 73).
## SystemFont escolhe o primeiro nome que a plataforma realmente tem.
static func mono_font() -> Font:
	var font := SystemFont.new()
	font.font_names = PackedStringArray([
		"SF Mono", "Menlo", "Monaco", "Consolas", "DejaVu Sans Mono",
		"Liberation Mono", "monospace"])
	return font
