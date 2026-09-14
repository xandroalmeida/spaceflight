class_name MinimalHud
extends Instrument
## O HUD mínimo (regra 38).
##
## Existe para as vistas externas e para quem não quer o cockpit: uma faixa em
## baixo com o que se pilota e um canto com a missão. Nada mais.
##
## A hierarquia é deliberada e é o assunto da regra 38: velocidade, altitude e
## acelerador em corpo grande; o resto em corpo pequeno e cinza. Um HUD em que
## tudo tem o mesmo peso obriga a LER, e ler é o que um piloto não tem tempo de
## fazer.

const BAND_HEIGHT := 0.10      ## fração da altura da tela


func unit() -> float:
	## O HUD é dimensionado pela ALTURA DA TELA e não pela altura do controle:
	## ele ocupa a tela inteira, e uma unidade tirada de `size.y` daria letras de
	## dez centímetros. É por isso que este método é sobrescrito.
	return maxf(size.y / 100.0, 1.0) * 0.62


func _draw() -> void:
	if data.is_empty():
		return
	var u := unit()

	# No cockpit a faixa de baixo não é desenhada, e isso é a regra 38 aplicada e
	# não uma economia: o painel já mostra velocidade, altitude, acelerador e
	# propelente, em instrumentos, e repeti-los numa tarja por cima deles tapa
	# exatamente a parte da tela onde eles estão. O que sobra são os cantos --
	# warp, alvo, fase da missão -- que o painel não mostra.
	if data.get("cockpit_view", false):
		_corner_top_left()
		_corner_top_right()
		return

	var band := size.y * BAND_HEIGHT
	var base := size.y - band

	# A faixa não é uma barra opaca: é um degradê muito fraco, o suficiente para
	# o texto branco não desaparecer sobre o gelo polar da Terra. Uma faixa
	# sólida tapa exatamente a parte da vista que a nave costuma atravessar.
	draw_rect(Rect2(0.0, base, size.x, band), Color(0.0, 0.0, 0.0, 0.38), true)
	draw_line(Vector2(0.0, base), Vector2(size.x, base), Palette.PANEL_EDGE, 1.0)

	# Seis células, cada uma com UM valor.
	#
	# A primeira versão juntava apoapsis e periapsis numa célula: numa órbita de
	# estacionamento isso é "400,0 km / 400,0 km" e cabe, mas a caminho da Lua é
	# "362 658 km / -4 202,4 km" -- vinte e três caracteres que passavam por cima
	# da célula do acelerador. Uma célula, um número.
	var y := base + u * 9.0
	_big(Vector2(u * 6.0, y), "SPEED", Fmt.speed(data.get("speed_ms", 0.0)))
	_big(Vector2(size.x * 0.19, y), "ALTITUDE", Fmt.distance(data.get("altitude_m", 0.0)))
	_big(Vector2(size.x * 0.36, y), "APOAPSIS",
		Fmt.distance(data.get("apoapsis_altitude_m", 0.0)))
	_big(Vector2(size.x * 0.53, y), "PERIAPSIS",
		Fmt.distance(data.get("periapsis_altitude_m", 0.0)))

	_throttle(Vector2(size.x * 0.70, base + u * 3.0), size.x * 0.14, u)

	var propellant: float = data.get("propellant_kg", 0.0)
	var capacity: float = maxf(data.get("propellant_capacity_kg", 1.0), 1.0)
	_big(Vector2(size.x * 0.88, y), "PROPELLANT", Fmt.mass(propellant),
		Palette.CRITICAL if propellant / capacity < 0.08 else Palette.PRIMARY)

	_corner_top_left()
	_corner_top_right()


func _big(at: Vector2, label: String, value: String,
		colour: Color = Palette.PRIMARY) -> void:
	var u := unit()
	draw_text_at(at, label, 3.4, Palette.SECONDARY)
	draw_text_at(at + Vector2(0.0, u * 6.2), value, 6.0, colour)


func _throttle(at: Vector2, width: float, u: float) -> void:
	var throttle: float = data.get("throttle", 0.0)
	var thrust: float = data.get("thrust_n", 0.0)
	draw_text_at(at, "THROTTLE", 3.4, Palette.SECONDARY)
	draw_bar(Rect2(at + Vector2(0.0, u * 2.0), Vector2(width, u * 3.4)), throttle,
		Palette.ENGINE if thrust > 0.0 else Palette.DIM)
	draw_text_at(at + Vector2(0.0, u * 9.6), "%s   %s" % [Fmt.percent(throttle),
		Fmt.force(thrust)], 4.0, Palette.PRIMARY)


func _corner_top_left() -> void:
	var u := unit()
	var at := Vector2(u * 6.0, u * 8.0)
	draw_text_at(at, "%s   %s" % [Fmt.warp(data.get("time_warp", 1.0)),
		data.get("reference", "")], 5.0,
		Palette.WARNING if data.get("paused", false) else Palette.PRIMARY)
	if data.get("paused", false):
		draw_text_at(at + Vector2(0.0, u * 6.5), "PAUSED", 5.0, Palette.WARNING)
		return
	draw_text_at(at + Vector2(0.0, u * 6.5),
		"%s  %s" % [data.get("camera_mode", ""), data.get("rcs_activity", "")],
		4.0, Palette.SECONDARY)


func _corner_top_right() -> void:
	var u := unit()
	var right := size.x - u * 6.0
	var target: String = data.get("target", "")
	if not target.is_empty():
		draw_text_at(Vector2(right, u * 8.0), "TARGET %s" % target.to_upper(), 4.6,
			Palette.TARGET, HORIZONTAL_ALIGNMENT_RIGHT)
		draw_text_at(Vector2(right, u * 14.0),
			"%s   %s" % [Fmt.distance(data.get("target_distance_m", 0.0)),
				Fmt.speed(data.get("target_relative_speed_ms", 0.0))],
			4.0, Palette.SECONDARY, HORIZONTAL_ALIGNMENT_RIGHT)

	var phase: String = data.get("mission_phase", "")
	if phase.is_empty() or phase == "IDLE":
		return
	draw_text_at(Vector2(right, u * 22.0), phase, 5.2, Palette.PLAN,
		HORIZONTAL_ALIGNMENT_RIGHT)
	var next_event: String = data.get("next_event", "")
	if not next_event.is_empty():
		draw_text_at(Vector2(right, u * 28.0), next_event, 4.4, Palette.PLAN,
			HORIZONTAL_ALIGNMENT_RIGHT)
		draw_text_at(Vector2(right, u * 34.0),
			Fmt.countdown(data.get("next_event_seconds", 0.0)), 6.0, Palette.PLAN,
			HORIZONTAL_ALIGNMENT_RIGHT)
