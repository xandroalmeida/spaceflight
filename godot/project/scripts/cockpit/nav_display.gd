class_name NavDisplay
extends Instrument
## O mostrador de navegação (regra 20).
##
## Desenha a órbita, e os pontos da órbita vêm do core: `get_orbit_track()`
## amostra `trajectory::state_from_elements` sobre os elementos osculadores. Este
## arquivo NÃO resolve Kepler, não integra nada e não tem uma segunda opinião
## sobre a forma da elipse (regras 54 e 77). Ele projeta pontos num plano e
## desenha linhas entre eles.
##
## ## A projeção
##
## O plano do desenho é o plano da própria órbita, construído a partir de dois
## pontos da trajetória recebida. Vista de cima da órbita, portanto -- que é a
## vista em que apoapsis, periapsis e a posição da nave na volta são todas
## legíveis ao mesmo tempo. Uma projeção fixa (equatorial, digamos) esconderia a
## excentricidade de uma órbita inclinada atrás de um escorço, que é
## precisamente o número que se quer ler.

const MARGIN := 0.12


func _draw() -> void:
	title = "NAVIGATION"
	draw_frame()
	if data.is_empty():
		draw_text_at(size * 0.5, "NO DATA", 6.0, Palette.DIM, HORIZONTAL_ALIGNMENT_CENTER)
		return

	var track: PackedVector3Array = data.get("orbit_track", PackedVector3Array())
	var centre_3d: Vector3 = data.get("reference_position", Vector3.ZERO)
	var ship_3d: Vector3 = data.get("ship_position", Vector3.ZERO)

	if track.size() < 8:
		_draw_numbers_only()
		return

	var frame := _plane_from(track, centre_3d)
	var u: Vector3 = frame[0]
	var v: Vector3 = frame[1]

	# Escala: o ponto mais distante da órbita cabe na caixa, com margem.
	var extent := 0.0
	for point in track:
		extent = maxf(extent, (point - centre_3d).length())
	extent = maxf(extent, (ship_3d - centre_3d).length())
	if extent <= 0.0:
		_draw_numbers_only()
		return

	var box := minf(size.x, size.y * 1.35) * (0.5 - MARGIN)
	var origin := Vector2(size.x * 0.5, size.y * 0.47)
	var to_screen := func(p: Vector3) -> Vector2:
		var d := p - centre_3d
		return origin + Vector2(d.dot(u), -d.dot(v)) / extent * box

	_draw_central_body(origin, extent, box)

	# A órbita. Uma linha por segmento e não `draw_polyline`, porque uma
	# hipérbole vem com as pontas muito espaçadas e o antialiasing de uma
	# polilinha longa fica pior do que o de segmentos.
	var previous: Vector2 = to_screen.call(track[0])
	for i in range(1, track.size()):
		var current: Vector2 = to_screen.call(track[i])
		draw_line(previous, current, Palette.NAV, maxf(unit() * 0.30, 1.0))
		previous = current

	_draw_apsides(track, centre_3d, to_screen)
	_draw_ship(to_screen.call(ship_3d), origin)
	_draw_readouts()


func _plane_from(track: PackedVector3Array, centre: Vector3) -> Array:
	## Dois eixos ortonormais no plano da órbita, a partir dos pontos recebidos.
	##
	## O primeiro é o periapsis quando ele existe -- assim a elipse sai sempre na
	## mesma orientação e não roda sob os olhos do piloto a cada quadro, que é o
	## que um `u` tirado de "o primeiro ponto da amostra" faria numa órbita
	## reamostrada.
	var periapsis := track[0]
	var best := (track[0] - centre).length()
	for point in track:
		var distance := (point - centre).length()
		if distance < best:
			best = distance
			periapsis = point

	var u := (periapsis - centre).normalized()
	# A normal, do produto vetorial de dois raios bem separados: pontos vizinhos
	# são quase paralelos e o produto vetorial deles é ruído.
	var quarter: int = track.size() / 4
	var w := (track[0] - centre).cross(track[quarter] - centre)
	if w.length() < 1.0e-12:
		w = Vector3(0.0, 0.0, 1.0)
	w = w.normalized()
	var v := w.cross(u).normalized()
	return [u, v]


func _draw_central_body(origin: Vector2, extent: float, box: float) -> void:
	var radius: float = data.get("reference_radius", 0.0)
	var screen_radius := radius / extent * box
	var colour: Color = data.get("reference_colour", Palette.NAV_DIM)
	draw_circle(origin, maxf(screen_radius, unit() * 1.2), colour.darkened(0.55))
	draw_arc(origin, maxf(screen_radius, unit() * 1.2), 0.0, TAU, 48, colour,
		maxf(unit() * 0.28, 1.0))
	draw_text_at(origin + Vector2(0.0, screen_radius + unit() * 5.0),
		data.get("reference", "?"), 4.0, Palette.SECONDARY, HORIZONTAL_ALIGNMENT_CENTER)


func _draw_apsides(track: PackedVector3Array, centre: Vector3, to_screen: Callable) -> void:
	## Apoapsis e periapsis são os pontos mais e menos distantes DA AMOSTRA que o
	## core mandou. Não são recalculados: se a amostra tem 128 pontos o marcador
	## fica a menos de três graus do apsis verdadeiro, e o NÚMERO ao lado dele vem
	## do snapshot, que é exato. O marcador diz onde; o número diz quanto.
	var apo := track[0]
	var peri := track[0]
	var far := -1.0
	var near := INF
	for point in track:
		var distance := (point - centre).length()
		if distance > far:
			far = distance
			apo = point
		if distance < near:
			near = distance
			peri = point

	var bound: bool = data.get("bound", true)
	if bound:
		_apsis(to_screen.call(apo), "AP", Fmt.distance(data.get("apoapsis_altitude_m", 0.0)))
	_apsis(to_screen.call(peri), "PE", Fmt.distance(data.get("periapsis_altitude_m", 0.0)))


func _apsis(at: Vector2, label: String, value: String) -> void:
	var r := maxf(unit() * 1.6, 2.0)
	draw_circle(at, r, Palette.PLAN)
	draw_text_at(at + Vector2(r * 2.0, -r), label, 3.8, Palette.PLAN)
	draw_text_at(at + Vector2(r * 2.0, r * 2.0 + unit() * 3.4), value, 3.8, Palette.SECONDARY)


func _draw_ship(at: Vector2, origin: Vector2) -> void:
	var r := maxf(unit() * 2.0, 3.0)
	draw_circle(at, r, Palette.PRIMARY)
	# O sentido da marcha: a seta aponta ao longo da velocidade projetada, e a
	# velocidade projetada é perpendicular ao raio no sentido do movimento. Aqui
	# ela é tirada da geometria do desenho -- a tangente -- e não de um vetor
	# convertido, porque é o sentido NO DESENHO que tem de estar certo.
	var radial := (at - origin).normalized()
	var tangent := Vector2(-radial.y, radial.x)
	if data.get("retrograde_orbit", false):
		tangent = -tangent
	draw_line(at, at + tangent * r * 4.0, Palette.PRIMARY, maxf(unit() * 0.3, 1.0))
	var head := at + tangent * r * 4.0
	draw_line(head, head - tangent * r * 1.6 + radial * r * 1.0, Palette.PRIMARY,
		maxf(unit() * 0.3, 1.0))
	draw_line(head, head - tangent * r * 1.6 - radial * r * 1.0, Palette.PRIMARY,
		maxf(unit() * 0.3, 1.0))


func _draw_readouts() -> void:
	var left := unit() * 2.5
	var bottom := size.y - unit() * 2.0
	draw_text_at(Vector2(left, bottom - unit() * 10.0), "ALT", 3.6, Palette.SECONDARY)
	draw_text_at(Vector2(left, bottom - unit() * 5.0),
		Fmt.distance(data.get("altitude_m", 0.0)), 5.0, Palette.PRIMARY)

	draw_text_at(Vector2(size.x - left, bottom - unit() * 10.0), "ECC / INC", 3.6,
		Palette.SECONDARY, HORIZONTAL_ALIGNMENT_RIGHT)
	draw_text_at(Vector2(size.x - left, bottom - unit() * 5.0),
		"%.4f / %.2f°" % [data.get("eccentricity", 0.0), data.get("inclination_deg", 0.0)],
		5.0, Palette.PRIMARY, HORIZONTAL_ALIGNMENT_RIGHT)

	var period: float = data.get("period_s", 0.0)
	if period > 0.0:
		draw_text_at(Vector2(size.x * 0.5, bottom - unit() * 5.0),
			"T %s" % Fmt.duration(period), 4.2, Palette.SECONDARY,
			HORIZONTAL_ALIGNMENT_CENTER)


func _draw_numbers_only() -> void:
	## Sem trajetória não há desenho, mas há números -- e a alternativa, uma caixa
	## vazia, esconderia que a nave continua em algum lugar.
	draw_field(Vector2(unit() * 3.0, unit() * 18.0), "ALTITUDE",
		Fmt.distance(data.get("altitude_m", 0.0)), 7.0)
	draw_field(Vector2(unit() * 3.0, unit() * 38.0), "APOAPSIS",
		Fmt.distance(data.get("apoapsis_altitude_m", 0.0)), 7.0)
	draw_field(Vector2(unit() * 3.0, unit() * 58.0), "PERIAPSIS",
		Fmt.distance(data.get("periapsis_altitude_m", 0.0)), 7.0)
	draw_field(Vector2(unit() * 3.0, unit() * 78.0), "ECCENTRICITY",
		"%.5f" % data.get("eccentricity", 0.0), 7.0)
