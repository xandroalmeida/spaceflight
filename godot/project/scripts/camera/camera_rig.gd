class_name CameraRig
extends Node3D
## As câmeras, e as duas escalas (regras 10, 11, 12).
##
## ## Por que há DUAS câmeras
##
## A cena tem 1 unidade = 1e6 metros. A Terra tem 6,371 unidades de raio e a
## Lua fica a 358. O plano próximo da câmera do mundo é 0,05 unidades -- 50 km --
## e tem de ser, porque com profundidade invertida de 24 bits um plano próximo
## menor que `SKY_RADIUS * 2^-24` perde as estrelas por quantização
## (`docs/validation/starfield-debug.md` seção 6).
##
## A nave tem 22 metros. Isso é 2,2e-5 unidades: duas mil vezes DENTRO do plano
## próximo. Não existe uma câmera só que desenhe a Lua a 358 unidades e um
## painel a meio metro do rosto.
##
## Então são duas, com a MESMA orientação e o MESMO campo de visão:
##
##   world   as unidades de cena. Planetas, estrelas, órbitas. near 0,05, far 2e5.
##   near    METROS, origem no centro de massa da nave. Casco, cockpit, pluma,
##           jatos. near 0,05 m, far 8 km, fundo transparente, composta por cima.
##
## A conversão entre elas é uma multiplicação por `RENDER_SCALE` e está escrita
## em um lugar só, em `_commit`. A câmera próxima não "segue" a do mundo: as duas
## são a MESMA câmera expressa em duas unidades, e é por isso que a paralaxe
## entre a nave e o planeta atrás dela sai certa em vez de ser ajustada.
##
## O que isto custa, dito por inteiro: a camada próxima não é ocluída pela
## distante. Um planeta nunca passa à frente do casco. Para uma câmera que ou
## está dentro da nave ou a algumas dezenas de metros dela, isso não acontece --
## e está registrado como VISUAL_DEBT em vez de ser resolvido com um terceiro
## viewport.

enum Mode { COCKPIT, EXTERNAL_ORBIT, CHASE, VELOCITY_REFERENCE, TARGET_REFERENCE }

const MODE_NAMES := ["COCKPIT", "EXTERNAL", "CHASE", "VELOCITY", "TARGET"]

## Onde fica a cabeça do piloto, no referencial do corpo, em metros (regra 10).
## Dentro da carenagem do cockpit e atrás das janelas frontais, que começam em
## x = 4,0. `CockpitInterior` constrói os painéis em torno deste ponto.
const EYE := Vector3(3.15, 0.0, 0.36)

const WORLD_NEAR := 0.05
const WORLD_FAR := 2.0e5
const NEAR_NEAR := 0.05        ## [m]
const NEAR_FAR := 8000.0       ## [m]
## 75 graus VERTICAIS. O padrão do Godot mantém a altura, então este é o ângulo
## de cima a baixo, e ele decide o que cabe entre a janela e o painel: a 68 graus
## os três mostradores ficavam fora do quadro e o cockpit lia-se como uma caixa
## vazia com estrelas ao fundo. Medido, não estimado -- as posições do painel em
## `cockpit_interior.gd` estão a 23, 28 e 43 graus abaixo da linha de visada.
const FOV := 75.0

## Para onde a cabeça olha em repouso: dez graus abaixo da linha do nariz.
##
## É o que um piloto faz. Se fosse zero, a janela ficaria centrada e o painel
## inteiro ficaria fora do quadro -- e `recentre` devolveria a câmera a uma pose
## em que metade do cockpit não existe. O marcador do nariz no mostrador de voo
## continua a marcar o NARIZ; o que está inclinado é a cabeça, não a nave.
const COCKPIT_REST_PITCH := -0.1920      ## 11 graus

## Limites de cabeça. São limites de ENTRADA sobre um ângulo de câmera, não
## afirmações sobre o mundo (regra 13 do M2): passados de 90 graus o vetor "para
## cima" inverte e o controle inverte nas mãos do piloto.
const LOOK_YAW_LIMIT := 2.44      ## 140 graus: dá para olhar os painéis laterais
const LOOK_PITCH_LIMIT := 1.28    ## 73 graus
const ORBIT_ELEVATION_LIMIT := 1.5533   ## 89 graus

const ZOOM_STEP := 1.15
const ZOOM_MIN := 0.25
const ZOOM_MAX := 90.0

## Distância natural da câmera externa: o raio que contém a nave, vezes três.
## Derivada da geometria e não escolhida à mão, para que mudar a nave mude o
## enquadramento junto.
const EXTERNAL_NATURAL_M := 40.0

var world_camera: Camera3D
var near_camera: Camera3D
var mode: int = Mode.COCKPIT

var mouse_sensitivity := 1.0
var invert_look := false

var orbit_azimuth := PI
var orbit_elevation := 0.35
var orbit_zoom := 1.0
var look_yaw := 0.0
var look_pitch := 0.0

## Foco num corpo, para a vista técnica herdada do M5. -1 = a nave.
var focus_index := -1

var _render_scale := 1.0e-6
var _ship_position := Vector3.ZERO
var _ship_basis := Basis.IDENTITY
var _focus_position := Vector3.ZERO
var _focus_natural := 1.0
var _dragging := false
var _mouse_look := false

## Alinhamento do olhar do piloto com o casco.
##
## A câmera do Godot olha ao longo de -Z; o nariz da nave é +X e o "cima" dela é
## +Z. Então, nas coordenadas do corpo: cam_Z = -x̂, cam_Y = +ẑ, e
## cam_X = cam_Y × cam_Z = -ŷ. Escrito aqui uma vez, porque errar isto dá um
## cockpit que olha para o lado e ainda assim parece plausível.
const COCKPIT_ALIGN := Basis(Vector3(0.0, -1.0, 0.0), Vector3(0.0, 0.0, 1.0),
	Vector3(-1.0, 0.0, 0.0))

## Onde cada modo põe a câmera quando entra: (azimute, elevação).
const MODE_PRESET := {
	Mode.EXTERNAL_ORBIT: [PI, 0.35],
	Mode.CHASE: [PI, 0.14],
	Mode.VELOCITY_REFERENCE: [PI, 0.0],
	Mode.TARGET_REFERENCE: [PI, 0.18],
}


func build(render_scale: float, near_viewport: SubViewport) -> void:
	_render_scale = render_scale

	world_camera = Camera3D.new()
	world_camera.near = WORLD_NEAR
	world_camera.far = WORLD_FAR
	world_camera.fov = FOV
	world_camera.current = true
	add_child(world_camera)

	near_camera = Camera3D.new()
	near_camera.near = NEAR_NEAR
	near_camera.far = NEAR_FAR
	near_camera.fov = FOV
	near_camera.current = true
	near_viewport.add_child(near_camera)


func set_mode(new_mode: int) -> void:
	if new_mode == mode:
		return
	mode = new_mode
	look_yaw = 0.0
	look_pitch = 0.0
	if MODE_PRESET.has(mode):
		orbit_azimuth = MODE_PRESET[mode][0]
		orbit_elevation = MODE_PRESET[mode][1]


func cycle_mode() -> void:
	set_mode((mode + 1) % MODE_NAMES.size())


func mode_name() -> String:
	return MODE_NAMES[mode]


func is_cockpit() -> bool:
	return mode == Mode.COCKPIT


## Chamado uma vez por quadro, depois de a simulação avançar.
##
## `beta` é a velocidade baricêntrica sobre c e é o eixo em torno do qual o céu
## é aberrado -- não o prógrado. A distinção custou uma leitura mentirosa no M5
## e está preservada: em órbita baixa os dois apontam a uns 30 graus de
## distância.
func update(ship_position: Vector3, ship_basis: Basis, beta: Vector3,
		target_direction: Vector3, focus_position: Vector3, focus_natural: float) -> void:
	_ship_position = ship_position
	_ship_basis = ship_basis
	_focus_position = focus_position
	_focus_natural = focus_natural

	if focus_index >= 0:
		_place_orbiting_body()
	elif mode == Mode.COCKPIT:
		_place_cockpit()
	else:
		_place_external(beta, target_direction)


func _place_cockpit() -> void:
	# A cabeça está fixa no casco; o que se move é para onde ela olha. A atitude
	# da nave continua sendo a do core -- olhar em volta NÃO é manobra e não
	# muda um número sequer do estado.
	var basis := _ship_basis * COCKPIT_ALIGN
	basis = basis.rotated(basis.y, look_yaw)
	basis = basis.rotated(basis.x, look_pitch + COCKPIT_REST_PITCH)
	_commit(_ship_basis * EYE, basis.orthonormalized())


func _place_external(beta: Vector3, target_direction: Vector3) -> void:
	var frame := _orbit_frame(beta, target_direction)
	var forward: Vector3 = frame[0]
	var up: Vector3 = frame[1]
	var right := forward.cross(up)
	if right.length() < 1.0e-6:
		right = forward.cross(Vector3.UP)
		if right.length() < 1.0e-6:
			right = forward.cross(Vector3.RIGHT)
	right = right.normalized()
	up = right.cross(forward).normalized()

	var ce := cos(orbit_elevation)
	var se := sin(orbit_elevation)
	var ca := cos(orbit_azimuth)
	var sa := sin(orbit_azimuth)
	var offset := forward * (ce * ca) + right * (ce * sa) + up * se
	# A tangente do meridiano: a derivada de `offset` em relação à elevação. É
	# unitária e EXATAMENTE perpendicular a `offset` para todo par (azimute,
	# elevação) -- offset·meridiano = -ce·se + se·ce = 0, algebricamente. É o que
	# a torna um vetor "para cima" seguro em qualquer lugar, polos incluídos, e é
	# por isso que o guard que trocava o vetor "para cima" sumiu em vez de ser
	# reajustado: o guard ERA o salto de 88 graus num quadro que esta câmera
	# tinha (`godot/README.md`).
	var meridian := forward * (-se * ca) + right * (-se * sa) + up * ce

	var distance := maxf(EXTERNAL_NATURAL_M * orbit_zoom, NEAR_NEAR * 4.0)
	var eye := offset * distance
	_commit(eye, _aim(-offset, meridian))


func _place_orbiting_body() -> void:
	## A vista técnica do M5: orbitar um corpo, em unidades de cena. Aqui a
	## câmera próxima fica onde estaria -- a milhões de metros da nave -- e a
	## nave simplesmente cai para fora do seu plano distante. Nenhum caso
	## especial: as duas câmeras continuam sendo a mesma câmera.
	##
	## ⚠️ `forward` é a direção do CORPO PARA A NAVE, e não um eixo fixo. A
	## primeira versão usava `Vector3.FORWARD` com o polo J2000 como "cima" --
	## e (0,0,−1) × (0,0,1) é o VETOR NULO. Normalizado dá zero, o deslocamento
	## dá zero, e a câmera ficava exatamente no centro do planeta: tela preta,
	## sem erro nenhum. Apanhado por uma captura do planeta inteiro.
	##
	## Apontar para a nave também é a escolha útil: azimute 180 põe a câmera do
	## lado da nave, que é de onde se quer ver o corpo que se está a orbitar.
	var forward := _ship_position - _focus_position
	if forward.length() < 1.0e-9:
		forward = Vector3.RIGHT
	forward = forward.normalized()
	var up := _stable_up(forward)
	var right := forward.cross(up)
	if right.length() < 1.0e-6:
		right = forward.cross(Vector3.UP)
		if right.length() < 1.0e-6:
			right = forward.cross(Vector3.RIGHT)
	right = right.normalized()
	up = right.cross(forward).normalized()

	var ce := cos(orbit_elevation)
	var se := sin(orbit_elevation)
	var offset := forward * (ce * cos(orbit_azimuth)) + right * (ce * sin(orbit_azimuth)) + up * se
	var meridian := forward * (-se * cos(orbit_azimuth)) + right * (-se * sin(orbit_azimuth)) + up * ce
	var distance := maxf(_focus_natural * orbit_zoom, WORLD_NEAR * 3.0)

	var eye_scene := _focus_position + offset * distance
	world_camera.position = eye_scene
	var basis := _aim(-offset, meridian)
	world_camera.basis = basis
	near_camera.position = (eye_scene - _ship_position) / _render_scale
	near_camera.basis = basis


func _orbit_frame(beta: Vector3, target_direction: Vector3) -> Array:
	## Devolve [forward, up]; quem chama constrói `right` a partir deles, para que
	## a lateralidade viva num lugar só.
	match mode:
		Mode.EXTERNAL_ORBIT:
			# Os eixos do próprio casco. Azimute 180 é exatamente atrás da cauda,
			# 0 é de frente no nariz. É o que se quer para OLHAR A NAVE, e a
			# primeira versão desta câmera usava o referencial da velocidade para
			# tudo -- com o que simplesmente não dava para chegar atrás dela.
			return [_ship_basis.x.normalized(), _ship_basis.z.normalized()]
		Mode.TARGET_REFERENCE:
			var to_target := target_direction
			if to_target.length() < 0.5:
				to_target = _ship_basis.x.normalized()
			return [to_target.normalized(), _stable_up(to_target.normalized())]
		_:
			# O referencial da velocidade: `forward` é o eixo em torno do qual o
			# céu é aberrado.
			var forward := beta.normalized()
			if forward.length() < 0.5:
				forward = Vector3.FORWARD
			return [forward, _stable_up(forward)]


func _stable_up(forward: Vector3) -> Vector3:
	## O polo J2000, e a escolha tem duas justificativas.
	##
	## A de princípio: estes modos existem para olhar o CÉU, e o céu não gira.
	## Uma referência inercial mantém o campo de estrelas parado enquanto a nave
	## dá a volta.
	##
	## A medida: a alternativa óbvia -- para fora do corpo de referência -- é
	## quase PARALELA à velocidade baricêntrica em órbita baixa. Medido
	## |β·radial| = 0,99, com o que o produto vetorial que fixa o azimute tinha
	## comprimento 0,13 e balançava. Contra o polo, |β·ẑ| = 0,128.
	var up := Vector3(0.0, 0.0, 1.0)
	if absf(forward.dot(up)) > 0.99:
		# Uma trajetória a sair pelo polo celeste. Nada no Sistema Solar faz
		# isto, e trocar aqui É uma descontinuidade -- deixada visível em vez de
		# suavizada, porque fingir que é contínua seria o mesmo erro do guard.
		up = Vector3(0.0, 1.0, 0.0)
	return up


func _aim(forward: Vector3, up: Vector3) -> Basis:
	## `up` tem de ser perpendicular a `forward`, e quem chama garante isso por
	## construção e não por verificação.
	var f := forward.normalized()
	var u := up.normalized()
	var right := f.cross(u).normalized()
	# A câmera do Godot olha ao longo de -Z.
	var basis := Basis(right, u, -f)
	basis = basis.rotated(basis.y, look_yaw)
	basis = basis.rotated(basis.x, look_pitch)
	return basis.orthonormalized()


func _commit(eye_metres: Vector3, basis: Basis) -> void:
	## A única conversão entre as duas escalas do projeto, e está aqui.
	near_camera.position = eye_metres
	near_camera.basis = basis
	world_camera.position = _ship_position + eye_metres * _render_scale
	world_camera.basis = basis


# --- entrada -----------------------------------------------------------------

func handle_mouse_button(event: InputEventMouseButton) -> bool:
	match event.button_index:
		MOUSE_BUTTON_RIGHT:
			_dragging = event.pressed
			_set_capture(_dragging)
			return true
		MOUSE_BUTTON_WHEEL_UP:
			if event.pressed and not is_cockpit():
				zoom(-1.0)
				return true
		MOUSE_BUTTON_WHEEL_DOWN:
			if event.pressed and not is_cockpit():
				zoom(1.0)
				return true
	return false


func handle_mouse_motion(event: InputEventMouseMotion, free_look: bool) -> bool:
	if is_cockpit():
		# No cockpit o mouse olha em volta sempre que está capturado, sem botão:
		# é a cabeça do piloto e não um manipulador de objeto.
		if not (_mouse_look or _dragging):
			return false
		_apply_look(-event.relative.x, -event.relative.y)
		return true
	if not _dragging:
		return false
	if free_look:
		_apply_look(-event.relative.x, -event.relative.y)
	else:
		orbit_azimuth -= event.relative.x * 0.006 * mouse_sensitivity
		orbit_elevation += event.relative.y * 0.006 * mouse_sensitivity
		_settle()
	return true


func set_mouse_look(enabled: bool) -> void:
	_mouse_look = enabled
	_set_capture(enabled)


func _set_capture(enabled: bool) -> void:
	if DisplayServer.get_name() == "headless":
		return
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if enabled else Input.MOUSE_MODE_VISIBLE


func _apply_look(dx: float, dy: float) -> void:
	var sign_y := -1.0 if invert_look else 1.0
	look_yaw += dx * 0.0035 * mouse_sensitivity
	look_pitch += dy * 0.0035 * mouse_sensitivity * sign_y
	_settle()


func apply_keys(delta: float, yaw_axis: float, pitch_axis: float, free_look: bool) -> void:
	var step := 1.4 * delta
	if is_cockpit() or free_look:
		look_yaw += yaw_axis * step
		look_pitch += pitch_axis * step
	else:
		orbit_azimuth += yaw_axis * step
		orbit_elevation += pitch_axis * step
	_settle()


func zoom(steps: float) -> void:
	orbit_zoom = clampf(orbit_zoom * pow(ZOOM_STEP, steps), ZOOM_MIN, ZOOM_MAX)


func recentre() -> void:
	look_yaw = 0.0
	look_pitch = 0.0
	orbit_zoom = 1.0
	if MODE_PRESET.has(mode):
		orbit_azimuth = MODE_PRESET[mode][0]
		orbit_elevation = MODE_PRESET[mode][1]


func at_preset() -> bool:
	## Se a câmera está de facto onde o preset a põe. O mostrador precisa disto:
	## um rótulo que continua dizendo "prógrado" depois de o piloto ter orbitado
	## para longe do prógrado é uma leitura que mente.
	if is_cockpit():
		return absf(look_yaw) < 0.02 and absf(look_pitch) < 0.02
	if not MODE_PRESET.has(mode):
		return false
	return (absf(wrapf(orbit_azimuth - MODE_PRESET[mode][0], -PI, PI)) < 0.02
		and absf(orbit_elevation - MODE_PRESET[mode][1]) < 0.02
		and absf(look_yaw) < 0.02 and absf(look_pitch) < 0.02)


func look_direction() -> Vector3:
	return -world_camera.global_transform.basis.z


func _settle() -> void:
	orbit_azimuth = wrapf(orbit_azimuth, -PI, PI)
	orbit_elevation = clampf(orbit_elevation, -ORBIT_ELEVATION_LIMIT, ORBIT_ELEVATION_LIMIT)
	if is_cockpit():
		look_yaw = clampf(look_yaw, -LOOK_YAW_LIMIT, LOOK_YAW_LIMIT)
		look_pitch = clampf(look_pitch, -LOOK_PITCH_LIMIT, LOOK_PITCH_LIMIT)
	else:
		look_yaw = wrapf(look_yaw, -PI, PI)
		look_pitch = clampf(look_pitch, -ORBIT_ELEVATION_LIMIT, ORBIT_ELEVATION_LIMIT)
