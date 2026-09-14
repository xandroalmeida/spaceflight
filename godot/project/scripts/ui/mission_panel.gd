class_name MissionPanel
extends PanelContainer
## O computador de bordo (regras 25, 26, 27).
##
##     NAV -> escolher alvo -> PLAN TRANSFER -> resumo -> EXECUTE | CANCEL
##
## Cada número do resumo vem de `get_plan()`, que vem das `MissionMetrics` do
## core. Este painel renomeia campos e formata unidades; ele não calcula Δv, não
## estima tempo de voo e não tem opinião sobre a órbita prevista (regra 77).
##
## ## Por que PLANEJAR trava o quadro
##
## Trava, por cerca de um segundo, e isso está assim de propósito: planejar é
## procurar oportunidades de partida e depois inverter o modelo completo duas
## vezes, dezenas de propagações. É uma operação de missão, não de quadro.
## Threadá-lo compraria um segundo mais suave e custaria poder dizer qual era o
## estado da simulação quando o plano foi feito.
##
## O que o painel faz é AVISAR antes: o botão muda para "PLANNING..." e o quadro
## seguinte é o que bloqueia. Sem isso o jogo parece ter travado, que é a única
## coisa pior do que travar.

signal plan_requested(target: String, periapsis_km: float, apoapsis_km: float)
signal execute_requested()
signal cancel_requested()
signal target_changed(target: String)
signal closed()

const DEFAULT_PERIAPSIS_KM := 100.0
const DEFAULT_APOAPSIS_KM := 100.0

var targets: PackedStringArray = PackedStringArray()
var target_index := 0

var _header: Label
var _target_label: Label
var _summary: RichTextLabel
var _plan_button: Button
var _execute_button: Button
var _cancel_button: Button
var _status: Label
var _pending_plan := false
var _font: Font


func _ready() -> void:
	_font = Palette.mono_font()
	custom_minimum_size = Vector2(760, 0)

	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.043, 0.051, 0.062, 0.96)
	style.border_color = Palette.PANEL_EDGE
	style.set_border_width_all(1)
	style.set_corner_radius_all(4)
	style.set_content_margin_all(18)
	add_theme_stylebox_override("panel", style)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	add_child(column)

	_header = _label("MISSION COMPUTER", 20, Palette.PRIMARY)
	column.add_child(_header)
	column.add_child(_rule())

	# --- alvo ---
	var target_row := HBoxContainer.new()
	target_row.add_theme_constant_override("separation", 10)
	column.add_child(target_row)
	target_row.add_child(_label("TARGET", 13, Palette.SECONDARY))
	var previous := _button("◀", func() -> void: _step_target(-1))
	previous.custom_minimum_size = Vector2(44, 0)
	target_row.add_child(previous)
	_target_label = _label("—", 18, Palette.TARGET)
	_target_label.custom_minimum_size = Vector2(190, 0)
	_target_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	target_row.add_child(_target_label)
	var next := _button("▶", func() -> void: _step_target(1))
	next.custom_minimum_size = Vector2(44, 0)
	target_row.add_child(next)

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	target_row.add_child(spacer)

	_plan_button = _button("PLAN TRANSFER", func() -> void: _request_plan())
	target_row.add_child(_plan_button)

	# --- resumo ---
	_summary = RichTextLabel.new()
	_summary.bbcode_enabled = true
	_summary.fit_content = true
	_summary.scroll_active = false
	_summary.custom_minimum_size = Vector2(0, 280)
	_summary.add_theme_font_override("normal_font", _font)
	_summary.add_theme_font_size_override("normal_font_size", 14)
	column.add_child(_summary)

	_status = _label("", 13, Palette.WARNING)
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_status)

	column.add_child(_rule())
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 10)
	column.add_child(actions)
	_execute_button = _button("EXECUTE", func() -> void: execute_requested.emit())
	_execute_button.custom_minimum_size = Vector2(150, 40)
	actions.add_child(_execute_button)
	_cancel_button = _button("CANCEL", func() -> void: cancel_requested.emit())
	_cancel_button.custom_minimum_size = Vector2(150, 40)
	actions.add_child(_cancel_button)
	var gap := Control.new()
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	actions.add_child(gap)
	actions.add_child(_button("CLOSE", func() -> void: closed.emit()))


func _process(_delta: float) -> void:
	if not _pending_plan:
		return
	# O pedido foi anunciado no quadro anterior e a tela já mostra "PLANNING".
	# Só agora é que o quadro bloqueia. Um quadro de diferença é o que separa
	# "o jogo avisou" de "o jogo congelou".
	_pending_plan = false
	plan_requested.emit(current_target(), DEFAULT_PERIAPSIS_KM, DEFAULT_APOAPSIS_KM)


func set_targets(names: PackedStringArray, current: String) -> void:
	targets = names
	target_index = maxi(targets.find(current), 0)
	_refresh_target()


func current_target() -> String:
	if targets.is_empty():
		return ""
	return targets[clampi(target_index, 0, targets.size() - 1)]


func step_target(direction: int) -> void:
	_step_target(direction)


func _step_target(direction: int) -> void:
	if targets.is_empty():
		return
	target_index = wrapi(target_index + direction, 0, targets.size())
	_refresh_target()
	target_changed.emit(current_target())


func _refresh_target() -> void:
	_target_label.text = current_target().to_upper() if not targets.is_empty() else "—"


func _request_plan() -> void:
	_plan_button.text = "PLANNING…"
	_plan_button.disabled = true
	_status.text = "searching departure opportunities -- the frame will stop for about a second"
	_status.add_theme_color_override("font_color", Palette.SECONDARY)
	_pending_plan = true


## Chamado pelo `Flight` depois de planejar (ou de falhar).
func show_plan(plan: Dictionary, error: String) -> void:
	_plan_button.text = "PLAN TRANSFER"
	_plan_button.disabled = false

	if plan.is_empty() or not plan.get("valid", false):
		_summary.text = ""
		_status.add_theme_color_override("font_color", Palette.CRITICAL)
		_status.text = "NO PLAN -- %s" % error
		_execute_button.disabled = true
		_cancel_button.disabled = true
		return

	_execute_button.disabled = plan.get("armed", false)
	_cancel_button.disabled = false
	_status.add_theme_color_override("font_color", Palette.SECONDARY)
	_status.text = ("EXECUTING -- the autopilot has the attitude"
		if plan.get("armed", false)
		else "review, then EXECUTE to arm the burns")
	_summary.text = _format(plan)


func show_alternatives(alternatives: Array) -> void:
	## As geometrias que a busca de facto voou (M6.2 seção 13), cada uma com a
	## órbita em que teria chegado. Mostradas em vez de escondidas, porque o
	## planejador NÃO mira uma inclinação e esta lista é como ele diz isso.
	if alternatives.is_empty():
		return
	var lines := ["", "[color=#8a939e]CANDIDATES FLOWN BY THE SEARCH[/color]"]
	for entry in alternatives:
		var alternative: Dictionary = entry
		# Uma linha por candidato, e curta o bastante para não quebrar: o rótulo
		# completo do planejador tem 28 caracteres e, com a órbita prevista atrás
		# dele, cada candidato ocupava duas linhas e a lista transbordava para
		# fora do painel. O que interessa do rótulo é o tempo de voo, que é o que
		# distingue um candidato do seguinte.
		var label := String(alternative["label"]).split("/")
		lines.append("  %-7s %s %6.0f m/s   %.0f × %.0f km   i %.1f°"
			% [label[1] if label.size() > 1 else label[0],
			   "ok " if alternative["feasible"] else "no ",
			   alternative["total_delta_v"],
			   float(alternative["predicted_periapsis_m"]) / 1000.0,
			   float(alternative["predicted_apoapsis_m"]) / 1000.0,
			   alternative["predicted_inclination_deg"]])
	_summary.text += "\n".join(lines)


func _format(p: Dictionary) -> String:
	var departure: float = p.get("seconds_to_ignition", 0.0)
	var arrival: float = p.get("seconds_to_insertion", 0.0)
	var rows := [
		"[color=#e5ecf2]%s → %s[/color]" % [String(p.get("origin", "?")).to_upper(),
			String(p.get("target", "?")).to_upper()],
		"",
		_row("DEPARTURE", "in %s" % Fmt.duration(departure) if departure > 0.0 else "passed"),
		_row("ARRIVAL", "in %s" % Fmt.duration(arrival) if arrival > 0.0 else "passed"),
		_row("FLIGHT TIME", "%.2f days   %s branch"
			% [p.get("time_of_flight_days", 0.0), p.get("branch", "?")]),
		_row("TRANSFER ANGLE", "%.1f°" % p.get("transfer_angle_deg", 0.0)),
		"",
		_row("INJECTION", "%.1f m/s over %s"
			% [p.get("injection_delta_v", 0.0),
			   Fmt.duration(p.get("injection_duration_s", 0.0))]),
		_row("MIDCOURSE", "%.1f m/s  (folded into the injection)"
			% p.get("midcourse_delta_v", 0.0)),
		_row("CAPTURE", "%.1f m/s over %s"
			% [p.get("insertion_delta_v", 0.0),
			   Fmt.duration(p.get("insertion_duration_s", 0.0))]),
		_row("TOTAL ΔV", "[color=#f2bf67]%.1f m/s[/color] of %s available"
			% [p.get("total_delta_v", 0.0), Fmt.speed(p.get("delta_v_available", 0.0))]),
		"",
		_row("PROPELLANT", "%.1f kg required, %.1f kg left after"
			% [p.get("propellant_required_kg", 0.0), p.get("propellant_remaining_kg", 0.0)]),
		"",
		"[color=#8a939e]PREDICTED ORBIT AT ARRIVAL[/color]",
		_row("  PE", Fmt.distance(p.get("predicted_periapsis_m", 0.0))),
		_row("  AP", Fmt.distance(p.get("predicted_apoapsis_m", 0.0))),
		_row("  ECC", "%.5f" % p.get("predicted_eccentricity", 0.0)),
		_row("  INC", "%.2f°   RAAN %.1f°"
			% [p.get("predicted_inclination_deg", 0.0), p.get("predicted_raan_deg", 0.0)]),
	]
	return "\n".join(rows)


func _row(label: String, value: String) -> String:
	return "[color=#8a939e]%s[/color]  %s" % [label.rpad(16), value]


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
	node.add_theme_font_size_override("font_size", 14)
	node.pressed.connect(callback)
	return node
