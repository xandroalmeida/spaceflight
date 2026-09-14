class_name PauseMenu
extends PanelContainer
## Menu de pausa e ajustes mínimos (regras 69, 70).
##
## Quatro entradas e quatro ajustes. A regra 70 pede sensibilidade do mouse,
## volume geral, volume de efeitos e escala da interface, e é exatamente isso --
## um menu de opções que cresce sem que ninguém precise dele é uma forma de
## adiar decisões de design.
##
## Pausar para o TEMPO DE SIMULAÇÃO e nada mais. A câmera continua a responder,
## a interface continua viva, e nada no Godot é pausado (`get_tree().paused`
## fica intacto) -- porque o que tem de parar é o relógio da nave, não o
## programa.

signal resume_requested()
signal quit_requested()
signal help_requested()
signal setting_changed(key: String, value: float)

var _font: Font
var _sliders: Dictionary = {}


func _ready() -> void:
	_font = Palette.mono_font()
	custom_minimum_size = Vector2(420, 0)

	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.043, 0.051, 0.062, 0.97)
	style.border_color = Palette.PANEL_EDGE
	style.set_border_width_all(1)
	style.set_corner_radius_all(4)
	style.set_content_margin_all(22)
	add_theme_stylebox_override("panel", style)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	add_child(column)

	column.add_child(_label("PAUSED", 24, Palette.PRIMARY))
	column.add_child(_label("simulation time is stopped; the ship is untouched", 12,
		Palette.SECONDARY))
	column.add_child(_rule())

	column.add_child(_button("RESUME", func() -> void: resume_requested.emit()))
	column.add_child(_button("CONTROLS", func() -> void: help_requested.emit()))
	column.add_child(_rule())
	column.add_child(_label("SETTINGS", 14, Palette.SECONDARY))

	_slider(column, "mouse_sensitivity", "MOUSE SENSITIVITY", 0.2, 3.0, 1.0)
	_slider(column, "master_volume", "MASTER VOLUME", 0.0, 1.0, 0.8)
	_slider(column, "effects_volume", "EFFECTS VOLUME", 0.0, 1.0, 0.8)
	_slider(column, "ui_scale", "UI SCALE", 0.7, 1.6, 1.0)

	column.add_child(_rule())
	column.add_child(_button("QUIT", func() -> void: quit_requested.emit()))


func set_value(key: String, value: float) -> void:
	if _sliders.has(key):
		(_sliders[key] as HSlider).set_value_no_signal(value)


func _slider(parent: Control, key: String, label: String, low: float, high: float,
		initial: float) -> void:
	var row := VBoxContainer.new()
	row.add_theme_constant_override("separation", 2)
	parent.add_child(row)

	var caption := _label("%s   %.2f" % [label, initial], 12, Palette.SECONDARY)
	row.add_child(caption)

	var slider := HSlider.new()
	slider.min_value = low
	slider.max_value = high
	slider.step = 0.01
	slider.value = initial
	slider.custom_minimum_size = Vector2(0, 20)
	slider.value_changed.connect(func(value: float) -> void:
		caption.text = "%s   %.2f" % [label, value]
		setting_changed.emit(key, value))
	row.add_child(slider)
	_sliders[key] = slider


func _label(text: String, px: int, colour: Color) -> Label:
	var node := Label.new()
	node.text = text
	node.add_theme_font_override("font", _font)
	node.add_theme_font_size_override("font_size", px)
	node.add_theme_color_override("font_color", colour)
	return node


func _rule() -> Control:
	var line := ColorRect.new()
	line.color = Palette.PANEL_EDGE
	line.custom_minimum_size = Vector2(0, 1)
	return line


func _button(text: String, callback: Callable) -> Button:
	var node := Button.new()
	node.text = text
	node.add_theme_font_override("font", _font)
	node.add_theme_font_size_override("font_size", 15)
	node.custom_minimum_size = Vector2(0, 34)
	node.pressed.connect(callback)
	return node
