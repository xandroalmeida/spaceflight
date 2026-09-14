class_name MessageLog
extends VBoxContainer
## As mensagens da missão, discretas (regra 67).
##
## Cada linha aparece, fica, e apaga. Sem animação de entrada, sem caixa, sem
## som de arcade: o que a nave diz ao piloto tem de ler-se como telemetria e não
## como uma conquista desbloqueada.
##
## A última linha de cada evento importante fica registrada no histórico, que o
## HUD técnico mostra por inteiro. Uma mensagem que passou e ninguém viu é uma
## mensagem que não existiu.

enum Level { INFO, MISSION, WARNING, CRITICAL }

const HOLD_SECONDS := 6.0
const FADE_SECONDS := 1.6
const MAX_VISIBLE := 6
const HISTORY_LIMIT := 200

var history: Array[Dictionary] = []

var _entries: Array[Dictionary] = []
var _font: Font


func _ready() -> void:
	_font = Palette.mono_font()
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_theme_constant_override("separation", 2)


func post(text: String, level: int = Level.INFO) -> void:
	history.append({"text": text, "level": level, "at": Time.get_ticks_msec()})
	if history.size() > HISTORY_LIMIT:
		history.remove_at(0)

	var label := Label.new()
	label.text = text
	label.add_theme_font_override("font", _font)
	label.add_theme_color_override("font_color", _colour(level))
	label.add_theme_font_size_override("font_size", _size())
	# Um contorno escuro: a mensagem aparece por cima do espaço e por cima da
	# Terra, e sem ele metade delas fica ilegível metade do tempo.
	label.add_theme_color_override("font_outline_color", Color(0.0, 0.0, 0.0, 0.85))
	label.add_theme_constant_override("outline_size", 4)
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(label)
	_entries.append({"label": label, "age": 0.0})

	while _entries.size() > MAX_VISIBLE:
		var oldest: Dictionary = _entries.pop_front()
		(oldest["label"] as Label).queue_free()


func _process(delta: float) -> void:
	var survivors: Array[Dictionary] = []
	for entry in _entries:
		entry["age"] = float(entry["age"]) + delta
		var age: float = entry["age"]
		var label: Label = entry["label"]
		if age > HOLD_SECONDS + FADE_SECONDS:
			label.queue_free()
			continue
		if age > HOLD_SECONDS:
			label.modulate.a = 1.0 - (age - HOLD_SECONDS) / FADE_SECONDS
		survivors.append(entry)
	_entries = survivors


func rescale() -> void:
	for entry in _entries:
		(entry["label"] as Label).add_theme_font_size_override("font_size", _size())


func _size() -> int:
	var height := get_viewport_rect().size.y
	return clampi(int(height / 52.0), 12, 24)


static func _colour(level: int) -> Color:
	match level:
		Level.MISSION: return Palette.NAV
		Level.WARNING: return Palette.WARNING
		Level.CRITICAL: return Palette.CRITICAL
		_: return Palette.PRIMARY


static func level_name(level: int) -> String:
	match level:
		Level.MISSION: return "MISSION"
		Level.WARNING: return "WARNING"
		Level.CRITICAL: return "CRITICAL"
		_: return "INFO"
