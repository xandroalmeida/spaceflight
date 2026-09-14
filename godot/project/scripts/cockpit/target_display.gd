class_name TargetDisplay
extends Instrument
## O mostrador de alvo (regra 21).
##
## Distância, velocidade relativa, direção relativa, e a interceptação quando ela
## existe. A parte que merece uma nota é o "estimated intercept": ele é uma
## EXTRAPOLAÇÃO LINEAR -- distância a fechar dividida pela taxa a que ela fecha --
## e está rotulado como tal no mostrador, porque não é uma trajetória.
##
## A trajetória verdadeira quem faz é o planejador, e quando há um plano armado
## este mostrador mostra a chegada DELE em vez da extrapolação. A diferença entre
## as duas é grande: a extrapolação ignora a gravidade inteira.

func _draw() -> void:
	title = "TARGET"
	draw_frame()

	var target: String = data.get("target", "")
	if target.is_empty():
		draw_text_at(Vector2(size.x * 0.5, size.y * 0.45), "NO TARGET", 6.5, Palette.DIM,
			HORIZONTAL_ALIGNMENT_CENTER)
		draw_text_at(Vector2(size.x * 0.5, size.y * 0.62),
			"%s / %s to select" % [InputActions.label("target_prev"),
				InputActions.label("target_next")],
			3.8, Palette.DIM, HORIZONTAL_ALIGNMENT_CENTER)
		return

	var u := unit()
	draw_text_at(Vector2(u * 3.0, u * 15.0), target.to_upper(), 9.0, Palette.TARGET)

	var distance: float = data.get("target_distance_m", 0.0)
	var relative_speed: float = data.get("target_relative_speed_ms", 0.0)
	var closing: float = data.get("closing_speed_ms", 0.0)

	draw_field(Vector2(u * 3.0, u * 27.0), "DISTANCE", Fmt.distance(distance), 7.5)
	draw_field(Vector2(u * 3.0, u * 45.0), "REL SPEED", Fmt.speed(relative_speed), 7.5)
	# O sinal é a informação: fechando ou abrindo. Um módulo esconderia isso.
	draw_field(Vector2(u * 3.0, u * 63.0), "CLOSING",
		("%s%s" % ["-" if closing < 0.0 else "+", Fmt.speed(absf(closing))]),
		7.5, Palette.OK if closing > 0.0 else Palette.WARNING)

	_draw_bearing(Vector2(size.x * 0.76, size.y * 0.42), minf(size.x * 0.20, size.y * 0.28))
	_draw_intercept(Vector2(u * 3.0, size.y - u * 5.5), distance, closing)


func _draw_bearing(centre: Vector2, radius: float) -> void:
	## Onde o alvo está em relação ao nariz, na mesma projeção do mostrador de
	## voo -- a mesma convenção de tela, para que os dois não se contradigam.
	draw_arc(centre, radius, 0.0, TAU, 48, Palette.DIM, maxf(unit() * 0.22, 1.0))
	draw_tick(centre - Vector2(radius, 0.0), centre + Vector2(radius, 0.0), Palette.DIM, 0.16)
	draw_tick(centre - Vector2(0.0, radius), centre + Vector2(0.0, radius), Palette.DIM, 0.16)
	draw_marker(centre, maxf(unit() * 1.6, 2.0), Palette.PRIMARY, "nose")

	var directions: Dictionary = data.get("directions", {})
	if not directions.has("target"):
		return
	var basis: Basis = data.get("ship_basis", Basis.IDENTITY)
	var direction: Vector3 = directions["target"]
	var body := Vector3(basis.x.dot(direction), basis.y.dot(direction), basis.z.dot(direction))
	var angle := rad_to_deg(acos(clampf(body.x, -1.0, 1.0)))
	var lateral := Vector2(-body.y, -body.z)
	lateral = lateral.normalized() if lateral.length() > 1.0e-6 else Vector2(0.0, -1.0)
	var at := centre + lateral * radius * minf(angle / 180.0, 1.0)
	draw_marker(at, maxf(unit() * 1.8, 2.5), Palette.TARGET, "target")
	draw_text_at(centre + Vector2(0.0, radius + unit() * 5.0), "%.1f° off nose" % angle,
		3.6, Palette.SECONDARY, HORIZONTAL_ALIGNMENT_CENTER)


func _draw_intercept(at: Vector2, distance: float, closing: float) -> void:
	var plan_arrival: float = data.get("plan_seconds_to_arrival", -1.0)
	if plan_arrival > 0.0:
		draw_text_at(at, "ARRIVAL (PLANNED)  %s" % Fmt.countdown(plan_arrival), 4.4, Palette.PLAN)
		return
	if closing <= 0.0:
		draw_text_at(at, "NO INTERCEPT -- opening", 4.4, Palette.SECONDARY)
		return
	# Rotulado "linear" onde ele é lido, e não só neste comentário: o número
	# ignora a gravidade e um piloto que o confundisse com uma previsão de
	# chegada erraria por horas.
	draw_text_at(at, "CLOSE IN %s (linear)" % Fmt.duration(distance / closing), 4.4,
		Palette.SECONDARY)
