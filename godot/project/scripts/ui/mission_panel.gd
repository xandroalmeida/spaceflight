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
signal search_cancelled()
signal target_changed(target: String)
signal closed()

const DEFAULT_PERIAPSIS_KM := 100.0
const DEFAULT_APOAPSIS_KM := 100.0

## As altitudes que o piloto pode pedir, em km (regras 9 e 57).
##
## Uma lista e não um campo de texto: o número tem de ser plausível para o corpo
## escolhido, e uma caixa livre convida a pedir uma órbita de 5 m. 500 km é o
## default de Marte e 100 km o da Lua, e qual deles aparece primeiro depende do
## alvo -- ver `_default_altitude_for`.
const ALTITUDES_KM: Array[float] = [50.0, 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0]

var targets: PackedStringArray = PackedStringArray()
var target_index := 0
var altitude_index := 1

var _header: Label
var _target_label: Label
var _orbit_label: Label
var _summary: RichTextLabel
var _plan_button: Button
var _execute_button: Button
var _cancel_button: Button
var _status: Label
var _pending_plan := false
var _searching := false
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

	# --- órbita desejada (regras 9, 45, 57) ---
	var orbit_row := HBoxContainer.new()
	orbit_row.add_theme_constant_override("separation", 10)
	column.add_child(orbit_row)
	orbit_row.add_child(_label("TARGET ORBIT", 13, Palette.SECONDARY))
	var lower := _button("◀", func() -> void: _step_altitude(-1))
	lower.custom_minimum_size = Vector2(44, 0)
	orbit_row.add_child(lower)
	_orbit_label = _label("—", 18, Palette.PRIMARY)
	_orbit_label.custom_minimum_size = Vector2(190, 0)
	_orbit_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	orbit_row.add_child(_orbit_label)
	var higher := _button("▶", func() -> void: _step_altitude(1))
	higher.custom_minimum_size = Vector2(44, 0)
	orbit_row.add_child(higher)

	var orbit_spacer := Control.new()
	orbit_spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	orbit_row.add_child(orbit_spacer)

	_plan_button = _button("SEARCH", func() -> void: _request_plan())
	orbit_row.add_child(_plan_button)
	_refresh_orbit()

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
	plan_requested.emit(current_target(), current_altitude_km(), current_altitude_km())


func set_targets(names: PackedStringArray, current: String) -> void:
	targets = names
	target_index = maxi(targets.find(current), 0)
	altitude_index = _default_altitude_for(current_target())
	_refresh_target()
	if _orbit_label != null:
		_refresh_orbit()


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
	altitude_index = _default_altitude_for(current_target())
	_refresh_target()
	_refresh_orbit()
	target_changed.emit(current_target())


func _refresh_target() -> void:
	_target_label.text = current_target().to_upper() if not targets.is_empty() else "—"


func _step_altitude(direction: int) -> void:
	altitude_index = wrapi(altitude_index + direction, 0, ALTITUDES_KM.size())
	_refresh_orbit()


func _refresh_orbit() -> void:
	_orbit_label.text = "%.0f km circular" % ALTITUDES_KM[altitude_index]


func current_altitude_km() -> float:
	return ALTITUDES_KM[altitude_index]


func _default_altitude_for(target: String) -> int:
	## 100 km para a Lua, 500 km para tudo o mais. Não é um número mágico por
	## corpo: é o default do milestone (regra 57) com a exceção que a campanha
	## lunar qualificou.
	var wanted := 100.0 if target == "Moon" else 500.0
	return maxi(ALTITUDES_KM.find(wanted), 0)


func _request_plan() -> void:
	if _searching:
		search_cancelled.emit()
		return
	_plan_button.text = "CANCEL SEARCH"
	_status.text = "starting the search…"
	_status.add_theme_color_override("font_color", Palette.SECONDARY)
	_pending_plan = true
	_searching = true


func show_progress(progress: Dictionary) -> void:
	## Regra 48: a interface diz o que está a acontecer enquanto acontece.
	##
	## Contagens, não trajetórias. Um relatório de progresso que trouxesse um
	## plano seria um segundo lugar de onde planos vêm.
	if progress.is_empty():
		return
	_searching = true
	_plan_button.text = "CANCEL SEARCH"
	_plan_button.disabled = false
	_execute_button.disabled = true
	var stage := String(progress.get("stage", "searching")).to_upper()
	var line := "SEARCHING TRAJECTORIES — %s\n  candidates tested %d of %d   flown %d   found %d" % [
		stage,
		int(progress.get("candidates_screened", 0)),
		int(progress.get("candidates_considered", 0)),
		int(progress.get("candidates_flown", 0)),
		int(progress.get("candidates_succeeded", 0))]
	var steps := int(progress.get("integrator_steps", 0))
	if steps > 0:
		line += "   %s integrator steps" % Fmt.count(steps)
	if progress.get("cancelled", false):
		line = "CANCELLING — the search stops between candidates\n" + line
	_status.add_theme_color_override("font_color", Palette.SECONDARY)
	_status.text = line


## Chamado pelo `Flight` depois de planejar (ou de falhar).
func show_plan(plan: Dictionary, error: String) -> void:
	_searching = false
	_plan_button.text = "SEARCH"
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
	## As geometrias que a busca de facto voou (regra 47), lado a lado.
	##
	## Uma TABELA e não uma lista, porque a pergunta que ela responde é uma
	## comparação: qual é mais rápida, qual é mais barata, o que custa a
	## diferença. Uma coluna por candidata viável, no máximo três -- a quarta não
	## cabe e, medido sobre cem épocas, nunca deslocou a vencedora.
	##
	## Cada número vem do planejador. Este painel não estima tempo de voo nem Δv
	## e não inventa uma linha quando a busca só encontrou uma opção (regra 47:
	## "não escrever valores fictícios estáticos").
	if alternatives.is_empty():
		return

	var feasible: Array = []
	var refused: Array = []
	for entry in alternatives:
		var alternative: Dictionary = entry
		if alternative.get("feasible", false):
			feasible.append(alternative)
		else:
			refused.append(alternative)

	var lines: Array[String] = ["", "[color=#8a939e]TRAJECTORIES FOUND[/color]"]
	if feasible.is_empty():
		lines.append("  none — every geometry the search flew was refused")
	else:
		# Ordenadas pelo tempo de voo, para que a coluna da esquerda seja sempre a
		# mais rápida: é a leitura que a regra 47 desenha.
		feasible.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			return float(a["time_of_flight_s"]) < float(b["time_of_flight_s"]))
		var shown: Array = feasible.slice(0, mini(3, feasible.size()))
		var headings := PackedStringArray()
		for i in range(shown.size()):
			headings.append(_alternative_name(i, shown.size()))
		lines.append("  %-12s %s" % ["", _row_of(headings)])
		lines.append("  %-12s %s" % ["FLIGHT", _row_of(_column(shown, func(a: Dictionary) -> String:
			return Fmt.duration(float(a["time_of_flight_s"]))))])
		lines.append("  %-12s %s" % ["DEPART ΔV", _row_of(_column(shown, func(a: Dictionary) -> String:
			return "%.0f m/s" % float(a["injection_delta_v"])))])
		lines.append("  %-12s %s" % ["CAPTURE ΔV", _row_of(_column(shown, func(a: Dictionary) -> String:
			return "%.0f m/s" % float(a["capture_delta_v"])))])
		lines.append("  %-12s %s" % ["TOTAL ΔV", _row_of(_column(shown, func(a: Dictionary) -> String:
			return "%.0f m/s" % float(a["total_delta_v"])))])
		lines.append("  %-12s %s" % ["ORBIT", _row_of(_column(shown, func(a: Dictionary) -> String:
			return "%.0f × %.0f km" % [float(a["predicted_periapsis_m"]) / 1000.0,
				float(a["predicted_apoapsis_m"]) / 1000.0]))])
		lines.append("  %-12s %s" % ["INC", _row_of(_column(shown, func(a: Dictionary) -> String:
			return "%.1f°" % float(a["predicted_inclination_deg"])))])

	if not refused.is_empty():
		lines.append("")
		lines.append("[color=#8a939e]REFUSED (%d)[/color]" % refused.size())
		for entry in refused:
			var alternative: Dictionary = entry
			lines.append("  %-22s %s" % [_short_label(String(alternative["label"])),
				String(alternative.get("failure", "?"))])
	_summary.text += "\n".join(lines)


func _alternative_name(index: int, total: int) -> String:
	## FAST / BALANCED / LOW ΔV, e apenas quando há três para nomear.
	##
	## Com duas opções, chamar uma de "BALANCED" seria inventar um meio-termo que
	## a busca não encontrou; com uma, qualquer rótulo é uma comparação com nada.
	if total >= 3:
		return ["FAST", "BALANCED", "LOW ΔV"][index]
	if total == 2:
		return ["FASTER", "CHEAPER"][index]
	return "ONLY OPTION"


func _column(rows: Array, reader: Callable) -> PackedStringArray:
	var out := PackedStringArray()
	for row in rows:
		out.append(String(reader.call(row)))
	return out


func _row_of(cells: PackedStringArray) -> String:
	var parts := PackedStringArray()
	for cell in cells:
		parts.append(cell.rpad(16))
	return "".join(parts)


func _short_label(label: String) -> String:
	var parts := label.split("/")
	return parts[1] if parts.size() > 1 else label


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
