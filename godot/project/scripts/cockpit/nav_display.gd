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

## O epsilon do float de 32 bits, o ganho de ruído do estimador do apsis, e
## quanto o marcador pode mexer-se entre quadros sem que isso se note.
##
## ⚠️ Os pontos da órbita chegam aqui em UNIDADES DE CENA e em float de 32 bits.
## Numa órbita quase circular a diferença entre o raio no apoapsis e no periapsis
## é da ordem do ulp desse float, e a direção do apsis deixa de estar nos dados.
##
## `APSIS_NOISE_GAIN` é MEDIDO, não deduzido: o erro angular do estimador cai com
## 1/separação, e sobre uma órbita de estacionamento a 400 km (ulp 0,8 m) o
## produto (erro × separação) fica entre 224 e 320 graus·metro em toda a faixa de
## 7 m a 9 km de separação. Em radianos e por ulp, isso é 6,5. A conta fecha:
##
##     erro ≈ 6,5 × ulp / separação
##
## Um grau é o limite porque é o que se vê: neste mostrador o anel tem 110 px de
## raio, e um grau são dois píxeis. Abaixo disso o marcador está parado para quem
## olha, e acima disso ele treme.
const FLOAT32_EPSILON := 1.1920929e-7
const APSIS_NOISE_GAIN := 6.5
const APSIS_STABILITY_DEG := 1.0


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

	# Escala: o ponto mais distante da órbita cabe na caixa, com margem.
	var extent := 0.0
	for point in track:
		extent = maxf(extent, (point - centre_3d).length())
	extent = maxf(extent, (ship_3d - centre_3d).length())
	if extent <= 0.0:
		_draw_numbers_only()
		return

	var bound: bool = data.get("bound", true)
	var resolved := apsides_resolved(
		absf(data.get("apoapsis_altitude_m", 0.0) - data.get("periapsis_altitude_m", 0.0)),
		extent, data.get("render_scale", 1.0))

	var frame := _plane_from(track, centre_3d, resolved, bound)
	var u: Vector3 = frame[0]
	var v: Vector3 = frame[1]

	# O raio do desenho tem de caber nas DUAS direções.
	#
	# ⚠️ A conta anterior era `min(largura, altura × 1,35) × 0,38`, que num painel
	# de 480×315 dá 161 px de raio com 148 px de altura disponíveis acima do
	# centro: a órbita saía pelo topo e pelo fundo do mostrador -- dá para ver o
	# anel cortado nas capturas -- e o marcador da nave DESAPARECIA durante todo o
	# trecho da volta em que ela passava por lá. Num mostrador de navegação, "onde
	# está a nave" é a pergunta.
	var origin := Vector2(size.x * 0.5, size.y * 0.47)
	var half := minf(size.x * 0.5, minf(origin.y, size.y - origin.y))
	var box := half * (1.0 - MARGIN * 2.0)
	var to_screen := func(p: Vector3) -> Vector2:
		var d := p - centre_3d
		return origin + Vector2(d.dot(u), -d.dot(v)) / extent * box

	_draw_central_body(origin, extent, box)
	_draw_reference_banner()

	# A órbita. Uma linha por segmento e não `draw_polyline`, porque uma
	# hipérbole vem com as pontas muito espaçadas e o antialiasing de uma
	# polilinha longa fica pior do que o de segmentos.
	var previous: Vector2 = to_screen.call(track[0])
	for i in range(1, track.size()):
		var current: Vector2 = to_screen.call(track[i])
		draw_line(previous, current, Palette.NAV, maxf(unit() * 0.30, 1.0))
		previous = current

	_draw_apsides(origin, extent, box, resolved, bound)
	_draw_ship(to_screen.call(ship_3d), origin)
	_draw_readouts()


## A separação dos apsides é maior do que aquilo que o desenho consegue
## distinguir?
##
## `spread_m` é (apoapsis − periapsis) em metros, do snapshot, que é exato.
## `extent_scene` é o raio da órbita em unidades de cena, que é onde o float de
## 32 bits está. A conta pergunta quanto o marcador tremeria e compara isso com o
## que se vê, e é a única coisa que decide se este mostrador tem direito a
## apontar para um apsis.
##
## Escala-livre de propósito: ela não pergunta "a excentricidade é pequena?", que
## dependeria do corpo, mas "o número que eu quero desenhar sobrevive à precisão
## com que ele me chegou?", que vale igual na Terra, na Lua e à volta do Sol.
static func apsides_resolved(spread_m: float, extent_scene: float,
		render_scale: float) -> bool:
	if render_scale <= 0.0 or extent_scene <= 0.0:
		return false
	var ulp_m := extent_scene * FLOAT32_EPSILON / render_scale
	return spread_m > APSIS_NOISE_GAIN * ulp_m / deg_to_rad(APSIS_STABILITY_DEG)


func _plane_from(track: PackedVector3Array, centre: Vector3, resolved: bool,
		bound: bool = true) -> Array:
	## Dois eixos ortonormais no plano da órbita, a partir dos pontos recebidos.
	##
	## A normal vem primeiro, do produto vetorial de dois raios bem separados:
	## pontos vizinhos são quase paralelos e o produto vetorial deles é ruído.
	var quarter: int = track.size() / 4
	var w := (track[0] - centre).cross(track[quarter] - centre)
	if w.length() < 1.0e-12:
		w = Vector3(0.0, 0.0, 1.0)
	w = w.normalized()

	var u := Vector3.ZERO
	if resolved and bound:
		# O periapsis, de TODAS as amostras: assim a elipse sai sempre na mesma
		# orientação e não roda sob os olhos do piloto a cada quadro, que é o que
		# um `u` tirado de "o primeiro ponto da amostra" faria numa órbita
		# reamostrada.
		u = _periapsis_from_centroid(track, centre)
	elif resolved:
		# Hipérbole: a média não serve, porque o arco é um pedaço e não uma volta.
		# Mas aqui não faz falta -- o periapsis de uma hipérbole é um bico, o raio
		# à volta dele varia depressa, e o extremo da amostra acerta-o.
		var best := 0
		var near := INF
		for i in range(track.size()):
			var distance := (track[i] - centre).length()
			if distance < near:
				near = distance
				best = i
		u = (_refined_apsis(track, centre, best) - centre).normalized()
	else:
		# Numa órbita circular não HÁ periapsis, e tirar `u` do ponto mais próximo
		# da amostra é tirá-lo do arredondamento: o desenho inteiro rodava até
		# 37 graus por quadro. Um círculo a rodar não se nota -- mas o marcador da
		# nave em cima dele saltava com ele.
		#
		# Um eixo FIXO do mundo, projetado no plano da órbita. Qual deles é
		# indiferente (o desenho é um círculo, não tem orientação para acertar);
		# o que importa é que seja o MESMO no quadro seguinte.
		var axes: Array[Vector3] = [Vector3(0.0, 0.0, 1.0), Vector3(1.0, 0.0, 0.0)]
		for axis in axes:
			var projected := axis - w * axis.dot(w)
			if projected.length() > 1.0e-3:
				u = projected.normalized()
				break

	var v := w.cross(u).normalized()
	return [u, v]


## A direção do periapsis, de TODAS as amostras e não das duas mais próximas.
##
## `get_orbit_track` amostra uniformemente em ANOMALIA VERDADEIRA, e para essa
## amostragem a média das posições cai em −p·e/2 ao longo da direção do
## periapsis: proporcional à excentricidade, e -- por ser uma média de N pontos
## -- com o ruído de cada amostra dividido pela raiz de N.
##
## ⚠️ Procurar o ponto MAIS PRÓXIMO era o contrário disto. Perto de um apsis o
## raio é estacionário, ou seja, a grandeza que distingue o apsis dos vizinhos é
## a menor da curva inteira -- o pior sítio possível para ir buscar um extremo
## num sinal com ruído. Medido, entre quadros, numa órbita a 400 km com 7 a 19 km
## de separação entre apsides: o extremo saltava 0,24° a 1,75°; a média, 0,017° a
## 0,033°. Cinquenta a cem vezes menos, e o que sobra já é a precessão a sério.
func _periapsis_from_centroid(track: PackedVector3Array, centre: Vector3) -> Vector3:
	# O último ponto REPETE o primeiro (ν = −π e ν = +π são o mesmo sítio da
	# órbita): contá-lo duas vezes põe um apoapsis a mais na média.
	var count := maxi(track.size() - 1, 1)
	var sum := Vector3.ZERO
	for i in range(count):
		sum += track[i] - centre
	var offset := sum / float(count)
	if offset.length() < 1.0e-12:
		return (track[0] - centre).normalized()
	# A média cai do lado do APOAPSIS; o periapsis é do outro.
	return -offset.normalized()


## O ponto do apsis, ENTRE as amostras e não em cima de uma delas. Só para a
## hipérbole: a órbita fechada usa a média, que é melhor.
##
## ⚠️ `get_orbit_track` amostra uniformemente em ANOMALIA VERDADEIRA de −π a +π,
## e o periapsis está em ν = 0 -- que com 128 amostras cai no índice 63,5, ou
## seja, EXATAMENTE entre duas. Qual das duas ganha o `<` é decidido pelo
## arredondamento, e o eixo do desenho saltava 2,83 graus (um espaçamento de
## amostra) de um quadro para o outro: numa elipse de transferência isso é a
## órbita inteira a balançar.
##
## O raio perto de um apsis é uma parábola em função do índice, então o vértice
## sai de três pontos em fórmula fechada. Com o mínimo a meio caminho, os dois
## índices candidatos dão o MESMO vértice -- 63,5 -- e o salto desaparece por
## construção, em vez de ficar dependente de qual deles a comparação escolheu.
func _refined_apsis(track: PackedVector3Array, centre: Vector3, at: int) -> Vector3:
	if at <= 0 or at >= track.size() - 1:
		return track[at]
	var before := (track[at - 1] - centre).length()
	var middle := (track[at] - centre).length()
	var after := (track[at + 1] - centre).length()
	var curvature := before - 2.0 * middle + after
	if absf(curvature) < 1.0e-12:
		return track[at]
	var offset := clampf(0.5 * (before - after) / curvature, -1.0, 1.0)
	var neighbour := track[at + 1] if offset >= 0.0 else track[at - 1]
	# Interpolação linear entre as duas amostras: perto de um apsis o raio é
	# estacionário, então a corda e o arco diferem por menos do que um pixel deste
	# mostrador, e o marcador fica em cima da linha que já foi desenhada -- que é
	# o que um marcador de apsis tem de fazer.
	return track[at].lerp(neighbour, absf(offset))


func _draw_central_body(origin: Vector2, extent: float, box: float) -> void:
	var radius: float = data.get("reference_radius", 0.0)
	var screen_radius := radius / extent * box
	var colour: Color = data.get("reference_colour", Palette.NAV_DIM)
	draw_circle(origin, maxf(screen_radius, unit() * 1.2), colour.darkened(0.55))
	draw_arc(origin, maxf(screen_radius, unit() * 1.2), 0.0, TAU, 48, colour,
		maxf(unit() * 0.28, 1.0))
	# Sem rótulo por baixo do planeta: `_draw_reference_banner` já o diz, em cima e
	# à esquerda, e diz melhor. Aqui ele era uma segunda cópia da mesma palavra a
	# meio do caminho da órbita -- e desde que o desenho passou a caber no painel,
	# uma cópia por cima do marcador da nave.


func _draw_reference_banner() -> void:
	## Regra 64: dizer, sem ambiguidade, contra QUE corpo os números são lidos.
	##
	## Num voo Terra-Lua a resposta nunca muda e o rótulo sob o planeta bastava.
	## Num cruzeiro interplanetário ela muda três vezes -- Terra, Sol, Marte -- e
	## "AP 402 km" sem o corpo ao lado não é um número, é um número e um palpite.
	var u := unit()
	var reference := String(data.get("reference", "?")).to_upper()
	draw_text_at(Vector2(u * 3.0, u * 12.0), "REFERENCE", 3.4, Palette.DIM)
	draw_text_at(Vector2(u * 3.0, u * 17.5), reference, 5.0,
		Palette.WARNING if reference == "SUN" else Palette.SECONDARY)


func _draw_apsides(origin: Vector2, extent: float, box: float, resolved: bool,
		bound: bool) -> void:
	## Os apsides estão na LINHA DOS APSIDES, que é o eixo `u` do desenho, e a que
	## distância eles estão vem do snapshot, em dupla precisão. Então não há nada
	## para procurar: o periapsis fica a +r_pe de `u` e o apoapsis a −r_ap.
	##
	## ⚠️ A versão anterior ia buscar os dois à amostra, pelo ponto mais e menos
	## distante. O periapsis saía sempre no sítio certo por acidente (é ele que
	## define `u`), mas o APOAPSIS vinha de uma segunda busca independente, e como
	## perto de um apsis o raio é estacionário essa busca é ruído: o ponto amarelo
	## do "AP" deslizava alguns graus pelo anel acima e abaixo enquanto o "PE" do
	## outro lado estava quieto.
	##
	## Quando os dois não se separam o suficiente para que a direção seja
	## conhecível, não há "onde": a órbita é circular e o mostrador diz isso em
	## vez de apontar para um sítio ao acaso. Uma hipérbole escapa à regra porque
	## o periapsis dela é sempre nítido -- o que ela não tem é apoapsis, e disso já
	## trata `bound`.
	if bound and not resolved:
		_draw_circular_note()
		return

	var scale: float = data.get("render_scale", 1.0)
	if bound:
		var apoapsis := float(data.get("apoapsis_m", 0.0)) * scale
		_apsis(origin + Vector2(-apoapsis / extent * box, 0.0), "AP",
			Fmt.distance(data.get("apoapsis_altitude_m", 0.0)))
	var periapsis := float(data.get("periapsis_m", 0.0)) * scale
	_apsis(origin + Vector2(periapsis / extent * box, 0.0), "PE",
		Fmt.distance(data.get("periapsis_altitude_m", 0.0)))


func _draw_circular_note() -> void:
	## A resposta que substitui os dois marcadores, na COR deles: quem procurava o
	## apsis encontra ali por que é que ele não está lá. O valor é a altitude, que
	## numa órbita circular é a mesma em toda a volta -- e continua no seu lugar,
	## em baixo, para quem não olhou para aqui.
	var u := unit()
	draw_text_at(Vector2(u * 3.0, u * 27.0), "CIRCULAR", 4.6, Palette.PLAN)
	draw_text_at(Vector2(u * 3.0, u * 33.0),
		Fmt.distance(data.get("altitude_m", 0.0)), 4.2, Palette.SECONDARY)


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
