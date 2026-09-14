class_name Instrument
extends Control
## Base dos instrumentos do cockpit (regra 17).
##
## Um instrumento recebe um `Dictionary` por quadro e desenha. Ele não pergunta
## nada à simulação e não guarda estado entre quadros além do último dado
## recebido -- o que significa que o mesmo instrumento serve ao painel 3D dentro
## do cockpit e ao HUD 2D, sem saber em qual dos dois está.
##
## O desenho é `_draw()` e não nós de cena: um mostrador é uma figura que muda
## inteira a cada quadro, e montar e desmontar `Line2D` para isso custa mais e lê
## pior. As primitivas comuns -- moldura, rótulo, valor, tique -- estão aqui para
## que os quatro mostradores não divirjam em espessura de linha e em altura de
## texto, que é como uma interface deixa de parecer um sistema.

var data: Dictionary = {}
var title := ""

var _font: Font
var _scale := 1.0


func _ready() -> void:
	_font = Palette.mono_font()
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	resized.connect(queue_redraw)


## A fonte, UMA vez por instrumento.
##
## `Palette.mono_font()` constrói um `SystemFont` novo a cada chamada, e um
## `SystemFont` recém-construído ainda não resolveu a face -- o que ele desenha
## até resolver são os retângulos de `.notdef`. Chamá-lo dentro de `_draw`, uma
## vez por string, por quadro, enchia a faixa de sistemas de caixas brancas e
## custava uma alocação por número no mostrador.
func font() -> Font:
	return _font


func refresh(new_data: Dictionary) -> void:
	data = new_data
	queue_redraw()


## Tamanho de fonte proporcional à altura do instrumento, para que o mesmo
## desenho funcione num painel de 380 px e num HUD de 1000 px (regra 59).
func unit() -> float:
	return maxf(size.y / 100.0, 0.5)


func font_size(units: float) -> int:
	return maxi(int(unit() * units), 8)


func draw_frame(colour: Color = Palette.PANEL_EDGE) -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Palette.BACKGROUND, true)
	draw_rect(Rect2(Vector2.ONE, size - Vector2.ONE * 2.0), colour, false, maxf(unit() * 0.4, 1.0))
	if title != "":
		draw_text_at(Vector2(unit() * 2.5, unit() * 6.0), title, 5.0, Palette.SECONDARY)


func draw_text_at(at: Vector2, text: String, units: float, colour: Color,
		align: int = HORIZONTAL_ALIGNMENT_LEFT) -> void:
	var px := font_size(units)
	var width := _font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, px).x
	var x := at.x
	if align == HORIZONTAL_ALIGNMENT_RIGHT:
		x -= width
	elif align == HORIZONTAL_ALIGNMENT_CENTER:
		x -= width * 0.5
	draw_string(_font, Vector2(x, at.y), text, HORIZONTAL_ALIGNMENT_LEFT, -1, px, colour)


## Um par rótulo/valor, que é a unidade de leitura de que o cockpit é feito. O
## rótulo é pequeno e dessaturado, o valor é grande e claro: a hierarquia da
## regra 38 posta onde ela é aplicada, e não numa folha de estilo à parte.
func draw_field(at: Vector2, label: String, value: String, value_units: float = 8.0,
		colour: Color = Palette.PRIMARY) -> void:
	draw_text_at(at, label, 4.2, Palette.SECONDARY)
	draw_text_at(at + Vector2(0.0, unit() * (value_units + 1.0)), value, value_units, colour)


func draw_bar(rect: Rect2, fraction: float, colour: Color,
		background: Color = Palette.DIM) -> void:
	draw_rect(rect, background.darkened(0.6), true)
	var filled := Rect2(rect.position, Vector2(rect.size.x * clampf(fraction, 0.0, 1.0), rect.size.y))
	draw_rect(filled, colour, true)
	draw_rect(rect, background, false, maxf(unit() * 0.25, 1.0))


func draw_tick(from: Vector2, to: Vector2, colour: Color, width_units: float = 0.3) -> void:
	draw_line(from, to, colour, maxf(unit() * width_units, 1.0))


## A retícula de um marcador de vetor. Desenhada e não texturizada, porque é
## exatamente o caso da regra 46: um círculo com quatro tiques fica melhor num
## `_draw` do que em qualquer PNG, e escala sozinho.
func draw_marker(at: Vector2, radius: float, colour: Color, kind: String) -> void:
	var w := maxf(unit() * 0.35, 1.0)
	match kind:
		"prograde":
			draw_arc(at, radius, 0.0, TAU, 24, colour, w)
			draw_circle(at, radius * 0.22, colour)
			draw_line(at - Vector2(radius * 1.8, 0.0), at - Vector2(radius, 0.0), colour, w)
			draw_line(at + Vector2(radius, 0.0), at + Vector2(radius * 1.8, 0.0), colour, w)
			draw_line(at - Vector2(0.0, radius * 1.8), at - Vector2(0.0, radius), colour, w)
		"retrograde":
			draw_arc(at, radius, 0.0, TAU, 24, colour, w)
			var d := radius * 0.62
			draw_line(at - Vector2(d, d), at + Vector2(d, d), colour, w)
			draw_line(at - Vector2(d, -d), at + Vector2(d, -d), colour, w)
			draw_line(at - Vector2(radius * 1.8, 0.0), at - Vector2(radius, 0.0), colour, w)
			draw_line(at + Vector2(radius, 0.0), at + Vector2(radius * 1.8, 0.0), colour, w)
		"normal":
			draw_polyline([at + Vector2(-radius, radius * 0.7), at + Vector2(0.0, -radius),
				at + Vector2(radius, radius * 0.7)], colour, w)
			draw_line(at + Vector2(-radius, radius * 0.7), at + Vector2(radius, radius * 0.7),
				colour, w)
		"anti_normal":
			draw_polyline([at + Vector2(-radius, -radius * 0.7), at + Vector2(0.0, radius),
				at + Vector2(radius, -radius * 0.7)], colour, w)
			draw_line(at + Vector2(-radius, -radius * 0.7), at + Vector2(radius, -radius * 0.7),
				colour, w)
		"radial_out":
			draw_arc(at, radius * 0.75, 0.0, TAU, 20, colour, w)
			draw_line(at, at + Vector2(0.0, -radius * 1.7), colour, w)
		"radial_in":
			draw_arc(at, radius * 0.75, 0.0, TAU, 20, colour, w)
			draw_line(at, at + Vector2(0.0, radius * 1.7), colour, w)
		"target":
			draw_arc(at, radius, 0.0, TAU, 24, colour, w)
			for angle in [0.0, PI * 0.5, PI, PI * 1.5]:
				var dir := Vector2(cos(angle), sin(angle))
				draw_line(at + dir * radius, at + dir * radius * 1.7, colour, w)
		"anti_target":
			for angle in [PI * 0.25, PI * 0.75, PI * 1.25, PI * 1.75]:
				var dir := Vector2(cos(angle), sin(angle))
				draw_line(at + dir * radius * 0.35, at + dir * radius * 1.5, colour, w)
		"nose":
			draw_line(at - Vector2(radius * 2.2, 0.0), at - Vector2(radius * 0.5, 0.0), colour, w)
			draw_line(at + Vector2(radius * 0.5, 0.0), at + Vector2(radius * 2.2, 0.0), colour, w)
			draw_line(at, at + Vector2(0.0, radius * 1.2), colour, w)
		_:
			draw_arc(at, radius, 0.0, TAU, 16, colour, w)
