class_name OrbitMap
extends Instrument
## O mapa orbital (regras 20, 53, 54, 55).
##
## Tudo o que este arquivo desenha veio pronto do core:
##
##   `get_orbit_track()`         a elipse osculadora da nave
##   `get_body_orbit_track()`    o caminho do alvo, amostrado da EFEMÉRIDE
##   `get_planned_trajectory()`  o arco que o planejador voou
##   `get_maneuvers()`           onde cada queima acende
##
## O mapa projeta pontos e desenha linhas. Ele não integra, não resolve Kepler,
## não propaga e não tem opinião sobre para onde a nave vai (regra 54).
##
## A projeção é a mesma do mostrador de navegação -- o plano da órbita atual --
## e a razão é a mesma: uma projeção fixa esconde a excentricidade de uma órbita
## inclinada atrás de um escorço.
##
## O caminho da Lua é amostrado da efeméride e NÃO de uma elipse, e essa é a
## diferença entre desenhar a órbita que a Lua tem e desenhar a que ela teria se
## o Sol não existisse: centenas de quilômetros.

const MARGIN := 0.10

var zoom := 1.0
var _u := Vector3.RIGHT
var _v := Vector3.UP
var _origin := Vector2.ZERO
var _box := 1.0
var _centre := Vector3.ZERO
var _extent := 1.0


func unit() -> float:
	return maxf(size.y / 100.0, 1.0) * 0.55


func _draw() -> void:
	# Opaco, sem alfa nenhum. A 82 % o cockpit aparecia por trás e as linhas da
	# órbita cruzavam com o texto dos mostradores; a 98,5 % ainda se lia o painel
	# através do mapa, porque o painel é claro e o mapa é escuro. Um mapa que se
	# lê por cima de outro mostrador não se lê. O que o mapa tapa, ele tapa de
	# propósito; `M` fecha-o.
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.016, 0.020, 0.027), true)
	if data.is_empty():
		return

	var track: PackedVector3Array = data.get("orbit_track", PackedVector3Array())
	var plan: PackedVector3Array = data.get("planned_trajectory", PackedVector3Array())
	var moon: PackedVector3Array = data.get("target_track", PackedVector3Array())
	_centre = data.get("reference_position", Vector3.ZERO)

	var everything: Array[PackedVector3Array] = [track, plan, moon]
	_extent = 0.0
	for set_of_points in everything:
		for point in set_of_points:
			_extent = maxf(_extent, (point - _centre).length())
	_extent = maxf(_extent, (Vector3(data.get("ship_position", Vector3.ZERO)) - _centre).length())
	if _extent <= 0.0:
		return
	_extent /= zoom

	_plane(track, plan)
	_box = minf(size.x, size.y) * (0.5 - MARGIN)
	_origin = Vector2(size.x * 0.5, size.y * 0.5)

	_draw_grid()
	_draw_body(_centre, data.get("reference_radius", 0.0), data.get("reference", "?"),
		Palette.NAV_DIM)
	if moon.size() > 2:
		_polyline(moon, Palette.TARGET.darkened(0.45), 0.22)
	if plan.size() > 2:
		_polyline(plan, Palette.PLAN, 0.30)
	if track.size() > 2:
		_polyline(track, Palette.NAV, 0.26)

	var target_position: Vector3 = data.get("target_position", Vector3.ZERO)
	if data.get("has_target", false):
		_draw_body(target_position, data.get("target_radius", 0.0),
			data.get("target", "TARGET"), Palette.TARGET)

	_draw_maneuvers()
	_draw_ship()
	_draw_legend()


func _plane(track: PackedVector3Array, plan: PackedVector3Array) -> void:
	## O plano do desenho: o que contém o corpo central, a NAVE e o ALVO.
	##
	## ⚠️ A primeira versão usava o plano da órbita atual, e isso está errado para
	## um mapa de transferência por uma razão geométrica simples: a órbita de
	## estacionamento tem 51,6° de inclinação, a Lua anda perto da eclíptica, e a
	## projeção de um ponto sobre um plano perde a componente normal. Com a Lua
	## quase sobre a normal da órbita, o marcador dela colapsava para cima da
	## Terra -- a 384 000 km de distância, desenhados como zero.
	##
	## Construir o plano a partir da nave e do alvo garante que nenhum dos dois é
	## encurtado: os dois estão NO plano por construção. O que se perde é a
	## inclinação relativa entre eles, que este mapa não promete mostrar.
	var to_ship := Vector3(data.get("ship_position", Vector3.ZERO)) - _centre
	if to_ship.length() < 1.0e-9:
		to_ship = Vector3.RIGHT
	var second := Vector3.ZERO
	if data.get("has_target", false):
		second = Vector3(data.get("target_position", Vector3.ZERO)) - _centre
	if second.length() < 1.0e-9:
		# Sem alvo: um ponto a um quarto de volta da trajetória que existir, que é
		# o que dá a normal da órbita sem ser quase paralelo ao primeiro.
		var source := track if track.size() > 8 else plan
		if source.size() >= 8:
			second = source[source.size() / 4] - _centre

	_u = to_ship.normalized()
	var w := _u.cross(second)
	if w.length() < 1.0e-12:
		# Nave e alvo alinhados com o centro: qualquer plano que os contenha
		# serve, e a escolha só roda o desenho.
		w = _u.cross(Vector3(0.0, 0.0, 1.0))
		if w.length() < 1.0e-12:
			w = _u.cross(Vector3(0.0, 1.0, 0.0))
	_v = w.normalized().cross(_u).normalized()


func _screen(p: Vector3) -> Vector2:
	var d := p - _centre
	return _origin + Vector2(d.dot(_u), -d.dot(_v)) / _extent * _box


func _polyline(points: PackedVector3Array, colour: Color, width_units: float) -> void:
	var w := maxf(unit() * width_units, 1.0)
	var previous := _screen(points[0])
	for i in range(1, points.size()):
		var current := _screen(points[i])
		# Segmentos fora da caixa são descartados um a um em vez de a linha
		# inteira: um arco translunar sai da tela e volta, e descartar o conjunto
		# apagaria a metade que interessa.
		if _near_screen(previous) or _near_screen(current):
			draw_line(previous, current, colour, w)
		previous = current


func _near_screen(at: Vector2) -> bool:
	var slack := size * 0.5
	return at.x > -slack.x and at.x < size.x + slack.x \
		and at.y > -slack.y and at.y < size.y + slack.y


func _draw_grid() -> void:
	## Anéis de escala, com o raio rotulado. Sem eles o mapa é bonito e não diz
	## distância nenhuma, que é a sua única função.
	var step := _nice_step(_extent * 0.45)
	var ring := step
	while ring <= _extent * 1.45:
		var radius := ring / _extent * _box
		draw_arc(_origin, radius, 0.0, TAU, 72, Palette.PANEL_EDGE.darkened(0.2),
			maxf(unit() * 0.14, 1.0))
		draw_text_at(_origin + Vector2(radius + unit() * 1.5, -unit() * 1.0),
			Fmt.distance(ring / data.get("render_scale", 1.0e-6)), 3.2, Palette.DIM)
		ring += step


func _nice_step(target: float) -> float:
	## 1, 2 ou 5 vezes uma potência de dez. Um passo "bonito" é o que faz o
	## rótulo do anel ser um número redondo, e um número redondo é o que se
	## consegue comparar de relance.
	var exponent := floorf(log(maxf(target, 1.0e-12)) / log(10.0))
	var base := pow(10.0, exponent)
	for multiple: float in [1.0, 2.0, 5.0, 10.0]:
		if base * multiple >= target:
			return base * multiple
	return base * 10.0


func _draw_body(at: Vector3, radius: float, name: String, colour: Color) -> void:
	var screen := _screen(at)
	var r := maxf(radius / _extent * _box, unit() * 1.4)
	draw_circle(screen, r, colour.darkened(0.6))
	draw_arc(screen, r, 0.0, TAU, 48, colour, maxf(unit() * 0.22, 1.0))
	draw_text_at(screen + Vector2(0.0, r + unit() * 4.5), name.to_upper(), 3.6,
		colour, HORIZONTAL_ALIGNMENT_CENTER)


func _draw_maneuvers() -> void:
	## Regra 55: cada queima com um marcador e um ETA. Os dados são os do plano;
	## o mapa não decide quando nada acende.
	var maneuvers: Array = data.get("maneuvers", [])
	for entry in maneuvers:
		var burn: Dictionary = entry
		if not burn.get("located", false):
			continue
		var at := _screen(burn["position"])
		var r := maxf(unit() * 1.6, 2.5)
		var done: bool = burn.get("done", false)
		var active: bool = burn.get("active", false)
		var colour := Palette.DIM if done else (Palette.CRITICAL if active else Palette.WARNING)
		draw_line(at + Vector2(-r * 2.0, 0.0), at + Vector2(r * 2.0, 0.0), colour,
			maxf(unit() * 0.26, 1.0))
		draw_line(at + Vector2(0.0, -r * 2.0), at + Vector2(0.0, r * 2.0), colour,
			maxf(unit() * 0.26, 1.0))
		draw_arc(at, r, 0.0, TAU, 20, colour, maxf(unit() * 0.26, 1.0))
		var label: String = String(burn.get("name", "BURN")).to_upper()
		draw_text_at(at + Vector2(r * 3.0, -unit() * 0.5), label, 3.6, colour)
		if not done:
			draw_text_at(at + Vector2(r * 3.0, unit() * 3.6),
				Fmt.countdown(burn.get("seconds_to_ignition", 0.0)), 3.6, colour)


func _draw_ship() -> void:
	var at := _screen(data.get("ship_position", Vector3.ZERO))
	var r := maxf(unit() * 1.8, 3.0)
	draw_circle(at, r, Palette.PRIMARY)
	draw_arc(at, r * 2.6, 0.0, TAU, 24, Palette.PRIMARY, maxf(unit() * 0.2, 1.0))


func _draw_legend() -> void:
	var u := unit()
	draw_text_at(Vector2(u * 4.0, u * 6.0), "ORBITAL MAP", 5.0, Palette.PRIMARY)
	draw_text_at(Vector2(u * 4.0, u * 12.0),
		"%s close   wheel zoom   zoom %.2fx" % [InputActions.label("orbit_map"), zoom],
		3.6, Palette.SECONDARY)

	var legend := [["current orbit", Palette.NAV], ["planned transfer", Palette.PLAN],
		["target path", Palette.TARGET.darkened(0.45)], ["burn", Palette.WARNING]]
	var y := size.y - u * 6.0
	for entry in legend:
		draw_line(Vector2(u * 4.0, y - u * 1.2), Vector2(u * 10.0, y - u * 1.2),
			entry[1], maxf(u * 0.3, 1.0))
		draw_text_at(Vector2(u * 11.5, y), entry[0], 3.6, Palette.SECONDARY)
		y -= u * 5.5
