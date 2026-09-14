class_name SystemDisplay
extends Instrument
## O mostrador de sistemas: motor, propelente, RCS, relatividade
## (regras 14, 15, 19, 22).
##
## É a faixa larga do painel -- 1280 × 122, quase onze para um -- e o layout é
## escrito PARA essa forma em vez de herdado dos mostradores quadrados. A
## primeira versão usava o mesmo `unit()` deles, que é altura sobre cem: numa
## faixa de 122 px isso dá 1,2 px por unidade, e um campo posicionado em "66
## unidades" cai 80 px abaixo do topo de uma caixa de 122 -- por cima da fileira
## de botões. Tudo aqui é fração da altura real.
##
## A relatividade tem uma célula e só uma. A regra 22 pede que ela apareça como
## instrumento de verdade e que NÃO ocupe metade do cockpit em regime
## convencional; a 1e-4 c ela cabe numa linha, e é isso que ela merece.

const COLUMNS := 6


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Palette.BACKGROUND, true)
	draw_rect(Rect2(Vector2.ONE, size - Vector2.ONE * 2.0), Palette.PANEL_EDGE, false, 1.0)
	if data.is_empty():
		return

	var h := size.y
	var pad := h * 0.07
	var column := (size.x - pad * 2.0) / float(COLUMNS)
	for i in range(1, COLUMNS):
		var x := pad + column * float(i)
		draw_line(Vector2(x, h * 0.12), Vector2(x, h * 0.88), Palette.PANEL_EDGE, 1.0)

	_engine(pad + column * 0.0, column, h)
	_thrust(pad + column * 1.0, column, h)
	_propellant(pad + column * 2.0, column, h)
	_mass(pad + column * 3.0, column, h)
	_rcs(pad + column * 4.0, column, h)
	_relativity(pad + column * 5.0, column, h)


# --- células -----------------------------------------------------------------

func _label(at: Vector2, text: String, h: float, colour: Color = Palette.SECONDARY) -> void:
	draw_string(font(), at, text, HORIZONTAL_ALIGNMENT_LEFT, -1,
		maxi(int(h * 0.105), 8), colour)


func _value(at: Vector2, text: String, h: float, colour: Color = Palette.PRIMARY,
		scale: float = 0.175) -> void:
	draw_string(font(), at, text, HORIZONTAL_ALIGNMENT_LEFT, -1,
		maxi(int(h * scale), 9), colour)


func _cell(x: float, width: float, h: float, label: String, value: String,
		colour: Color = Palette.PRIMARY) -> void:
	var inset := width * 0.06
	_label(Vector2(x + inset, h * 0.26), label, h)
	_value(Vector2(x + inset, h * 0.56), value, h, colour)


func _engine(x: float, width: float, h: float) -> void:
	var inset := width * 0.06
	var throttle: float = data.get("throttle", 0.0)
	var thrust: float = data.get("thrust_n", 0.0)
	_label(Vector2(x + inset, h * 0.26), "MAIN ENGINE  %s" % data.get("engine_mode", "?"), h)

	# A régua do acelerador (regra 14). A barra é o COMANDO; o rótulo ao lado é o
	# ESTADO, que vem do empuxo que o core está de facto a produzir. Com o tanque
	# vazio a tecla continua a funcionar e a barra continua a subir -- e o estado
	# passa a dizer SAFE, que é a única das duas leituras que é sobre a nave.
	var bar := Rect2(Vector2(x + inset, h * 0.38), Vector2(width - inset * 2.0, h * 0.16))
	draw_bar(bar, throttle, Palette.ENGINE if thrust > 0.0 else Palette.DIM)
	var status := "RUNNING" if thrust > 0.0 else ("ARMED" if data.get("engine_armed", true)
		else "SAFE")
	var colour := Palette.OK if thrust > 0.0 else Palette.SECONDARY
	_value(Vector2(x + inset, h * 0.86), "%s  %s" % [Fmt.percent(throttle), status], h, colour,
		0.15)


func _thrust(x: float, width: float, h: float) -> void:
	var thrust: float = data.get("thrust_n", 0.0)
	_cell(x, width, h, "THRUST", Fmt.force(thrust),
		Palette.ENGINE if thrust > 0.0 else Palette.PRIMARY)
	_value(Vector2(x + width * 0.06, h * 0.86),
		"%s kg/s" % Fmt.sci(data.get("mass_flow_kg_s", 0.0), 3), h, Palette.SECONDARY, 0.125)


func _propellant(x: float, width: float, h: float) -> void:
	var inset := width * 0.06
	var propellant: float = data.get("propellant_kg", 0.0)
	var capacity: float = maxf(data.get("propellant_capacity_kg", 1.0), 1.0)
	var fraction := propellant / capacity
	var colour := Palette.OK
	if fraction < 0.25:
		colour = Palette.WARNING
	if fraction < 0.08:
		colour = Palette.CRITICAL

	_label(Vector2(x + inset, h * 0.26), "PROPELLANT", h)
	draw_bar(Rect2(Vector2(x + inset, h * 0.38), Vector2(width - inset * 2.0, h * 0.16)),
		fraction, colour)
	_value(Vector2(x + inset, h * 0.86), "%s  %s" % [Fmt.mass(propellant), Fmt.percent(fraction)],
		h, colour, 0.15)


func _mass(x: float, width: float, h: float) -> void:
	_cell(x, width, h, "TOTAL MASS", Fmt.mass(data.get("mass_kg", 0.0)))
	_value(Vector2(x + width * 0.06, h * 0.86),
		"ΔV %s" % Fmt.speed(data.get("delta_v_budget_ms", 0.0)),
		h, Palette.SECONDARY, 0.125)


func _rcs(x: float, width: float, h: float) -> void:
	var inset := width * 0.06
	var enabled: bool = data.get("rcs_enabled", true)
	var firing: int = data.get("rcs_firing", 0)
	var total: int = maxi(data.get("rcs_thrusters", 12), 1)

	_label(Vector2(x + inset, h * 0.26), "REACTION CONTROL", h)
	_value(Vector2(x + inset, h * 0.55), "RCS %s" % ("ON" if enabled else "OFF"), h,
		Palette.OK if enabled else Palette.WARNING, 0.165)
	# Só a atividade. O modo de apontamento e o erro estavam aqui também e a
	# linha passava por cima da célula da relatividade -- e eles já estão no
	# mostrador de voo, grandes, com a linha do diretor a apontar para eles.
	_value(Vector2(x + inset, h * 0.86), data.get("rcs_activity", "IDLE"), h,
		Palette.NAV if firing > 0 else Palette.SECONDARY, 0.125)

	# Uma lâmpada por bico, na ordem em que o core os declara, acesas pelo
	# ACIONAMENTO e não pela tecla (regra 15). Numa faixa desta forma elas cabem
	# numa fileira única de doze, à direita da célula -- e assim vê-se de relance
	# que um comando diagonal abre quatro a frações diferentes.
	var throttles: PackedFloat64Array = data.get("rcs_throttles", PackedFloat64Array())
	var lamps_x := x + width * 0.54
	var lamp_w := (width * 0.42) / float(total)
	for i in range(total):
		var open: float = throttles[i] if i < throttles.size() else 0.0
		var cell := Rect2(Vector2(lamps_x + lamp_w * float(i) + 1.0, h * 0.34),
			Vector2(maxf(lamp_w - 2.0, 1.0), h * 0.30))
		if open <= 0.002:
			draw_rect(cell, Palette.PANEL_EDGE, true)
		else:
			draw_rect(cell, Palette.RCS * Color(1.0, 1.0, 1.0, 0.35 + 0.65 * open), true)
			draw_rect(cell, Palette.RCS, false, 1.0)
	_value(Vector2(lamps_x, h * 0.86), "%d/%d" % [firing, total], h, Palette.SECONDARY, 0.125)


func _relativity(x: float, width: float, h: float) -> void:
	var inset := width * 0.06
	var beta: float = data.get("beta", 0.0)
	var colour := Palette.PRIMARY if beta > 1.0e-3 else Palette.SECONDARY
	_label(Vector2(x + inset, h * 0.26), "RELATIVITY", h)
	# Regra 22 por inteiro numa célula: β, γ−1, a diferença entre o tempo
	# coordenado e o próprio, e a aceleração própria. A 7,7 km/s isso é
	# 1,0e-4, 5,1e-9, dezenas de femtossegundos e zero -- e é exatamente isso
	# que tem de se ler. Os valores completos, com todos os dígitos, estão no
	# HUD técnico.
	# Dois algarismos e não três, e o texto medido contra a largura da célula: com
	# três, as duas linhas saíam pela borda direita do mostrador e liam-se
	# "γ-1 5.02e" e "a 0.00 m". Quem quiser os dígitos todos tem o HUD técnico.
	_value(Vector2(x + inset, h * 0.55),
		"β %s    a %.2f m/s²" % [Fmt.sci(beta, 2),
			data.get("proper_acceleration_ms2", 0.0)],
		h, colour, 0.13)
	_value(Vector2(x + inset, h * 0.86),
		"γ-1 %s   Δt %s s" % [Fmt.sci(data.get("lorentz_factor_minus_one", 0.0), 2),
			Fmt.sci(data.get("clock_difference_s", 0.0), 2)],
		h, Palette.SECONDARY, 0.10)
