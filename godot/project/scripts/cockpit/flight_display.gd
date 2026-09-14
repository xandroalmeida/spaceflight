class_name FlightDisplay
extends Instrument
## O mostrador primário de voo (regra 18).
##
## NÃO tem horizonte artificial, e a razão está na regra 18: um horizonte é a
## linha onde o chão encontra o céu, e em órbita não existe nem um nem outro. O
## que existe é para onde o nariz aponta e onde estão as direções que importam,
## e é isso que este mostrador desenha.
##
## ## A projeção
##
## O centro é o NARIZ. Cada direção é levada ao referencial do corpo, e o ângulo
## entre ela e o nariz vira um raio: projeção azimutal equidistante, em que a
## distância ao centro É o erro de apontamento em graus. Um marcador a meio raio
## está a metade do ângulo máximo, sem interpretação.
##
## Direita na tela é -y do corpo e cima é +z, que é exatamente como a câmera do
## cockpit está montada (`CameraRig.COCKPIT_ALIGN`). Os dois têm de concordar: um
## marcador que aparece à direita no mostrador e à esquerda pela janela é pior
## que nenhum marcador.
##
## Direções para trás -- mais de 90 graus do nariz -- são desenhadas na borda,
## esmaecidas, em vez de descartadas. "Está atrás de você" é informação.

const FIELD_OF_VIEW_DEG := 90.0
const MARKERS := [
	["prograde", "prograde", Palette.PROGRADE],
	["retrograde", "retrograde", Palette.PROGRADE],
	["normal", "normal", Palette.NAV],
	["anti_normal", "anti_normal", Palette.NAV],
	["radial_out", "radial_out", Palette.NAV_DIM],
	["radial_in", "radial_in", Palette.NAV_DIM],
	["target", "target", Palette.TARGET],
	["anti_target", "anti_target", Palette.TARGET],
]


func _draw() -> void:
	title = "FLIGHT"
	draw_frame()
	if data.is_empty():
		draw_text_at(size * 0.5, "NO DATA", 6.0, Palette.DIM, HORIZONTAL_ALIGNMENT_CENTER)
		return

	var centre := Vector2(size.x * 0.5, size.y * 0.52)
	var radius := minf(size.x, size.y) * 0.40

	_draw_rose(centre, radius)
	_draw_markers(centre, radius)
	_draw_nose(centre, radius)
	_draw_rates()


func _draw_rose(centre: Vector2, radius: float) -> void:
	## Três anéis, a 30, 60 e 90 graus do nariz, rotulados. São a escala do
	## mostrador: sem eles um marcador a meio raio não quer dizer nada.
	for ring in [1.0 / 3.0, 2.0 / 3.0, 1.0]:
		var colour := Palette.DIM if ring < 1.0 else Palette.SECONDARY
		draw_arc(centre, radius * ring, 0.0, TAU, 64, colour, maxf(unit() * 0.22, 1.0))
	for degrees in [30, 60]:
		draw_text_at(centre + Vector2(radius * float(degrees) / FIELD_OF_VIEW_DEG + unit(), -unit()),
			"%d°" % degrees, 3.4, Palette.DIM)
	# Cruz de eixos: a referência de rolagem. É a única coisa neste mostrador que
	# fala do eixo longitudinal da nave.
	draw_tick(centre - Vector2(radius, 0.0), centre + Vector2(radius, 0.0), Palette.DIM, 0.18)
	draw_tick(centre - Vector2(0.0, radius), centre + Vector2(0.0, radius), Palette.DIM, 0.18)


func _draw_markers(centre: Vector2, radius: float) -> void:
	var directions: Dictionary = data.get("directions", {})
	var basis: Basis = data.get("ship_basis", Basis.IDENTITY)
	var marker_radius := maxf(unit() * 2.4, 3.0)

	for entry in MARKERS:
		var key: String = entry[0]
		if not directions.has(key):
			continue
		var placed := _project(directions[key], basis, centre, radius)
		var at: Vector2 = placed[0]
		var behind: bool = placed[1]
		var colour: Color = entry[2]
		if behind:
			colour = Palette.faded(colour, 0.62)
		draw_marker(at, marker_radius, colour, entry[1])


func _draw_nose(centre: Vector2, radius: float) -> void:
	## O nariz está SEMPRE no centro, porque o centro é o nariz. O que se desenha
	## aqui é o símbolo da nave e, quando há um comando de apontamento, a linha
	## que liga o nariz ao alvo dele -- o "diretor de voo" da regra 18: siga a
	## linha e o erro fecha.
	draw_marker(centre, maxf(unit() * 3.0, 4.0), Palette.PRIMARY, "nose")

	var mode: String = data.get("pointing_mode", "HOLD")
	if mode == "HOLD" or mode == "":
		return
	var directions: Dictionary = data.get("directions", {})
	var key := mode.to_lower()
	if not directions.has(key):
		return
	var basis: Basis = data.get("ship_basis", Basis.IDENTITY)
	var placed := _project(directions[key], basis, centre, radius)
	draw_line(centre, placed[0], Palette.WARNING, maxf(unit() * 0.28, 1.0))
	var error: float = data.get("pointing_error_deg", 0.0)
	draw_text_at(Vector2(size.x - unit() * 2.5, size.y - unit() * 4.5),
		"%s  %.2f°" % [mode, error],
		4.4, Palette.WARNING if error > 2.0 else Palette.OK, HORIZONTAL_ALIGNMENT_RIGHT)


func _draw_rates() -> void:
	var rate: float = data.get("rotation_rate_deg_s", 0.0)
	draw_text_at(Vector2(unit() * 2.5, size.y - unit() * 4.5),
		"SPIN %.3f °/s" % rate, 4.4,
		Palette.WARNING if rate > 2.0 else Palette.SECONDARY)
	# Velocidade, no topo: é o número que se lê mais vezes que qualquer outro.
	draw_text_at(Vector2(size.x - unit() * 2.5, unit() * 6.0),
		Fmt.speed(data.get("speed_ms", 0.0)), 5.6, Palette.PRIMARY,
		HORIZONTAL_ALIGNMENT_RIGHT)


func _project(direction: Vector3, basis: Basis, centre: Vector2, radius: float) -> Array:
	## Integração -> corpo -> tela. `basis` tem as colunas x (nariz), y, z.
	var body := Vector3(basis.x.dot(direction), basis.y.dot(direction), basis.z.dot(direction))
	var along := clampf(body.x, -1.0, 1.0)
	var angle := rad_to_deg(acos(along))
	var lateral := Vector2(-body.y, -body.z)   # tela: direita = -y, cima = +z
	if lateral.length() < 1.0e-6:
		# Exatamente no nariz (ou exatamente atrás): o azimute é indefinido e
		# qualquer escolha serve, porque o raio é zero ou máximo.
		lateral = Vector2(0.0, -1.0)
	else:
		lateral = lateral.normalized()
	var behind := angle > FIELD_OF_VIEW_DEG
	var r := radius * minf(angle / FIELD_OF_VIEW_DEG, 1.0)
	return [centre + lateral * r, behind]
