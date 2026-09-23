class_name HelpPanel
extends PanelContainer
## A ajuda dos controles, dentro do jogo (regra 75).
##
## Gerada a partir do `InputMap`, e não escrita à mão. Uma lista de teclas
## copiada num painel é uma lista de teclas que envelhece em silêncio: alguém
## reatribui uma tecla, o painel continua a ensinar a antiga, e o jogador conclui
## que o jogo está partido.
##
## `docs/gameplay/controls.md` sai da MESMA tabela, pelo mesmo motivo.

signal closed()

## O que fica de fora da área rolável: título, linha do mouse, botão, margens.
## A lista de teclas cresce com cada ação nova e numa tela de 768 px já não
## cabia -- o fim dela, com o CLOSE, ficava abaixo da borda e fora de alcance.
const CHROME_PX := 170.0

var _font: Font
var _scroll: ScrollContainer
var _grid: HBoxContainer


func _ready() -> void:
	_font = Palette.mono_font()
	custom_minimum_size = Vector2(760, 0)

	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.043, 0.051, 0.062, 0.96)
	style.border_color = Palette.PANEL_EDGE
	style.set_border_width_all(1)
	style.set_corner_radius_all(4)
	style.set_content_margin_all(20)
	add_theme_stylebox_override("panel", style)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 8)
	add_child(column)

	column.add_child(_label("CONTROLS", 22, Palette.PRIMARY))
	column.add_child(_label(
		"mouse: drag to look / orbit   wheel: zoom   click a cockpit button to use it",
		12, Palette.SECONDARY))

	var grid := HBoxContainer.new()
	grid.add_theme_constant_override("separation", 26)
	_grid = grid
	_scroll = ScrollContainer.new()
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_scroll.add_child(grid)
	column.add_child(_scroll)

	var groups := InputActions.by_group()
	var left := VBoxContainer.new()
	var right := VBoxContainer.new()
	left.add_theme_constant_override("separation", 10)
	right.add_theme_constant_override("separation", 10)
	grid.add_child(left)
	grid.add_child(right)

	var index := 0
	for group in groups:
		var into := left if index < groups.size() / 2 + 1 else right
		into.add_child(_label(group, 14, Palette.NAV))
		for entry in groups[group]:
			into.add_child(_label("  %s  %s" % [String(entry["key"]).rpad(13),
				entry["description"]], 12, Palette.SECONDARY))
		index += 1

	var close := Button.new()
	close.text = "CLOSE"
	close.add_theme_font_override("font", _font)
	close.pressed.connect(func() -> void: closed.emit())
	column.add_child(close)

	get_viewport().size_changed.connect(_fit_to_screen)
	visibility_changed.connect(_fit_to_screen)
	_fit_to_screen.call_deferred()


## A lista inteira quando cabe; quando não cabe, a altura da janela e uma barra
## de rolagem (roda do mouse ou arrastar).
func _fit_to_screen() -> void:
	if _scroll == null or not is_inside_tree():
		return
	var available := get_viewport_rect().size.y - CHROME_PX
	var wanted := _grid.get_combined_minimum_size().y
	# + a largura da barra de rolagem, que se desenha POR CIMA do conteúdo e
	# encostava na coluna da direita.
	var bar := _scroll.get_v_scroll_bar().get_combined_minimum_size().x + 12.0
	_scroll.custom_minimum_size = Vector2(_grid.get_combined_minimum_size().x + bar,
		clampf(wanted, 120.0, maxf(available, 120.0)))
	reset_size()


func _label(text: String, px: int, colour: Color) -> Label:
	var node := Label.new()
	node.text = text
	node.add_theme_font_override("font", _font)
	node.add_theme_font_size_override("font_size", px)
	node.add_theme_color_override("font_color", colour)
	return node
