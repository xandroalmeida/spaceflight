class_name DebugHud
extends Control
## O mostrador técnico: todos os números (regra 39).
##
## É a leitura do Milestone 5, preservada. Ela não foi reduzida nem "melhorada":
## é ela que a verificação sem tela imprime, é contra ela que as tolerâncias de
## `docs/validation/tolerances.md` são conferidas, e um número que saísse daqui
## deixaria de ser conferível. O que mudou é que ela deixou de ser a interface
## PADRÃO e passou a estar atrás de `F3` -- que é o que a regra 39 pede.
##
## A fonte é dimensionada pelo CONTEÚDO e não por palpite: `_fit` pergunta à
## fonte a altura de uma linha e a largura da mais comprida, e desce o tamanho
## até que as duas caibam. O palpite anterior -- altura da viewport sobre 38 --
## não sabia quantas linhas havia, e quando a leitura cresceu de 33 para 52 ela
## passou da borda de baixo da janela.

const FONT_MIN := 10.0
const FONT_MAX := 22.0

var flight: Node                ## o orquestrador; lê-se dele e não se escreve nele
var readout: Label
var margin: MarginContainer

var _last_shape := Vector2i.ZERO
var _font: Font


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE

	margin = MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_TOP_LEFT)
	margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(margin)

	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.0, 0.0, 0.0, 0.62)
	style.corner_radius_top_left = 6
	style.corner_radius_top_right = 6
	style.corner_radius_bottom_left = 6
	style.corner_radius_bottom_right = 6
	style.content_margin_left = 12
	style.content_margin_right = 12
	style.content_margin_top = 8
	style.content_margin_bottom = 8
	panel.add_theme_stylebox_override("panel", style)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	margin.add_child(panel)

	_font = Palette.mono_font()
	readout = Label.new()
	readout.add_theme_font_override("font", _font)
	readout.add_theme_color_override("font_color", Color(0.88, 0.92, 1.0))
	readout.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(readout)

	rescale()
	get_viewport().size_changed.connect(rescale)


func rescale() -> void:
	## Só as margens aqui; a fonte sai do conteúdo, em `_fit`.
	var height := get_viewport_rect().size.y
	if height <= 0.0:
		height = 720.0
	var px := int(maxf(roundf(height * 0.012), 6.0))
	for side in ["margin_left", "margin_top", "margin_right", "margin_bottom"]:
		margin.add_theme_constant_override(side, px)
	_last_shape = Vector2i.ZERO


func render() -> void:
	if not visible:
		return
	var lines := hud_lines(false)
	if lines.is_empty():
		return
	# 52 linhas numa janela de 900 px cabem a 10 px -- legível, mal, e ninguém
	# quer ler. A face é MONOESPAÇADA, então duas colunas não precisam de layout
	# nenhum: preenche-se a esquerda e concatena-se.
	var shown := _two_columns(lines)
	readout.text = "\n".join(shown)
	_fit(shown)


func _fit(lines: Array) -> void:
	var longest := ""
	for line in lines:
		if (line as String).length() > longest.length():
			longest = line
	var shape := Vector2i(lines.size(), longest.length())
	if shape == _last_shape:
		return                       # nada mudou; não mexer no tema a cada quadro
	_last_shape = shape

	var view := get_viewport_rect().size
	var gap := float(margin.get_theme_constant("margin_left"))
	var available_h := view.y - 2.0 * gap - 16.0
	var available_w := view.x - 2.0 * gap - 24.0
	var spacing := float(readout.get_theme_constant("line_spacing"))

	var px := int(FONT_MAX)
	while px > int(FONT_MIN):
		var text_h := float(lines.size()) * (_font.get_height(px) + spacing)
		var text_w := _font.get_string_size(longest, HORIZONTAL_ALIGNMENT_LEFT, -1, px).x
		if text_h <= available_h and text_w <= available_w:
			break
		px -= 1
	readout.add_theme_font_size_override("font_size", px)


func _two_columns(lines: Array) -> Array:
	## Corta na linha em branco mais próxima do meio, para nunca partir uma seção,
	## e preenche a coluna da esquerda até uma largura fixa. Só é correto porque a
	## fonte é monoespaçada -- que ela é de propósito, e por esta mesma razão.
	if lines.size() < 24:
		return lines

	var middle: int = lines.size() / 2
	var split := middle
	for offset in range(0, middle):
		if middle - offset > 0 and (lines[middle - offset] as String).is_empty():
			split = middle - offset
			break
		if middle + offset < lines.size() and (lines[middle + offset] as String).is_empty():
			split = middle + offset
			break

	var left: Array = lines.slice(0, split)
	var right: Array = lines.slice(split + 1)     # descarta a linha em branco do corte
	var width := 0
	for line in left:
		width = maxi(width, (line as String).length())

	var out: Array = []
	for i in range(maxi(left.size(), right.size())):
		var l: String = left[i] if i < left.size() else ""
		var r: String = right[i] if i < right.size() else ""
		out.append((l.rpad(width) + "   " + r).rstrip(" ") if not r.is_empty() else l)
	return out


# --- o conteúdo, preservado do Milestone 5 -----------------------------------

func hud_lines(compact: bool) -> Array:
	var simulation: SpaceflightSimulation = flight.simulation
	var s := simulation.get_snapshot()
	if s.is_empty():
		return []

	if compact:
		return _compact_lines(s)

	# O `%` do GDScript aceita %s %c %d %o %x %X %f %v %% -- e NÃO %e nem %g. A
	# notação científica passa por `Fmt.sci`; a primeira versão deste script usava
	# %g e todo quadro disparava um erro de formatação.
	var lines := [
		"t (TDB)        %+.3f s since J2000" % s["time_tdb_s"],
		"elapsed        %.6f s   warp %.0fx" % [s["elapsed_s"], s["time_warp"]],
		"proper time    %.6f s" % s["proper_time_s"],
		"clock diff     %s s" % Fmt.sci(s["clock_difference_s"]),
		"",
		"reference      %s" % s["reference"],
		"altitude       %.3f km" % (s["altitude_m"] / 1000.0),
		"speed          %.3f m/s" % s["speed_ms"],
		"acceleration   %.6f m/s^2" % s["acceleration_ms2"],
		"",
		"apoapsis       %.3f km" % (s["apoapsis_m"] / 1000.0),
		"periapsis      %.3f km" % (s["periapsis_m"] / 1000.0),
		"eccentricity   %.8f" % s["eccentricity"],
		"inclination    %.4f deg" % s["inclination_deg"],
		"period         %.2f s" % s["period_s"],
		"",
		"mass           %.1f kg" % s["mass_kg"],
		"propellant     %.3f kg" % s["propellant_kg"],
		"flow           %s kg/s   endurance %s"
			% [Fmt.sci(s["mass_flow_kg_s"]), Fmt.duration(s["endurance_s"])],
		"engine         %s   w = %.3f c" % [s["engine_mode"], s["exhaust_velocity_c"]],
		"throttle       %.0f %%      thrust %.1f N" % [s["throttle"] * 100.0, s["thrust_n"]],
		"delta-v left   %s m/s" % Fmt.sci(s["delta_v_budget_ms"]),
		"along track    %+.3f   orbit energy %+.1f J/kg/s"
			% [s["thrust_along_track"], s["specific_energy_rate"]],
		"",
		"pointing       %s   error %.3f deg" % [s["pointing_mode"], s["pointing_error_deg"]],
		"nose->prograde %.3f deg" % s["angle_to_prograde_deg"],
		"nose->nadir    %.3f deg" % s["angle_to_nadir_deg"],
		"spin rate      %.4f deg/s" % s["rotation_rate_deg_s"],
		"rcs            %s   %d of %d firing" % [flight.controls.rcs_activity(flight.rcs_firing_count()),
			flight.rcs_firing_count(), flight.rcs_thruster_count()],
		"target         %s at %.0f km, %.1f m/s"
			% [s["target"], s["target_distance_m"] / 1000.0, s["target_relative_speed_ms"]],
		"",
		"beta           %s" % Fmt.sci(s["beta"]),
		"gamma - 1      %s" % Fmt.sci(s["lorentz_factor_minus_one"]),
		"render res.    %s m per float ulp at %s"
			% [Fmt.sci(s["render_resolution_m"]), s["reference"]],
		"",
	]
	lines.append_array(mission_lines())
	lines.append_array(sky_lines())
	lines.append_array(_frame_lines())
	lines.append_array(_control_lines())
	return lines


func _compact_lines(s: Dictionary) -> Array:
	var sky_compact := []
	var sky: SpaceflightSky = flight.sky
	if sky != null and sky.is_ready():
		var d := sky.get_diagnostics()
		sky_compact = [
			"",
			"D ahead %.4f  astern %.4f" % [d["max_doppler"], d["min_doppler"]],
			"look  %s  D %.6f" % [flight.camera_rig.mode_name(),
				sky.get_doppler_in_direction(flight.camera_rig.look_direction())],
		]
	return ([
		"warp %.0fx   %s" % [s["time_warp"], s["reference"]],
		"altitude   %.1f km" % (s["altitude_m"] / 1000.0),
		"speed      %.1f m/s" % s["speed_ms"],
		"apo/peri   %.1f / %.1f km" % [s["apoapsis_m"] / 1000.0, s["periapsis_m"] / 1000.0],
		"propellant %.1f kg   throttle %.0f %%" % [s["propellant_kg"], s["throttle"] * 100.0],
		"engine     %s" % s["engine_mode"],
		"pointing   %s  err %.2f deg" % [s["pointing_mode"], s["pointing_error_deg"]],
		"beta       %s" % Fmt.sci(s["beta"]),
	] + sky_compact)


func mission_lines() -> Array:
	var simulation: SpaceflightSimulation = flight.simulation
	var lines: Array = []
	# A órbita em torno do alvo, quando existe. É a linha que diz se a missão
	# funcionou, e ela fica ACIMA do plano e não abaixo: uma vez em órbita lunar
	# o plano é história.
	var o := simulation.get_orbit_about_target() if simulation != null else {}
	if not o.is_empty():
		var radius: float = o["radius_m"]
		lines.append("about %-9s %s   %.1f km at %.1f m/s"
			% [o["body"], ("CAPTURED" if o["captured"] else "hyperbolic"),
			   float(o["distance_m"]) / 1000.0, o["speed_ms"]])
		if o["captured"]:
			lines.append("  orbit        %.1f x %.1f km altitude, e %.4f, i %.2f deg, %s"
				% [(float(o["periapsis_m"]) - radius) / 1000.0,
				   (float(o["apoapsis_m"]) - radius) / 1000.0,
				   o["eccentricity"], o["inclination_deg"],
				   Fmt.duration(o["period_s"])])
		lines.append("")

	if simulation == null or not simulation.has_plan():
		lines.append_array(["mission        none   (%s: plan a transfer, %s: %s)"
			% [InputActions.label("mission_plan"), InputActions.label("execution_model"),
			   simulation.get_execution_model()], ""])
		return lines
	var p := simulation.get_plan()
	if p.is_empty():
		return lines

	## Seção 15 do M6.2: tudo o que o computador mostra ANTES da execução, e cada
	## número vindo das `MissionMetrics` do core. Nada neste painel é calculado em
	## GDScript -- o HUD renomeia campos e formata-os, que é o todo do que o
	## renderizador pode fazer com uma trajetória.
	var to_ignition: float = p["seconds_to_ignition"]
	var to_insertion: float = p["seconds_to_insertion"]
	lines.append_array([
		"mission        %s to %s   (%s: abort)"
			% [p.get("phase", "?"), p["target"], InputActions.label("mission_abort")],
		"departure      %s   %s branch, %.2f d of flight"
			% [("in " + Fmt.duration(to_ignition)) if to_ignition > 0.0 else "past",
			   p.get("branch", "?"), p.get("time_of_flight_days", 0.0)],
		"arrival        %s"
			% [("in " + Fmt.duration(to_insertion)) if to_insertion > 0.0 else "past"],
		"injection      %.1f m/s over %s"
			% [p["injection_delta_v"], Fmt.duration(p.get("injection_duration_s", 0.0))],
		"midcourse      %.1f m/s   (folded into the injection, not a separate burn)"
			% p.get("midcourse_delta_v", 0.0),
		"capture        %.1f m/s over %s"
			% [p["insertion_delta_v"], Fmt.duration(p.get("insertion_duration_s", 0.0))],
		"total dv       %.1f m/s of %.0f available"
			% [p.get("total_delta_v", 0.0), p.get("delta_v_available", 0.0)],
		"predicted      %.1f x %.1f km, e %.4f, i %.2f deg, RAAN %.1f deg"
			% [float(p.get("predicted_periapsis_m", 0.0)) / 1000.0,
			   float(p.get("predicted_apoapsis_m", 0.0)) / 1000.0,
			   p.get("predicted_eccentricity", 0.0),
			   p.get("predicted_inclination_deg", 0.0),
			   p.get("predicted_raan_deg", 0.0)],
		"propellant     %.2f kg required, %.2f kg left after"
			% [p.get("propellant_required_kg", 0.0), p.get("propellant_remaining_kg", 0.0)],
		"autopilot      lag %.2f deg mean / %.2f deg peak, RCS %.1f g, duty %.3f"
			% [p.get("pointing_error_mean_deg", 0.0), p.get("pointing_error_peak_deg", 0.0),
			   float(p.get("rcs_propellant_kg", 0.0)) * 1000.0,
			   p.get("rcs_duty_cycle", 0.0)],
	])

	## Seção 17: previsto contra realizado, depois de a órbita assentar. É a linha
	## que diz se o simulador prevê a própria física, e só existe depois do facto
	## -- razão pela qual está abaixo do plano e não dentro dele.
	var outcome := simulation.get_mission_outcome()
	if not outcome.is_empty():
		lines.append("")
		lines.append("               %14s %14s %14s" % ["predicted", "actual", "difference"])
		lines.append_array([
			_outcome_line("periapsis km", outcome.get("periapsis_m", {}), 1.0e-3),
			_outcome_line("apoapsis km", outcome.get("apoapsis_m", {}), 1.0e-3),
			_outcome_line("eccentricity", outcome.get("eccentricity", {}), 1.0),
			_outcome_line("inclination", outcome.get("inclination_deg", {}), 1.0),
			_outcome_line("propellant kg", outcome.get("propellant_kg", {}), 1.0),
			_outcome_line("capture dv", outcome.get("capture_delta_v", {}), 1.0),
		])
		# A chegada só como DIFERENÇA. As épocas absolutas são 8,2e8 s desde J2000
		# e imprimi-las num campo de 14 colunas mostraria nove dígitos de acordo e
		# esconderia o número que alguém quer.
		var arrival: Dictionary = outcome.get("arrival_tdb_s", {})
		if not arrival.is_empty() and arrival.get("recorded", false):
			lines.append("  %-12s %14s %14s %14.1f" % ["arrival s", "-", "-",
				float(arrival["difference"])])
		if outcome.get("note", "") != "":
			lines.append("  note: %s" % outcome["note"])
	lines.append("")
	return lines


func _outcome_line(label: String, row: Dictionary, scale: float) -> String:
	## Uma linha que nunca foi registrada imprime-se como tal, e não como três
	## zeros. Uma diferença zero é uma afirmação; "não registrado" é a verdade
	## quando o voo nunca passou pela fase que a mediria.
	if row.is_empty() or not row.get("recorded", false):
		return "  %-12s %14s" % [label, "not recorded"]
	return "  %-12s %14.4f %14.4f %14.4f" % [label, float(row["predicted"]) * scale,
		float(row["actual"]) * scale, float(row["difference"]) * scale]


func sky_lines() -> Array:
	## O que a ótica está a fazer, em números, para que "parece rápido" nunca seja
	## a evidência. Cada valor saiu de `core/render/relativistic_sky.cpp`.
	var sky: SpaceflightSky = flight.sky
	var simulation: SpaceflightSimulation = flight.simulation
	if sky == null or not sky.is_ready():
		return ["sky            no catalogue (scripts/fetch_star_catalog.sh)", ""]

	var d := sky.get_diagnostics()
	var moon_light := 0.0
	for i in range(simulation.get_body_count()):
		if simulation.get_body_name(i) == "Moon":
			moon_light = simulation.get_body_light_time(i)

	var world: CelestialView = flight.celestial
	return [
		"stars          %d  visual beta %s" % [d["star_count"], flight.visual_beta_label()],
		"effects        aberration %s  Doppler %s  beaming %s  retarded %s"
			% ["ON" if world.effect_aberration else "OFF",
			   "ON" if world.effect_doppler else "OFF",
			   "ON" if world.effect_beaming else "OFF",
			   "ON" if world.effect_retarded else "OFF"],
		"forward cone   %.3f deg holds %d stars (%.2f %%)"
			% [d["forward_cone_deg"], d["stars_in_forward_cone"],
			   100.0 * float(d["fraction_in_forward_cone"])],
		"doppler        %.6f astern .. %.6f ahead" % [d["min_doppler"], d["max_doppler"]],
		# Sete dígitos, não quatro: a beta = 1e-4 o sinal inteiro está na sexta
		# casa (1.000464) e a beta = 0.9 ele vale 51,48. Um formato tem de carregar
		# os dois, e os dígitos são de graça.
		"5800 K star    %s x ahead, %s x astern  (visible band)"
			% [Fmt.sci(d["reference_forward_visible"], 7),
			   Fmt.sci(d["reference_aft_visible"], 7)],
		"exposure       %.4f half-saturation flux" % sky.get_half_saturation(),
		"light time     Moon %.4f s" % moon_light,
	] + look_lines(d)


func look_lines(d: Dictionary) -> Array:
	## Para onde a câmera aponta, e para dentro de quê. O ÂNGULO é geometria da
	## câmera e é medido aqui; o fator Doppler NÃO é -- vem de
	## `core/relativity/optics.hpp` através do céu, porque é física.
	##
	## O ângulo é medido a partir da velocidade BARICÊNTRICA e não do prógrado, e
	## a diferença não é preciosismo: o prógrado deste cockpit é relativo ao corpo
	## de referência (7,7 km/s em torno da Terra) enquanto o céu é aberrado pela
	## velocidade no referencial em que as estrelas estão paradas (30,7 km/s). Em
	## órbita baixa os dois apontam a uns 30 graus de distância.
	var sky: SpaceflightSky = flight.sky
	var rig: CameraRig = flight.camera_rig
	var forward := rig.look_direction()
	var axis := Vector3(flight.simulation.get_beta_vector()).normalized()
	var angle := rad_to_deg(forward.angle_to(axis)) if axis.length() > 0.5 else 0.0
	var inside := "  INSIDE the forward cone" if angle <= float(d["forward_cone_deg"]) else ""
	var where := rig.mode_name() if rig.at_preset() else "%s (free)" % rig.mode_name()
	var zoom := "" if is_equal_approx(rig.orbit_zoom, 1.0) else "   zoom %.2fx" % rig.orbit_zoom
	return [
		"look           %s   %.1f deg off the aberration axis%s%s"
			% [where, angle, inside, zoom],
		"looking into   D = %.6f" % sky.get_doppler_in_direction(forward),
		"",
	]


func sky_projection_lines(beta: float) -> Array:
	## O que o MESMO código faz a uma velocidade que a nave não tem. Rotulado,
	## todas as vezes, porque um número sem rótulo aqui seria exatamente a
	## mentira silenciosa que este projeto existe para evitar: o estado está
	## intacto, e isto é uma pergunta feita à ótica, não uma afirmação sobre o voo.
	var sky: SpaceflightSky = flight.sky
	if sky == null or not sky.is_ready():
		return []
	var heading: Vector3 = flight.simulation.get_spacecraft_velocity_direction()
	var d := sky.get_diagnostics_at(heading * beta)
	return [
		"PROJECTION at beta = %.4f (the ship is NOT at this speed; the state is untouched)"
			% beta,
		"  forward cone %.3f deg holds %d stars (%.2f %%)"
			% [d["forward_cone_deg"], d["stars_in_forward_cone"],
			   100.0 * float(d["fraction_in_forward_cone"])],
		"  doppler      %.4f astern .. %.4f ahead" % [d["min_doppler"], d["max_doppler"]],
		"  5800 K star  %s x ahead, %s x astern  (visible band, not bolometric)"
			% [Fmt.sci(d["reference_forward_visible"]),
			   Fmt.sci(d["reference_aft_visible"])],
		"",
	]


func _frame_lines() -> Array:
	## O que a regra 39 acrescentou: quadro, integrador, e em que estado a missão
	## está. Medido e não estimado -- `Performance` é o contador do próprio Godot.
	return [
		"fps            %.1f   frame %.2f ms   process %.2f ms"
			% [Engine.get_frames_per_second(),
			   Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0
				+ Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0,
			   Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0],
		"integrator     Dormand-Prince 5(4), adaptive; frame asks for time, never for steps",
		"frame          J2000 / SSB   render scale %s m per unit"
			% Fmt.sci(1.0 / flight.simulation.get_render_scale()),
		"camera         %s   focus %s   body scale %s"
			% [flight.camera_rig.mode_name(),
			   "spacecraft" if flight.camera_rig.focus_index < 0
				else flight.simulation.get_body_name(flight.camera_rig.focus_index),
			   flight.body_scale_label()],
		"",
	]


func _control_lines() -> Array:
	## Gerados a partir do `InputMap`, e não escritos à mão: uma lista de teclas
	## copiada é uma lista de teclas que envelhece em silêncio.
	var out: Array = []
	var groups := InputActions.by_group()
	for group in groups:
		var parts: Array[String] = []
		for entry in groups[group]:
			parts.append("%s %s" % [entry["key"], entry["description"]])
		out.append("(%s: %s)" % [group, "   ".join(parts)])
	return out
